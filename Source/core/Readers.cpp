#include "Readers.hpp"

#include <libgg/Todo.hpp>

#include <libgg/Markers.hpp>

#include <libgg/Conflicts.hpp>
#include <libgg/GitRunner.hpp>
#include <libgg/Thread.hpp>

#include <algorithm>
#include <chrono>
#include <cstring>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <unordered_set>

namespace ggui::core {

using namespace gg::git2;
namespace fs = std::filesystem;

Oid toOid(const git_oid& oid)
{
    return Oid::fromBytes(oid.id, oid.type == GIT_OID_SHA256 ? 32 : 20);
}

git_oid toGit(const Oid& oid)
{
    git_oid out;
    std::memset(&out, 0, sizeof(out));
    git_oid_fromraw(&out, oid.bytes.data(), oid.size == 32 ? GIT_OID_SHA256 : GIT_OID_SHA1);
    return out;
}

namespace {

constexpr std::uint64_t kMaxTextBytes = 2 * 1024 * 1024;

std::string readFileText(const fs::path& p)
{
    std::ifstream f(p, std::ios::binary);
    if (!f)
        return {};
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

std::string stripPrefix(const std::string& s, const std::string& prefix)
{
    return s.rfind(prefix, 0) == 0 ? s.substr(prefix.size()) : s;
}

int readInt(const fs::path& p)
{
    const std::string s = gg::trim(readFileText(p));
    return std::atoi(s.c_str());
}

// "ref: refs/heads/x" → x ; "<oid>" → "" (detached, the id in `oid`)
std::string headFileBranch(const fs::path& headFile, Oid& oid)
{
    const std::string s = gg::trim(readFileText(headFile));
    if (s.rfind("ref: ", 0) == 0)
        return stripPrefix(s.substr(5), "refs/heads/");
    oid = Oid::fromHex(s);
    return {};
}

std::vector<RebaseStep> rebaseSteps(const std::string& text)
{
    std::vector<RebaseStep> out;
    for (const auto& item : gg::todo::parse(text).items) {
        RebaseStep step;
        step.action = gg::todo::actionName(item.action);
        if (item.isCommit()) {
            if (item.fixup != gg::todo::FixupMessage::None)
                step.action += item.fixup == gg::todo::FixupMessage::Use ? " -C" : " -c";
            step.commit = item.commit;
            step.text = item.subject;
        } else {
            step.text = item.arg;
        }
        out.push_back(std::move(step));
    }
    return out;
}

RebaseProgress readRebaseProgress(git_repository* repo, const fs::path& dir)
{
    RebaseProgress p;
    p.done = rebaseSteps(readFileText(dir / "done"));
    p.todoText = readFileText(dir / "git-rebase-todo");
    p.remaining = rebaseSteps(p.todoText);
    p.headName = gg::trim(readFileText(dir / "head-name"));
    // Lines written without the subject (older git, or a todo typed that way): the commit's.
    for (auto* steps : {&p.done, &p.remaining})
        for (auto& step : *steps) {
            if (step.commit.empty() || !step.text.empty())
                continue;
            git_object* raw = nullptr;
            if (git_revparse_single(&raw, repo, (step.commit + "^{commit}").c_str()) == 0) {
                Object obj(raw);
                const char* summary = git_commit_summary(reinterpret_cast<git_commit*>(obj.get()));
                step.text = summary ? summary : "";
                step.commit = toHex(*git_object_id(obj.get()));
            }
            git_error_clear();
        }
    return p;
}

} // namespace

RepoState detectState(git_repository* repo, std::string& detail)
{
    detail.clear();
    const fs::path gitDir = git_repository_path(repo);
    std::error_code ec;
    if (fs::exists(gitDir / "rebase-merge", ec)) {
        const int done = readInt(gitDir / "rebase-merge" / "msgnum");
        const int end = readInt(gitDir / "rebase-merge" / "end");
        if (end > 0)
            detail = std::to_string(done) + "/" + std::to_string(end);
        // git >= 2.26 runs every merge-backend rebase (plain git rebase too) as an interactive
        // one: rebase-merge/interactive always exists, and so does the todo.
        return RepoState::RebasingInteractive;
    }
    if (fs::exists(gitDir / "rebase-apply", ec)) {
        const int next = readInt(gitDir / "rebase-apply" / "next");
        const int last = readInt(gitDir / "rebase-apply" / "last");
        if (last > 0)
            detail = std::to_string(next) + "/" + std::to_string(last);
        return RepoState::Rebasing;
    }
    if (fs::exists(gitDir / "MERGE_HEAD", ec))
        return RepoState::Merging;
    if (fs::exists(gitDir / "CHERRY_PICK_HEAD", ec))
        return RepoState::CherryPicking;
    if (fs::exists(gitDir / "REVERT_HEAD", ec))
        return RepoState::Reverting;
    if (fs::exists(gitDir / "sequencer" / "todo", ec)) {
        // A multi-commit cherry-pick/revert stopped between commits.
        const std::string todo = readFileText(gitDir / "sequencer" / "todo");
        return todo.rfind("revert", 0) == 0 ? RepoState::Reverting : RepoState::CherryPicking;
    }
    // Per worktree, like the bisect's other files.
    if (fs::exists(gitDir / "BISECT_START", ec))
        return RepoState::Bisecting;
    return RepoState::None;
}

bool isPublished(git_repository* repo, const git_oid& id)
{
    std::vector<git_oid> tips;
    forEachReference(repo, [&](git_reference* ref) {
        const char* name = git_reference_name(ref);
        if (std::strncmp(name, "refs/remotes/", 13) == 0 && git_reference_type(ref) == GIT_REFERENCE_DIRECT)
            tips.push_back(*git_reference_target(ref));
        return true;
    });
    if (tips.empty())
        return false;
    for (const auto& t : tips)
        if (git_oid_equal(&t, &id))
            return true;
    return git_graph_reachable_from_any(repo, &id, tips.data(), tips.size()) == 1;
}

SnapshotPtr readSnapshot(git_repository* repo, std::uint64_t generation, const gg::CancelToken& cancel)
{
    gg::assertNotUiThread("readSnapshot");
    auto snap = std::make_shared<Snapshot>();
    snap->generation = generation;
    snap->bare = git_repository_is_bare(repo) == 1;
    if (const char* wd = git_repository_workdir(repo))
        snap->workdir = fs::path(wd).lexically_normal();
    snap->gitDir = fs::path(git_repository_path(repo)).lexically_normal();
    snap->commonDir = fs::path(git_repository_commondir(repo)).lexically_normal();
    // libgit2's directories end with a separator: the name is the parent path's last part.
    auto leaf = [](const fs::path& p) { return p.parent_path().filename().string(); };
    snap->name = snap->bare ? leaf(snap->gitDir) : leaf(snap->workdir);
    snap->objectFormat = git_repository_oid_type(repo) == GIT_OID_SHA256 ? "sha256" : "sha1";
    if (git_repository_is_worktree(repo) == 1)
        snap->worktreeId = leaf(snap->gitDir);

    // HEAD
    snap->headUnborn = git_repository_head_unborn(repo) == 1;
    snap->headDetached = git_repository_head_detached(repo) == 1;
    {
        snap->headBranch = stripPrefix(headTarget(repo), "refs/heads/");
        git_oid oid;
        if (git_reference_name_to_id(&oid, repo, "HEAD") == 0) // fails when unborn
            snap->head = toOid(oid);
    }
    snap->state = detectState(repo, snap->stateDetail);
    if (snap->state != RepoState::None) {
        snap->mergeMessage = readFileText(snap->gitDir / "MERGE_MSG");
        if (snap->state == RepoState::RebasingInteractive || snap->state == RepoState::Rebasing) {
            const fs::path dir = fs::exists(snap->gitDir / "rebase-merge") ? snap->gitDir / "rebase-merge"
                                                                           : snap->gitDir / "rebase-apply";
            snap->stateOnto = gg::trim(readFileText(dir / "head-name"));
            snap->stateOnto = stripPrefix(snap->stateOnto, "refs/heads/");
        }
        if (snap->state == RepoState::RebasingInteractive)
            snap->rebase = readRebaseProgress(repo, snap->gitDir / "rebase-merge");
    }

    // Worktrees (needed for branch → worktree mapping)
    {
        WorktreeInfo main;
        main.name = "main";
        main.isMain = true;
        const fs::path common = snap->commonDir;
        const bool commonIsBare = [&] {
            Config cfg = repositoryConfig(repo);
            auto b = configBool(cfg.get(), "core.bare");
            return b.value_or(false);
        }();
        main.bare = commonIsBare;
        main.path = commonIsBare ? common : common.parent_path().parent_path(); // common ends with a separator
        main.branch = headFileBranch(common / "HEAD", main.head);
        if (!main.branch.empty()) {
            git_oid oid;
            if (git_reference_name_to_id(&oid, repo, ("refs/heads/" + main.branch).c_str()) == 0)
                main.head = toOid(oid);
        }
        main.isCurrent = snap->worktreeId == "main";
        snap->worktrees.push_back(main);

        StrArray names;
        git_worktree_list(&names.arr, repo); // on failure the list stays empty
        for (size_t i = 0; i < names.arr.count; ++i) {
            git_worktree* raw = nullptr;
            if (git_worktree_lookup(&raw, repo, names.arr.strings[i]) != 0)
                continue;
            Worktree wt(raw);
            WorktreeInfo info;
            info.name = names.arr.strings[i];
            info.path = fs::path(git_worktree_path(wt.get())).lexically_normal();
            Buf reason;
            info.locked = git_worktree_is_locked(&reason.buf, wt.get()) > 0;
            info.lockReason = gg::trim(reason.str());
            info.prunable = git_worktree_validate(wt.get()) != 0;
            info.branch = headFileBranch(common / "worktrees" / info.name / "HEAD", info.head);
            if (!info.branch.empty()) {
                git_oid oid;
                if (git_reference_name_to_id(&oid, repo, ("refs/heads/" + info.branch).c_str()) == 0)
                    info.head = toOid(oid);
            }
            info.isCurrent = snap->worktreeId == info.name;
            snap->worktrees.push_back(std::move(info));
        }
    }

    // References
    std::vector<std::pair<std::string, git_oid>> localBranches;
    forEachReference(repo, [&](git_reference* ref) {
        gg::throwIfCancelled(cancel);
        const std::string name = git_reference_name(ref);
        if (git_reference_type(ref) != GIT_REFERENCE_DIRECT)
            return true;
        const git_oid* target = git_reference_target(ref);
        if (name.rfind("refs/heads/", 0) == 0) {
            localBranches.emplace_back(name, *target);
        } else if (name.rfind("refs/remotes/", 0) == 0) {
            RemoteBranchInfo rb;
            rb.name = name.substr(13);
            rb.remote = rb.name.substr(0, rb.name.find('/'));
            rb.target = toOid(*target);
            snap->remoteBranches.push_back(std::move(rb));
        } else if (name.rfind("refs/tags/", 0) == 0) {
            TagInfo tag;
            tag.name = name.substr(10);
            git_object* obj = nullptr;
            if (git_object_lookup(&obj, repo, target, GIT_OBJECT_ANY) == 0) {
                Object o(obj);
                if (git_object_type(o.get()) == GIT_OBJECT_TAG) {
                    tag.annotated = true;
                    tag.object = toOid(*target);
                    const char* msg = git_tag_message(reinterpret_cast<git_tag*>(o.get()));
                    tag.message = msg ? msg : "";
                    git_object* peeled = nullptr;
                    if (git_object_peel(&peeled, o.get(), GIT_OBJECT_COMMIT) == 0) {
                        tag.target = toOid(*git_object_id(peeled));
                        git_object_free(peeled);
                    } else {
                        tag.target = toOid(*git_tag_target_id(reinterpret_cast<git_tag*>(o.get())));
                    }
                } else {
                    tag.target = toOid(*target);
                }
            } else {
                tag.target = toOid(*target);
            }
            snap->tags.push_back(std::move(tag));
        } else if (name.rfind("refs/gg/", 0) == 0) {
            snap->oldGgRefs.push_back(name);
        }
        return true;
    });
    // Symbolic refs under refs/gg are also leftovers.
    std::sort(snap->oldGgRefs.begin(), snap->oldGgRefs.end());

    for (const auto& [full, oid] : localBranches) {
        gg::throwIfCancelled(cancel);
        BranchInfo b;
        b.name = full.substr(11);
        b.target = toOid(oid);
        b.isHead = !snap->headDetached && b.name == snap->headBranch && !snap->bare;
        for (const auto& wt : snap->worktrees)
            if (!wt.isCurrent && wt.branch == b.name)
                b.worktree = wt.name;
        Buf up;
        if (git_branch_upstream_name(&up.buf, repo, full.c_str()) == 0) {
            const std::string upRef = up.str();
            b.upstream = stripPrefix(upRef, "refs/remotes/");
            git_oid upOid;
            if (git_reference_name_to_id(&upOid, repo, upRef.c_str()) == 0) {
                size_t ahead = 0, behind = 0;
                if (git_graph_ahead_behind(&ahead, &behind, repo, &oid, &upOid) == 0) {
                    b.ahead = static_cast<int>(ahead);
                    b.behind = static_cast<int>(behind);
                }
            } else {
                b.upstreamGone = true;
            }
        } else {
            git_error_clear();
        }
        snap->branches.push_back(std::move(b));
    }
    // An unborn current branch is still shown as the current branch label (not as a ref).
    auto byName = [](const auto& a, const auto& b) { return a.name < b.name; };
    std::sort(snap->branches.begin(), snap->branches.end(), byName);
    std::sort(snap->remoteBranches.begin(), snap->remoteBranches.end(), byName);
    std::sort(snap->tags.begin(), snap->tags.end(), byName);

    // Remotes
    {
        StrArray names;
        git_remote_list(&names.arr, repo); // on failure the list stays empty
        Config cfg = repositoryConfig(repo);
        for (size_t i = 0; i < names.arr.count; ++i) {
            git_remote* raw = nullptr;
            if (git_remote_lookup(&raw, repo, names.arr.strings[i]) != 0)
                continue;
            Remote r(raw);
            RemoteInfo info;
            info.name = names.arr.strings[i];
            info.url = git_remote_url(r.get()) ? git_remote_url(r.get()) : "";
            info.pushUrl = git_remote_pushurl(r.get()) ? git_remote_pushurl(r.get()) : "";
            info.pruneOnFetch = configBool(cfg.get(), ("remote." + info.name + ".prune").c_str()).value_or(false);
            snap->remotes.push_back(std::move(info));
        }
    }

    // Stashes
    {
        struct Ctx {
            git_repository* repo;
            std::vector<StashInfo>* out;
        } ctx{repo, &snap->stashes};
        git_stash_foreach(
            repo,
            [](size_t index, const char* message, const git_oid* id, void* payload) -> int {
                auto* c = static_cast<Ctx*>(payload);
                StashInfo s;
                s.index = static_cast<int>(index);
                s.message = message ? message : "";
                s.commit = toOid(*id);
                git_commit* raw = nullptr;
                if (git_commit_lookup(&raw, c->repo, id) == 0) {
                    Commit commit(raw);
                    s.time = git_commit_time(commit.get());
                    if (git_commit_parentcount(commit.get()) > 0)
                        s.base = toOid(*git_commit_parent_id(commit.get(), 0));
                    s.hasUntracked = git_commit_parentcount(commit.get()) > 2;
                    if (git_commit_parentcount(commit.get()) > 1) {
                        git_commit* base = nullptr;
                        git_commit* indexCommit = nullptr;
                        if (git_commit_parent(&base, commit.get(), 0) == 0 && git_commit_parent(&indexCommit, commit.get(), 1) == 0)
                            s.hasIndexChanges = !git_oid_equal(git_commit_tree_id(base), git_commit_tree_id(indexCommit));
                        git_commit_free(base);
                        git_commit_free(indexCommit);
                    }
                }
                c->out->push_back(std::move(s));
                return 0;
            },
            &ctx);
        git_error_clear();
    }
    return snap;
}

// ---- Status ------------------------------------------------------------------------------------

namespace {

ChangeKind kindFromDelta(git_delta_t s)
{
    switch (s) {
    case GIT_DELTA_ADDED: return ChangeKind::Added;
    case GIT_DELTA_DELETED: return ChangeKind::Deleted;
    case GIT_DELTA_RENAMED: return ChangeKind::Renamed;
    case GIT_DELTA_COPIED: return ChangeKind::Copied;
    case GIT_DELTA_TYPECHANGE: return ChangeKind::TypeChanged;
    case GIT_DELTA_UNTRACKED: return ChangeKind::Untracked;
    case GIT_DELTA_CONFLICTED: return ChangeKind::Conflicted;
    default: return ChangeKind::Modified;
    }
}

// Stages present: base (1), ours (2), theirs (3).
std::string conflictDescription(bool s1, bool s2, bool s3)
{
    static const char* const kByStages[8] = {"both deleted", "added by them", "added by us", "both added",
        "both deleted", "deleted by us", "deleted by them", "both modified"};
    return kByStages[(int(s1) << 2) | (int(s2) << 1) | int(s3)];
}

StatusEntry entryFromDelta(const git_diff_delta* d)
{
    StatusEntry e;
    e.kind = kindFromDelta(d->status);
    e.path = d->new_file.path;
    if (d->status == GIT_DELTA_DELETED)
        e.path = d->old_file.path;
    if (d->status == GIT_DELTA_RENAMED || d->status == GIT_DELTA_COPIED)
        e.oldPath = d->old_file.path;
    e.binary = (d->flags & GIT_DIFF_FLAG_BINARY) != 0;
    return e;
}

} // namespace

StatusPtr readStatus(git_repository* repo, std::uint64_t generation, const gg::CancelToken& cancel,
    const std::function<void(StatusPtr)>& partial)
{
    gg::assertNotUiThread("readStatus");
    auto result = std::make_shared<StatusResult>();
    result->generation = generation;
    if (git_repository_is_bare(repo) == 1)
        return result;

    git_index* rawIndex = nullptr;
    check(git_repository_index(&rawIndex, repo), "git_repository_index");
    Index index(rawIndex);
    git_index_read(index.get(), 0);

    // Conflicts (index stages 1–3)
    std::set<std::string> conflicted;
    if (git_index_has_conflicts(index.get())) {
        git_index_conflict_iterator* rawIt = nullptr;
        if (git_index_conflict_iterator_new(&rawIt, index.get()) == 0) {
            const git_index_entry *anc = nullptr, *ours = nullptr, *theirs = nullptr;
            while (git_index_conflict_next(&anc, &ours, &theirs, rawIt) == 0) {
                StatusEntry e;
                e.kind = ChangeKind::Conflicted;
                e.path = ours ? ours->path : theirs ? theirs->path : anc->path;
                e.stage1 = anc != nullptr;
                e.stage2 = ours != nullptr;
                e.stage3 = theirs != nullptr;
                e.conflictDescription = conflictDescription(e.stage1, e.stage2, e.stage3);
                // Binary if any side is (git's heuristic on the blob): no text regions possible.
                for (const git_index_entry* side : {anc, ours, theirs}) {
                    git_blob* blob = nullptr;
                    if (side && git_blob_lookup(&blob, repo, &side->id) == 0) {
                        e.binary = e.binary || git_blob_is_binary(blob);
                        git_blob_free(blob);
                    } else {
                        git_error_clear();
                    }
                }
                conflicted.insert(e.path);
                result->conflicted.push_back(std::move(e));
            }
            git_index_conflict_iterator_free(rawIt);
        }
    }

    // Intent-to-add entries
    std::set<std::string> ita;
    const size_t n = git_index_entrycount(index.get());
    for (size_t i = 0; i < n; ++i) {
        const git_index_entry* e = git_index_get_byindex(index.get(), i);
        if (e->flags_extended & GIT_INDEX_ENTRY_INTENT_TO_ADD)
            ita.insert(e->path);
    }

    // Staged: HEAD tree → index
    {
        Tree headTree;
        git_oid headId;
        if (git_reference_name_to_id(&headId, repo, "HEAD") == 0) {
            Commit c = lookupCommit(repo, headId);
            headTree = commitTree(c.get());
        }
        git_diff_options opts = GIT_DIFF_OPTIONS_INIT;
        opts.flags = GIT_DIFF_INCLUDE_TYPECHANGE;
        git_diff* raw = nullptr;
        check(git_diff_tree_to_index(&raw, repo, headTree.get(), index.get(), &opts), "git_diff_tree_to_index");
        Diff diff(raw);
        git_diff_find_options fo = GIT_DIFF_FIND_OPTIONS_INIT;
        fo.flags = GIT_DIFF_FIND_RENAMES;
        git_diff_find_similar(diff.get(), &fo);
        const size_t count = git_diff_num_deltas(diff.get());
        for (size_t i = 0; i < count; ++i) {
            const git_diff_delta* d = git_diff_get_delta(diff.get(), i);
            StatusEntry e = entryFromDelta(d);
            if (conflicted.count(e.path) || ita.count(e.path) || d->status == GIT_DELTA_CONFLICTED)
                continue;
            result->staged.push_back(std::move(e));
        }
    }

    // Unstaged + untracked: index → working tree, published in pieces on big worktrees.
    {
        struct Ctx {
            StatusResult* result;
            const std::set<std::string>* conflicted;
            const std::set<std::string>* ita;
            const gg::CancelToken* cancel;
            const std::function<void(StatusPtr)>* partial;
            std::chrono::steady_clock::time_point start, lastPublish;
            bool published = false;
        } ctx{result.get(), &conflicted, &ita, &cancel, &partial, std::chrono::steady_clock::now(), {}, false};
        ctx.lastPublish = ctx.start;

        git_diff_options opts = GIT_DIFF_OPTIONS_INIT;
        opts.flags = GIT_DIFF_INCLUDE_UNTRACKED | GIT_DIFF_RECURSE_UNTRACKED_DIRS | GIT_DIFF_INCLUDE_TYPECHANGE
            | GIT_DIFF_UPDATE_INDEX;
        opts.flags &= ~static_cast<unsigned>(GIT_DIFF_UPDATE_INDEX); // never write the index from a reader
        opts.payload = &ctx;
        opts.notify_cb = [](const git_diff* /*sofar*/, const git_diff_delta* d, const char*, void* payload) -> int {
            auto* c = static_cast<Ctx*>(payload);
            if (c->cancel->cancelled())
                return GIT_EUSER;
            StatusEntry e = entryFromDelta(d);
            if (c->conflicted->count(e.path) || d->status == GIT_DELTA_CONFLICTED)
                return 1; // skip: shown under Conflicted
            if (d->status == GIT_DELTA_UNTRACKED) {
                e.kind = ChangeKind::Untracked;
                c->result->untracked.push_back(std::move(e));
            } else {
                if (c->ita->count(e.path)) {
                    e.intentToAdd = true;
                    e.kind = ChangeKind::Added;
                }
                c->result->unstaged.push_back(std::move(e));
            }
            // Publish partial results ("scanning…") once many changes have arrived, and then
            // periodically, so huge worktrees show progress instead of waiting for the full scan.
            const auto now = std::chrono::steady_clock::now();
            const size_t seen = c->result->unstaged.size() + c->result->untracked.size();
            const bool firstBatch = !c->published && seen >= 256;
            const bool periodic = now - c->start > std::chrono::milliseconds(150)
                && now - c->lastPublish > std::chrono::milliseconds(250);
            if (*c->partial && (firstBatch || periodic)) {
                auto snapshot = std::make_shared<StatusResult>(*c->result);
                snapshot->partial = true;
                (*c->partial)(snapshot);
                c->lastPublish = now;
                c->published = true;
            }
            return 1; // we collected it ourselves; libgit2 need not keep it
        };
        opts.progress_cb = [](const git_diff*, const char*, const char*, void* payload) -> int {
            return static_cast<Ctx*>(payload)->cancel->cancelled() ? GIT_EUSER : 0;
        };
        git_diff* raw = nullptr;
        const int rc = git_diff_index_to_workdir(&raw, repo, index.get(), &opts);
        Diff diff(raw);
        if (cancel.cancelled())
            throw gg::Cancelled{};
        check(rc, "git_diff_index_to_workdir");
        // Intent-to-add files that libgit2 does not report as modified are still "added".
        for (const auto& path : ita) {
            const bool listed = std::any_of(result->unstaged.begin(), result->unstaged.end(),
                [&](const StatusEntry& e) { return e.path == path; });
            if (!listed) {
                StatusEntry e;
                e.path = path;
                e.kind = ChangeKind::Added;
                e.intentToAdd = true;
                result->unstaged.push_back(std::move(e));
            }
        }
    }
    auto byPath = [](const StatusEntry& a, const StatusEntry& b) { return a.path < b.path; };
    std::sort(result->staged.begin(), result->staged.end(), byPath);
    std::sort(result->unstaged.begin(), result->unstaged.end(), byPath);
    std::sort(result->untracked.begin(), result->untracked.end(), byPath);
    std::sort(result->conflicted.begin(), result->conflicted.end(), byPath);
    return result;
}

void addFirstClassConflicts(git_repository* repo, StatusResult& status, void* conflictCache)
{
    if (git_repository_is_bare(repo) == 1)
        return;
    auto& cache = *static_cast<gg::conflicts::Cache*>(conflictCache);
    const fs::path workdir = git_repository_workdir(repo);
    std::set<std::string> native;
    for (const auto& e : status.conflicted)
        native.insert(e.path);
    std::set<std::string> changed;
    for (const auto* list : {&status.staged, &status.unstaged, &status.untracked})
        for (const auto& e : *list)
            changed.insert(e.path);
    std::map<std::string, int> found;
    git_oid head;
    if (git_reference_name_to_id(&head, repo, "HEAD") == 0) {
        for (const auto& f : gg::conflicts::commitConflicts(repo, head, cache))
            if (!changed.count(f.path))
                found[f.path] = f.sides;
    }
    git_error_clear();
    // Edited files: their content on disk decides.
    for (const auto& path : changed) {
        if (native.count(path))
            continue;
        std::error_code ec;
        const fs::path p = workdir / path;
        if (!fs::is_regular_file(p, ec) || fs::file_size(p, ec) > kMaxTextBytes)
            continue;
        const int sides = gg::conflicts::contentSides(readFileText(p));
        if (sides > 0 && gg::conflicts::eligible(repo, nullptr, path))
            found[path] = sides;
    }
    for (const auto& [path, sides] : found) {
        StatusEntry e;
        e.path = path;
        e.kind = ChangeKind::Conflicted;
        e.firstClass = true;
        e.sides = sides;
        e.conflictDescription = std::to_string(sides) + "-sided conflict";
        status.conflicted.push_back(std::move(e));
    }
    std::sort(status.conflicted.begin(), status.conflicted.end(),
        [](const StatusEntry& a, const StatusEntry& b) { return a.path < b.path; });
}

// ---- Diff --------------------------------------------------------------------------------------

namespace {

constexpr size_t kMaxDiffLines = 20000;

bool isImagePath(const std::string& p)
{
    std::string ext = fs::path(p).extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    static const std::set<std::string> kImages{".png", ".jpg", ".jpeg", ".gif", ".bmp", ".webp", ".ico"};
    return kImages.count(ext) > 0;
}

// "WxH" from PNG, GIF, BMP or JPEG header bytes ("" when unknown).
std::string imageDims(const std::string& b)
{
    auto be16 = [&](size_t i) { return (static_cast<unsigned char>(b[i]) << 8) | static_cast<unsigned char>(b[i + 1]); };
    auto le16 = [&](size_t i) { return static_cast<unsigned char>(b[i]) | (static_cast<unsigned char>(b[i + 1]) << 8); };
    auto be32 = [&](size_t i) { return (be16(i) << 16) | be16(i + 2); };
    auto le32 = [&](size_t i) { return le16(i) | (le16(i + 2) << 16); };
    auto dims = [](long w, long h) { return std::to_string(w) + "x" + std::to_string(h); };
    if (b.size() >= 24 && b.compare(1, 3, "PNG") == 0)
        return dims(be32(16), be32(20));
    if (b.size() >= 10 && b.compare(0, 3, "GIF") == 0)
        return dims(le16(6), le16(8));
    if (b.size() >= 26 && b.compare(0, 2, "BM") == 0)
        return dims(le32(18), std::labs(static_cast<int>(le32(22))));
    if (b.size() >= 4 && static_cast<unsigned char>(b[0]) == 0xFF && static_cast<unsigned char>(b[1]) == 0xD8) {
        size_t i = 2;
        while (i + 9 < b.size()) {
            if (static_cast<unsigned char>(b[i]) != 0xFF) {
                ++i;
                continue;
            }
            const unsigned char marker = static_cast<unsigned char>(b[i + 1]);
            if (marker >= 0xC0 && marker <= 0xC3)
                return dims(be16(i + 7), be16(i + 5));
            i += 2 + static_cast<size_t>(be16(i + 2));
        }
    }
    return {};
}

std::string rawBlob(git_repository* repo, const git_oid& id)
{
    if (git_oid_is_zero(&id))
        return {};
    git_blob* raw = nullptr;
    if (git_blob_lookup(&raw, repo, &id) != 0) {
        git_error_clear();
        return {};
    }
    Blob b(raw);
    return blobContent(b.get());
}

std::shared_ptr<std::vector<std::string>> splitText(const std::string& s)
{
    auto lines = std::make_shared<std::vector<std::string>>();
    size_t start = 0;
    while (start < s.size()) {
        size_t end = s.find('\n', start);
        if (end == std::string::npos) {
            lines->push_back(s.substr(start));
            break;
        }
        size_t e = end;
        if (e > start && s[e - 1] == '\r')
            --e;
        lines->push_back(s.substr(start, e - start));
        start = end + 1;
    }
    return lines;
}

Tree treeOf(git_repository* repo, const Oid& commitId)
{
    if (commitId.isNull())
        return Tree();
    const git_oid oid = toGit(commitId);
    Commit c = lookupCommit(repo, oid);
    return commitTree(c.get());
}

Tree parentTree(git_repository* repo, const Oid& commitId, unsigned n)
{
    const git_oid oid = toGit(commitId);
    Commit c = lookupCommit(repo, oid);
    if (git_commit_parentcount(c.get()) <= n)
        return Tree();
    git_commit* raw = nullptr;
    check(git_commit_parent(&raw, c.get(), n), "git_commit_parent");
    Commit p(raw);
    return commitTree(p.get());
}

std::string blobText(git_repository* repo, const git_oid& id, std::uint64_t* size)
{
    if (git_oid_is_zero(&id))
        return {};
    git_blob* raw = nullptr;
    if (git_blob_lookup(&raw, repo, &id) != 0)
        return {};
    Blob b(raw);
    *size = static_cast<std::uint64_t>(git_blob_rawsize(b.get()));
    if (*size > kMaxTextBytes || git_blob_is_binary(b.get()))
        return {};
    return blobContent(b.get());
}

} // namespace

namespace {

// The hunks of `patch` into `f`: at most kMaxDiffLines lines (then `truncated`) unless `full`.
// (Binary patches have no hunks.)
void readHunks(git_patch* patch, DiffFile& f, bool full)
{
    size_t total = 0;
    const size_t hunks = git_patch_num_hunks(patch);
    for (size_t h = 0; h < hunks && !f.truncated; ++h) {
        const git_diff_hunk* gh = nullptr;
        size_t lines = 0;
        git_patch_get_hunk(&gh, &lines, patch, h);
        DiffHunk hunk;
        hunk.header = std::string(gh->header, gh->header_len);
        while (!hunk.header.empty() && (hunk.header.back() == '\n' || hunk.header.back() == '\r'))
            hunk.header.pop_back();
        hunk.oldStart = gh->old_start;
        hunk.oldLines = gh->old_lines;
        hunk.newStart = gh->new_start;
        hunk.newLines = gh->new_lines;
        for (size_t l = 0; l < lines; ++l) {
            const git_diff_line* gl = nullptr;
            git_patch_get_line_in_hunk(&gl, patch, h, l);
            if (gl->origin == GIT_DIFF_LINE_CONTEXT_EOFNL || gl->origin == GIT_DIFF_LINE_ADD_EOFNL
                || gl->origin == GIT_DIFF_LINE_DEL_EOFNL) {
                hunk.lines.back().noNewline = true; // always after the line it is about
                continue;
            }
            if (!full && total >= kMaxDiffLines) {
                f.truncated = true;
                break;
            }
            DiffLine line;
            line.origin = gl->origin;
            line.oldNo = gl->old_lineno;
            line.newNo = gl->new_lineno;
            line.text.assign(gl->content, gl->content_len); // never empty: a line or its "\n"
            if (line.text.back() == '\n') {
                line.text.pop_back();
                if (!line.text.empty() && line.text.back() == '\r') {
                    line.text.pop_back();
                    line.crlf = true;
                }
            }
            hunk.lines.push_back(std::move(line));
            ++total;
        }
        f.hunks.push_back(std::move(hunk));
    }
}

} // namespace

DiffPtr readDiff(git_repository* repo, const DiffQuery& q, const gg::CancelToken& cancel)
{
    gg::assertNotUiThread("readDiff");
    auto result = std::make_shared<DiffResult>();
    result->query = q;

    git_diff_options opts = GIT_DIFF_OPTIONS_INIT;
    opts.context_lines = static_cast<std::uint32_t>(std::max(0, q.context));
    opts.interhunk_lines = 0;
    opts.flags = GIT_DIFF_INCLUDE_TYPECHANGE;
    if (q.whitespace == Whitespace::IgnoreChanges)
        opts.flags |= GIT_DIFF_IGNORE_WHITESPACE_CHANGE;
    else if (q.whitespace == Whitespace::IgnoreAll)
        opts.flags |= GIT_DIFF_IGNORE_WHITESPACE;
    const bool worktree = q.kind == DiffKind::Unstaged;
    if (worktree)
        opts.flags |= GIT_DIFF_INCLUDE_UNTRACKED | GIT_DIFF_RECURSE_UNTRACKED_DIRS | GIT_DIFF_SHOW_UNTRACKED_CONTENT;
    opts.payload = const_cast<gg::CancelToken*>(&cancel);
    opts.progress_cb = [](const git_diff*, const char*, const char*, void* payload) -> int {
        return static_cast<gg::CancelToken*>(payload)->cancelled() ? GIT_EUSER : 0;
    };
    // Single-file queries still diff the whole tree so renames can be matched.

    git_diff* raw = nullptr;
    Tree a, b;
    Index index;
    switch (q.kind) {
    case DiffKind::Commit:
        a = parentTree(repo, q.a, 0);
        b = treeOf(repo, q.a);
        check(git_diff_tree_to_tree(&raw, repo, a.get(), b.get(), &opts), "git_diff_tree_to_tree");
        break;
    case DiffKind::Commits:
        a = treeOf(repo, q.a);
        b = treeOf(repo, q.b);
        check(git_diff_tree_to_tree(&raw, repo, a.get(), b.get(), &opts), "git_diff_tree_to_tree");
        break;
    case DiffKind::Staged: {
        git_oid head;
        if (git_reference_name_to_id(&head, repo, "HEAD") == 0)
            a = treeOf(repo, toOid(head));
        git_index* ri = nullptr;
        check(git_repository_index(&ri, repo), "git_repository_index");
        index.reset(ri);
        git_index_read(index.get(), 0);
        check(git_diff_tree_to_index(&raw, repo, a.get(), index.get(), &opts), "git_diff_tree_to_index");
        break;
    }
    case DiffKind::Unstaged: {
        git_index* ri = nullptr;
        check(git_repository_index(&ri, repo), "git_repository_index");
        index.reset(ri);
        git_index_read(index.get(), 0);
        check(git_diff_index_to_workdir(&raw, repo, index.get(), &opts), "git_diff_index_to_workdir");
        break;
    }
    case DiffKind::StashWorktree:
        a = parentTree(repo, q.a, 1); // index commit
        b = treeOf(repo, q.a);
        check(git_diff_tree_to_tree(&raw, repo, a.get(), b.get(), &opts), "git_diff_tree_to_tree");
        break;
    case DiffKind::StashIndex:
        a = parentTree(repo, q.a, 0); // base
        b = parentTree(repo, q.a, 1); // index commit
        check(git_diff_tree_to_tree(&raw, repo, a.get(), b.get(), &opts), "git_diff_tree_to_tree");
        break;
    case DiffKind::StashUntracked:
        b = parentTree(repo, q.a, 2);
        check(git_diff_tree_to_tree(&raw, repo, nullptr, b.get(), &opts), "git_diff_tree_to_tree");
        break;
    case DiffKind::Stages:
    case DiffKind::Term:
        break;
    }
    // A patch between two buffers or blobs, as one DiffFile.
    auto patchFile = [&](git_patch* rawPatch, const git_oid* oldId, const git_oid* newId) {
        Patch patch(rawPatch);
        DiffFile f;
        f.oldPath = f.newPath = q.path;
        f.kind = ChangeKind::Conflicted;
        if (oldId)
            f.oldId = toOid(*oldId);
        if (newId)
            f.newId = toOid(*newId);
        readHunks(patch.get(), f, true);
        result->files.push_back(std::move(f));
    };
    if (q.kind == DiffKind::Term) {
        // The file's content: the committed blob, or the working tree file.
        std::string text;
        if (!q.a.isNull()) {
            Commit c = lookupCommit(repo, toGit(q.a));
            Tree t = commitTree(c.get());
            git_tree_entry* rawEntry = nullptr;
            check(git_tree_entry_bypath(&rawEntry, t.get(), q.path.c_str()), "git_tree_entry_bypath");
            TreeEntry e(rawEntry);
            Blob blob = lookupBlob(repo, *git_tree_entry_id(e.get()));
            text = blobContent(blob.get());
        } else if (const char* wd = git_repository_workdir(repo)) {
            text = readFileText(std::filesystem::path(wd) / q.path);
        }
        const std::string base = gg::markers::takeBase(text);
        const std::string side = gg::markers::takeSide(text, q.stageB);
        git_patch* rawPatch = nullptr;
        check(git_patch_from_buffers(&rawPatch, base.data(), base.size(), q.path.c_str(), side.data(), side.size(),
                  q.path.c_str(), &opts),
            "git_patch_from_buffers");
        patchFile(rawPatch, nullptr, nullptr);
        return result;
    }
    if (q.kind == DiffKind::Stages) {
        // Blob-to-blob diff between two index stages of a conflicted path.
        git_index* ri = nullptr;
        check(git_repository_index(&ri, repo), "git_repository_index");
        Index idx(ri);
        git_index_read(idx.get(), 0);
        const git_index_entry* stages[4] = {};
        check(git_index_conflict_get(&stages[1], &stages[2], &stages[3], idx.get(), q.path.c_str()), "git_index_conflict_get");
        auto blobOf = [&](int n) -> Blob {
            if (n < 1 || n > 3 || !stages[n])
                return Blob();
            return lookupBlob(repo, stages[n]->id);
        };
        Blob blobA = blobOf(q.stageA);
        Blob blobB = blobOf(q.stageB);
        git_patch* rawPatch = nullptr;
        check(git_patch_from_blobs(&rawPatch, blobA.get(), q.path.c_str(), blobB.get(), q.path.c_str(), &opts),
            "git_patch_from_blobs");
        patchFile(rawPatch, blobA ? git_blob_id(blobA.get()) : nullptr, blobB ? git_blob_id(blobB.get()) : nullptr);
        return result;
    }
    Diff diff(raw);
    gg::throwIfCancelled(cancel);
    git_diff_find_options fo = GIT_DIFF_FIND_OPTIONS_INIT;
    fo.flags = GIT_DIFF_FIND_RENAMES | GIT_DIFF_FIND_COPIES;
    if (worktree)
        fo.flags |= GIT_DIFF_FIND_FOR_UNTRACKED;
    git_diff_find_similar(diff.get(), &fo);

    const size_t count = git_diff_num_deltas(diff.get());
    for (size_t i = 0; i < count; ++i) {
        gg::throwIfCancelled(cancel);
        const git_diff_delta* d = git_diff_get_delta(diff.get(), i);
        const std::string newPath = d->new_file.path;
        const std::string oldPath = d->old_file.path;
        if (!q.path.empty() && q.path != newPath && q.path != oldPath)
            continue;
        if (!q.paths.empty() && std::find(q.paths.begin(), q.paths.end(), newPath) == q.paths.end()
            && std::find(q.paths.begin(), q.paths.end(), oldPath) == q.paths.end())
            continue;
        DiffFile f;
        f.oldPath = oldPath;
        f.newPath = newPath;
        f.kind = kindFromDelta(d->status);
        if (d->status == GIT_DELTA_UNTRACKED)
            f.kind = ChangeKind::Untracked;
        f.oldMode = d->old_file.mode;
        f.newMode = d->new_file.mode;
        f.oldId = toOid(d->old_file.id);
        f.newId = toOid(d->new_file.id);
        f.submodule = d->old_file.mode == GIT_FILEMODE_COMMIT || d->new_file.mode == GIT_FILEMODE_COMMIT;
        f.image = isImagePath(f.path());
        if (f.image && q.withHunks) {
            f.oldImage = imageDims(rawBlob(repo, d->old_file.id));
            if (worktree && !git_repository_is_bare(repo))
                f.newImage = imageDims(readFileText(fs::path(git_repository_workdir(repo)) / newPath));
            else
                f.newImage = imageDims(rawBlob(repo, d->new_file.id));
        }
        f.oldSize = static_cast<std::uint64_t>(d->old_file.size);
        f.newSize = static_cast<std::uint64_t>(d->new_file.size);

        if (q.withHunks && !f.submodule) {
            git_patch* rawPatch = nullptr;
            if (git_patch_from_diff(&rawPatch, diff.get(), i) == 0 && rawPatch) {
                Patch patch(rawPatch);
                const git_diff_delta* pd = git_patch_get_delta(patch.get());
                f.binary = (pd->flags & GIT_DIFF_FLAG_BINARY) != 0;
                // The patch loaded both sides: their sizes are known now (the tree diff's may be 0).
                f.oldSize = static_cast<std::uint64_t>(pd->old_file.size);
                f.newSize = static_cast<std::uint64_t>(pd->new_file.size);
                size_t ctxLines = 0, adds = 0, dels = 0;
                git_patch_line_stats(&ctxLines, &adds, &dels, patch.get());
                f.additions = static_cast<int>(adds);
                f.deletions = static_cast<int>(dels);
                readHunks(patch.get(), f, q.full);
                Buf buf;
                if (!f.truncated && git_patch_to_buf(&buf.buf, patch.get()) == 0)
                    result->patch += buf.str();
            }
            // Full texts for context expansion.
            if (!f.binary && !f.truncated && !q.path.empty()) {
                std::uint64_t size = 0;
                const std::string oldText = blobText(repo, d->old_file.id, &size);
                f.oldText = splitText(oldText);
                if (worktree && !git_repository_is_bare(repo)) {
                    const fs::path p = fs::path(git_repository_workdir(repo)) / newPath;
                    std::error_code ec;
                    if (fs::is_regular_file(p, ec) && fs::file_size(p, ec) <= kMaxTextBytes)
                        f.newText = splitText(readFileText(p));
                } else {
                    f.newText = splitText(blobText(repo, d->new_file.id, &size));
                }
            }
        }
        result->files.push_back(std::move(f));
    }
    return result;
}

// ---- Blame -------------------------------------------------------------------------------------

BlamePtr readBlame(git_repository* repo, const BlameQuery& q, const gg::CancelToken& cancel)
{
    gg::assertNotUiThread("readBlame");
    constexpr size_t kMaxBlameLines = 50000;
    auto result = std::make_shared<BlameResult>();
    result->query = q;

    git_blame_options opts = GIT_BLAME_OPTIONS_INIT;
    opts.flags = GIT_BLAME_USE_MAILMAP;
    git_oid start;
    std::string content;
    const bool worktree = q.commit.isNull();
    if (!worktree) {
        start = toGit(q.commit);
        if (q.beforeCommit) {
            Commit c = lookupCommit(repo, start);
            if (git_commit_parentcount(c.get()) == 0)
                throw std::runtime_error("The commit has no parent: nothing before this change");
            start = *git_commit_parent_id(c.get(), 0);
            result->query.commit = toOid(start);
            result->query.beforeCommit = false;
            // Follow a rename: in the parent the file may have another name.
            Commit parent = lookupCommit(repo, start);
            Tree parentTree = commitTree(parent.get());
            git_tree_entry* probe = nullptr;
            if (git_tree_entry_bypath(&probe, parentTree.get(), q.path.c_str()) == 0) {
                git_tree_entry_free(probe);
            } else {
                git_error_clear();
                Tree tree = commitTree(c.get());
                git_diff* rawDiff = nullptr;
                check(git_diff_tree_to_tree(&rawDiff, repo, parentTree.get(), tree.get(), nullptr), "git_diff_tree_to_tree");
                Diff renames(rawDiff);
                git_diff_find_options fo = GIT_DIFF_FIND_OPTIONS_INIT;
                fo.flags = GIT_DIFF_FIND_RENAMES | GIT_DIFF_FIND_COPIES;
                git_diff_find_similar(renames.get(), &fo);
                bool found = false;
                for (size_t i = 0; i < git_diff_num_deltas(renames.get()); ++i) {
                    const git_diff_delta* d = git_diff_get_delta(renames.get(), i);
                    if ((d->status == GIT_DELTA_RENAMED || d->status == GIT_DELTA_COPIED) && q.path == d->new_file.path) {
                        result->query.path = d->old_file.path;
                        found = true;
                    }
                }
                if (!found)
                    throw std::runtime_error("The file did not exist before this change");
            }
        }
        opts.newest_commit = start;
        Commit c = lookupCommit(repo, start);
        Tree t = commitTree(c.get());
        git_tree_entry* rawEntry = nullptr;
        check(git_tree_entry_bypath(&rawEntry, t.get(), result->query.path.c_str()), "git_tree_entry_bypath");
        TreeEntry entry(rawEntry);
        Blob blob = lookupBlob(repo, *git_tree_entry_id(entry.get()));
        content = blobContent(blob.get());
    } else {
        content = readFileText(fs::path(git_repository_workdir(repo)) / q.path);
    }
    gg::throwIfCancelled(cancel);

    git_blame* rawBlame = nullptr;
    const int rc = git_blame_file(&rawBlame, repo, result->query.path.c_str(), &opts);
    Blame base(rawBlame);
    Blame blame;
    if (rc < 0) {
        // A file that exists only in the working tree has no history at all.
        if (!worktree)
            check(rc, "git_blame_file");
        git_error_clear();
    } else if (worktree) {
        git_blame* rawBuf = nullptr;
        check(git_blame_buffer(&rawBuf, base.get(), content.data(), content.size()), "git_blame_buffer");
        blame.reset(rawBuf);
    }
    git_blame* use = worktree ? blame.get() : base.get();
    gg::throwIfCancelled(cancel);

    const auto lines = splitText(content);
    std::map<std::string, std::pair<std::string, std::int64_t>> commitInfo; // hex → summary/time
    for (size_t i = 0; i < lines->size(); ++i) {
        if (i >= kMaxBlameLines) {
            result->truncated = true;
            break;
        }
        if ((i & 1023) == 0)
            gg::throwIfCancelled(cancel);
        BlameLine line;
        line.lineNo = static_cast<int>(i + 1);
        line.text = (*lines)[i];
        const git_blame_hunk* h = git_blame_get_hunk_byline(use, i + 1);
        if (h && !git_oid_is_zero(&h->final_commit_id)) {
            line.commit = toOid(h->final_commit_id);
            line.origPath = h->orig_path;
            line.origLine = static_cast<int>(h->orig_start_line_number + (i + 1 - h->final_start_line_number));
            if (h->final_signature) {
                line.author = h->final_signature->name;
                line.time = h->final_signature->when.time;
            }
            const std::string key = line.commit.hex();
            auto it = commitInfo.find(key);
            if (it == commitInfo.end()) {
                std::string summary;
                git_commit* rc2 = nullptr;
                if (git_commit_lookup(&rc2, repo, &h->final_commit_id) == 0) {
                    const char* s = git_commit_summary(rc2);
                    summary = s ? s : "";
                    git_commit_free(rc2);
                }
                it = commitInfo.emplace(key, std::make_pair(summary, line.time)).first;
            }
            line.summary = it->second.first;
        } else {
            line.origPath = q.path;
            line.origLine = line.lineNo;
            line.author = "Not committed yet";
        }
        result->lines.push_back(std::move(line));
    }
    return result;
}

// ---- Reflog / details / summary ----------------------------------------------------------------

ReflogPtr readReflog(git_repository* repo, const std::string& ref)
{
    gg::assertNotUiThread("readReflog");
    auto result = std::make_shared<ReflogResult>();
    result->ref = ref;
    git_reflog* raw = nullptr;
    if (git_reflog_read(&raw, repo, ref.c_str()) != 0) {
        git_error_clear();
        return result;
    }
    Reflog log(raw);
    const size_t n = git_reflog_entrycount(log.get());
    for (size_t i = 0; i < n; ++i) {
        const git_reflog_entry* e = git_reflog_entry_byindex(log.get(), i);
        ReflogEntry entry;
        entry.oldId = toOid(*git_reflog_entry_id_old(e));
        entry.newId = toOid(*git_reflog_entry_id_new(e));
        const char* msg = git_reflog_entry_message(e);
        entry.message = msg ? msg : "";
        const git_signature* sig = git_reflog_entry_committer(e); // always parsed with the entry
        entry.committer = sig->name;
        entry.time = sig->when.time;
        result->entries.push_back(std::move(entry));
    }
    return result;
}

CommitDetailsPtr readCommitDetails(git_repository* repo, const Oid& id)
{
    gg::assertNotUiThread("readCommitDetails");
    auto d = std::make_shared<CommitDetails>();
    d->id = id;
    const git_oid oid = toGit(id);
    Commit c = lookupCommit(repo, oid);
    d->message = commitMessage(c.get());
    const git_signature* a = git_commit_author(c.get());
    const git_signature* m = git_commit_committer(c.get());
    d->authorName = a->name;
    d->authorEmail = a->email;
    d->authorTime = a->when.time;
    d->authorOffset = a->when.offset;
    d->committerName = m->name;
    d->committerEmail = m->email;
    d->committerTime = m->when.time;
    for (unsigned i = 0; i < git_commit_parentcount(c.get()); ++i)
        d->parents.push_back(toOid(*git_commit_parent_id(c.get(), i)));
    d->published = isPublished(repo, oid);
    return d;
}

RepoSummary readSummary(const fs::path& path)
{
    RepoSummary s;
    s.path = path;
    std::error_code ec;
    if (!fs::exists(path, ec))
        return s;
    git_repository* raw = nullptr;
    if (git_repository_open_ext(&raw, path.string().c_str(), GIT_REPOSITORY_OPEN_NO_SEARCH, nullptr) != 0) {
        git_error_clear();
        return s;
    }
    Repository repo(raw);
    s.exists = true;
    s.detached = git_repository_head_detached(repo.get()) == 1;
    if (const std::string target = headTarget(repo.get()); !target.empty()) {
        s.branch = stripPrefix(target, "refs/heads/");
        Buf up;
        if (git_branch_upstream_name(&up.buf, repo.get(), target.c_str()) == 0) {
            s.upstream = stripPrefix(up.str(), "refs/remotes/");
            git_oid local, remote;
            if (git_reference_name_to_id(&local, repo.get(), target.c_str()) == 0
                && git_reference_name_to_id(&remote, repo.get(), up.str().c_str()) == 0) {
                size_t ahead = 0, behind = 0;
                if (git_graph_ahead_behind(&ahead, &behind, repo.get(), &local, &remote) == 0) {
                    s.ahead = static_cast<int>(ahead);
                    s.behind = static_cast<int>(behind);
                }
            }
        }
    }
    git_error_clear();
    return s;
}

} // namespace ggui::core
