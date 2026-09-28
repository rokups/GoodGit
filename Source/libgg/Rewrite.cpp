#include "libgg/Rewrite.hpp"

#include "libgg/Conflicts.hpp"
#include "libgg/Git2.hpp"
#include "libgg/GitRunner.hpp"
#include "libgg/Journal.hpp"
#include "libgg/Markers.hpp"
#include "libgg/Thread.hpp"

#include <git2/apply.h>
#include <git2/sys/index.h>
#include <git2/sys/mempack.h>
#include <git2/sys/odb_backend.h>

#include <algorithm>
#include <fstream>
#include <set>
#include <sstream>

namespace gg::rewrite {

namespace fs = std::filesystem;
using namespace gg::git2;

namespace {

constexpr std::uint32_t kModeLink = 0120000;
constexpr std::uint32_t kModeGitlink = 0160000;

std::string readFileBytes(const fs::path& p)
{
    std::ifstream f(p, std::ios::binary);
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

std::string firstLine(const std::string& message)
{
    return message.substr(0, message.find('\n'));
}

} // namespace

struct Rewriter::Impl {
    Repository repo;
    git_odb_backend* mempack = nullptr; // owned by the odb
    fs::path cwd;
    bool bare = false;
    std::string zero;
    gg::conflicts::Cache conflictCache; // memory only
    std::map<std::string, std::string> blobConflicted; // not used for attribute decisions
    Result last;

    // ---- helpers ------------------------------------------------------------------------------
    Commit commit(const std::string& id) { return lookupCommit(repo.get(), *fromHex(id)); }

    std::string treeOf(const std::string& commitId) { return toHex(*git_commit_tree_id(commit(commitId).get())); }

    std::string emptyTree()
    {
        git_treebuilder* raw = nullptr;
        check(git_treebuilder_new(&raw, repo.get(), nullptr), "git_treebuilder_new");
        TreeBuilder builder(raw);
        git_oid out;
        check(git_treebuilder_write(&out, builder.get()), "git_treebuilder_write");
        return toHex(out);
    }

    std::string blobText(const git_oid& id)
    {
        Blob b = lookupBlob(repo.get(), id);
        return blobContent(b.get());
    }

    bool isBinary(const git_oid& id)
    {
        Blob b = lookupBlob(repo.get(), id);
        return git_blob_is_binary(b.get()) != 0;
    }

    git_oid writeBlob(const std::string& content)
    {
        git_oid id;
        check(git_blob_create_from_buffer(&id, repo.get(), content.data(), content.size()), "git_blob_create_from_buffer");
        return id;
    }

    // A tree with the source's change restricted to (or excluding) some paths, against `base`.
    std::string filteredChange(const std::string& baseTree, const std::string& sourceTree,
        const std::vector<std::string>& only, const std::vector<std::string>& except)
    {
        Tree base = lookupTree(repo.get(), *fromHex(baseTree));
        Tree source = lookupTree(repo.get(), *fromHex(sourceTree));
        git_diff* rawDiff = nullptr;
        check(git_diff_tree_to_tree(&rawDiff, repo.get(), base.get(), source.get(), nullptr), "git_diff_tree_to_tree");
        Diff diff(rawDiff);
        auto selected = [&](const std::string& path) {
            auto under = [&](const std::string& p) { return path == p || path.rfind(p + "/", 0) == 0; };
            if (!only.empty())
                return std::any_of(only.begin(), only.end(), under);
            return !std::any_of(except.begin(), except.end(), under);
        };
        std::vector<git_tree_update> updates;
        std::vector<std::string> keep; // path storage
        keep.reserve(git_diff_num_deltas(diff.get()) * 2);
        for (size_t i = 0; i < git_diff_num_deltas(diff.get()); ++i) {
            const git_diff_delta* d = git_diff_get_delta(diff.get(), i);
            const std::string path = d->new_file.path ? d->new_file.path : d->old_file.path;
            if (!selected(path))
                continue;
            keep.push_back(path);
            git_tree_update u{};
            u.path = keep.back().c_str();
            if (d->status == GIT_DELTA_DELETED) {
                u.action = GIT_TREE_UPDATE_REMOVE;
            } else {
                u.action = GIT_TREE_UPDATE_UPSERT;
                u.id = d->new_file.id;
                u.filemode = static_cast<git_filemode_t>(d->new_file.mode);
            }
            updates.push_back(u);
        }
        git_oid out;
        check(git_tree_create_updated(&out, repo.get(), base.get(), updates.size(), updates.data()), "git_tree_create_updated");
        return toHex(out);
    }

    // Three-way merge of trees with first-class text conflicts (see the header).
    std::string mergeTrees(const std::string& baseTree, const std::string& oursTree, const std::string& theirsTree,
        const std::string& stepKey, const std::string& sourceCommit, const Plan& plan, Result& result)
    {
        if (oursTree == baseTree)
            return theirsTree;
        if (theirsTree == baseTree || theirsTree == oursTree)
            return oursTree;
        Tree base = lookupTree(repo.get(), *fromHex(baseTree));
        Tree ours = lookupTree(repo.get(), *fromHex(oursTree));
        Tree theirs = lookupTree(repo.get(), *fromHex(theirsTree));
        git_merge_options opts = GIT_MERGE_OPTIONS_INIT;
        opts.flags = GIT_MERGE_FIND_RENAMES;
        git_index* rawIndex = nullptr;
        check(git_merge_trees(&rawIndex, repo.get(), base.get(), ours.get(), theirs.get(), &opts), "git_merge_trees");
        Index index(rawIndex);
        const git_oid sourceOid = *fromHex(sourceCommit);
        const std::string subject = firstLine(commitMessage(commit(sourceCommit).get()));

        // 1. Conflicts reported by libgit2.
        struct Conflict {
            std::optional<git_index_entry> anc, ours, theirs;
            std::string ancPath, oursPath, theirsPath;
        };
        std::vector<Conflict> conflicts;
        if (git_index_has_conflicts(index.get())) {
            git_index_conflict_iterator* rawIt = nullptr;
            check(git_index_conflict_iterator_new(&rawIt, index.get()), "git_index_conflict_iterator_new");
            const git_index_entry *a = nullptr, *o = nullptr, *t = nullptr;
            while (git_index_conflict_next(&a, &o, &t, rawIt) == 0) {
                Conflict c;
                if (a) {
                    c.anc = *a;
                    c.ancPath = a->path;
                }
                if (o) {
                    c.ours = *o;
                    c.oursPath = o->path;
                }
                if (t) {
                    c.theirs = *t;
                    c.theirsPath = t->path;
                }
                conflicts.push_back(std::move(c));
            }
            git_index_conflict_iterator_free(rawIt);
        }
        // Paths involved in rename conflicts (libgit2 records them as NAME entries).
        std::set<std::string> renamePaths;
        for (size_t i = 0; i < git_index_name_entrycount(index.get()); ++i) {
            const git_index_name_entry* n = git_index_name_get_byindex(index.get(), i);
            for (const char* p : {n->ancestor, n->ours, n->theirs})
                if (p)
                    renamePaths.insert(p);
        }
        auto addEntry = [&](const std::string& path, const git_oid& id, std::uint32_t mode) {
            git_index_entry e{};
            e.path = path.c_str();
            e.id = id;
            e.mode = mode;
            check(git_index_add(index.get(), &e), "git_index_add");
        };
        for (const auto& c : conflicts) {
            const std::string path = c.ours ? c.oursPath : c.theirs ? c.theirsPath : c.ancPath;
            for (const std::string* p : {&c.ancPath, &c.oursPath, &c.theirsPath})
                if (!p->empty())
                    git_index_conflict_remove(index.get(), p->c_str());
            git_error_clear();
            // Rename conflicts are decided per rename (below), not per path.
            if (renamePaths.count(c.ancPath) || renamePaths.count(c.oursPath) || renamePaths.count(c.theirsPath))
                continue;
            const bool renamed = (!c.ancPath.empty() && !c.oursPath.empty() && c.ancPath != c.oursPath)
                || (!c.ancPath.empty() && !c.theirsPath.empty() && c.ancPath != c.theirsPath)
                || (!c.oursPath.empty() && !c.theirsPath.empty() && c.oursPath != c.theirsPath);
            // Kind of conflict.
            std::string kind;
            if (renamed || renamePaths.count(path) || renamePaths.count(c.ancPath))
                kind = "rename";
            else if (!c.ours || !c.theirs)
                kind = "modify/delete";
            else if (c.ours->mode == kModeLink || c.theirs->mode == kModeLink)
                kind = "symlink";
            else if (c.ours->mode == kModeGitlink || c.theirs->mode == kModeGitlink)
                kind = "submodule";
            else if (isBinary(c.ours->id) || isBinary(c.theirs->id) || (c.anc && isBinary(c.anc->id)))
                kind = "binary";
            else if (const std::string why = gg::conflicts::ineligibleReason(repo.get(), &sourceOid, path); !why.empty())
                kind = why;
            else if (c.ours->mode != c.theirs->mode && (!c.anc || (c.anc->mode != c.ours->mode && c.anc->mode != c.theirs->mode)))
                kind = "mode"; // both sides changed the mode differently (or added it differently)
            const std::string key = NonTextConflict::key(stepKey, path);
            const auto decided = plan.resolutions.find(key);
            if (!kind.empty() && (decided == plan.resolutions.end() || (kind == "mode" && !decided->second.mode))) {
                NonTextConflict n;
                n.step = stepKey;
                n.commit = sourceCommit;
                n.subject = subject;
                n.path = path;
                n.kind = kind;
                n.hasBase = c.anc.has_value();
                n.hasOurs = c.ours.has_value();
                n.hasTheirs = c.theirs.has_value();
                n.oursMode = c.ours ? c.ours->mode : 0;
                n.theirsMode = c.theirs ? c.theirs->mode : 0;
                result.unresolved.push_back(n);
            }
            if (kind.empty() || kind == "mode") {
                // Text: first-class conflict through the marker algebra.
                // The mode: the decision, or the side that changed it.
                std::uint32_t mode = c.ours->mode;
                if (kind == "mode")
                    mode = decided != plan.resolutions.end() && decided->second.mode ? *decided->second.mode : c.theirs->mode;
                else if (c.anc && c.ours->mode == c.anc->mode)
                    mode = c.theirs->mode;
                const std::string merged = gg::markers::mergeFiles(c.anc ? blobText(c.anc->id) : std::string(),
                    blobText(c.ours->id), blobText(c.theirs->id));
                addEntry(path, writeBlob(merged), mode);
                continue;
            }
            // Non-text: the decision, or a provisional one (the replayed side) to go on.
            const Resolution r = decided != plan.resolutions.end() ? decided->second : Resolution{Choice::Theirs, {}, {}};
            auto take = [&](const std::optional<git_index_entry>& e, const std::string& p) {
                if (e)
                    addEntry(p.empty() ? path : p, e->id, r.mode ? *r.mode : e->mode);
            };
            switch (r.choice) {
            case Choice::Ours: take(c.ours, c.oursPath); break;
            case Choice::Theirs: take(c.theirs, c.theirsPath); break;
            case Choice::Base: take(c.anc, c.ancPath); break;
            case Choice::Delete: break;
            case Choice::File: {
                const std::uint32_t mode = r.mode ? *r.mode : c.theirs ? c.theirs->mode : c.ours ? c.ours->mode : 0100644;
                addEntry(path, writeBlob(readFileBytes(r.file)), mode);
                break;
            }
            }
        }

        // Rename conflicts: one decision per renamed file (keep side A's name, side B's, the
        // base, or none).
        auto treeEntry = [&](git_tree* t, const char* p) -> std::optional<std::pair<git_oid, std::uint32_t>> {
            if (!p)
                return std::nullopt;
            git_tree_entry* raw = nullptr;
            if (git_tree_entry_bypath(&raw, t, p) != 0) {
                git_error_clear();
                return std::nullopt;
            }
            TreeEntry e(raw);
            return std::make_pair(*git_tree_entry_id(e.get()), static_cast<std::uint32_t>(git_tree_entry_filemode(e.get())));
        };
        for (size_t i = 0; i < git_index_name_entrycount(index.get()); ++i) {
            const git_index_name_entry* n = git_index_name_get_byindex(index.get(), i);
            const std::string anc = n->ancestor ? n->ancestor : (n->ours ? n->ours : n->theirs);
            const std::string key = NonTextConflict::key(stepKey, anc);
            const auto decided = plan.resolutions.find(key);
            if (decided == plan.resolutions.end()) {
                NonTextConflict nt;
                nt.step = stepKey;
                nt.commit = sourceCommit;
                nt.subject = subject;
                nt.path = anc;
                nt.kind = "rename";
                nt.hasBase = n->ancestor != nullptr;
                nt.hasOurs = n->ours != nullptr;
                nt.hasTheirs = n->theirs != nullptr;
                nt.oursPath = n->ours ? n->ours : "";
                nt.theirsPath = n->theirs ? n->theirs : "";
                result.unresolved.push_back(nt);
            }
            const Resolution r = decided != plan.resolutions.end() ? decided->second : Resolution{Choice::Theirs, {}, {}};
            for (const char* p : {n->ancestor, n->ours, n->theirs})
                if (p) {
                    git_index_remove_bypath(index.get(), p);
                    git_index_conflict_remove(index.get(), p);
                }
            git_error_clear();
            std::optional<std::pair<git_oid, std::uint32_t>> keep;
            std::string where;
            switch (r.choice) {
            case Choice::Ours: keep = treeEntry(ours.get(), n->ours); where = n->ours ? n->ours : ""; break;
            case Choice::Theirs: keep = treeEntry(theirs.get(), n->theirs); where = n->theirs ? n->theirs : ""; break;
            case Choice::Base: keep = treeEntry(base.get(), n->ancestor); where = anc; break;
            case Choice::File:
                keep = std::make_pair(writeBlob(readFileBytes(r.file)), std::uint32_t{0100644});
                where = n->theirs ? n->theirs : anc;
                break;
            case Choice::Delete: break;
            }
            if (keep && !where.empty())
                addEntry(where, keep->first, r.mode ? *r.mode : keep->second);
        }

        // 2. Files both sides changed and libgit2 merged cleanly: when any version holds
        //    first-class regions, merge them with the algebra instead (no nesting).
        auto changedPaths = [&](git_tree* a, git_tree* b) {
            git_diff* raw = nullptr;
            check(git_diff_tree_to_tree(&raw, repo.get(), a, b, nullptr), "git_diff_tree_to_tree");
            Diff d(raw);
            std::set<std::string> out;
            for (size_t i = 0; i < git_diff_num_deltas(d.get()); ++i) {
                const git_diff_delta* delta = git_diff_get_delta(d.get(), i);
                if (delta->status == GIT_DELTA_MODIFIED)
                    out.insert(delta->new_file.path);
            }
            return out;
        };
        const auto oursChanged = changedPaths(base.get(), ours.get());
        const auto theirsChanged = changedPaths(base.get(), theirs.get());
        for (const auto& path : oursChanged) {
            if (!theirsChanged.count(path))
                continue;
            const git_index_entry* merged = git_index_get_bypath(index.get(), path.c_str(), 0);
            if (!merged)
                continue;
            auto entryOf = [&](git_tree* t) -> std::optional<git_oid> {
                git_tree_entry* raw = nullptr;
                if (git_tree_entry_bypath(&raw, t, path.c_str()) != 0) {
                    git_error_clear();
                    return std::nullopt;
                }
                TreeEntry e(raw);
                return *git_tree_entry_id(e.get());
            };
            const auto b = entryOf(base.get());
            const auto o = entryOf(ours.get());
            const auto t = entryOf(theirs.get());
            if (!b || !o || !t || isBinary(*o) || isBinary(*t) || !gg::conflicts::eligible(repo.get(), &sourceOid, path))
                continue;
            const std::string bt = blobText(*b), ot = blobText(*o), tt = blobText(*t);
            if (!gg::markers::isConflicted(bt) && !gg::markers::isConflicted(ot) && !gg::markers::isConflicted(tt))
                continue;
            const std::uint32_t mode = merged->mode;
            addEntry(path, writeBlob(gg::markers::mergeFiles(bt, ot, tt)), mode);
        }
        git_oid out;
        check(git_index_write_tree_to(&out, index.get(), repo.get()), "git_index_write_tree_to");
        return toHex(out);
    }

    // `committedAt`: keep that committer date (the committer is still the current user).
    std::string createCommit(const std::vector<std::string>& parents, const std::string& tree, const std::string& message,
        const git_signature* author, const git_time* committedAt = nullptr)
    {
        git_signature* rawCommitter = nullptr;
        git_signature* rawDefaultAuthor = nullptr;
        if (git_signature_default_from_env(&rawDefaultAuthor, &rawCommitter, repo.get()) != 0)
            throw std::runtime_error("please set user.name and user.email (" + lastErrorMessage() + ")");
        Signature committer(rawCommitter);
        Signature defaultAuthor(rawDefaultAuthor);
        if (committedAt) {
            git_signature* raw = nullptr;
            check(git_signature_new(&raw, rawCommitter->name, rawCommitter->email, committedAt->time, committedAt->offset),
                "git_signature_new");
            committer.reset(raw);
        }
        std::vector<Commit> parentCommits;
        std::vector<const git_commit*> ptrs;
        for (const auto& p : parents) {
            parentCommits.push_back(commit(p));
            ptrs.push_back(parentCommits.back().get());
        }
        Tree t = lookupTree(repo.get(), *fromHex(tree));
        git_oid id;
        check(git_commit_create(&id, repo.get(), nullptr, author ? author : defaultAuthor.get(), committer.get(), nullptr,
                  message.c_str(), t.get(), ptrs.size(), ptrs.data()),
            "git_commit_create");
        return toHex(id);
    }
};

Rewriter::Rewriter(const fs::path& repoDir) : m(std::make_unique<Impl>())
{
    assertNotUiThread("Rewriter");
    m->repo = openRepository(repoDir);
    git_odb* odb = nullptr;
    check(git_repository_odb(&odb, m->repo.get()), "git_repository_odb");
    Odb owned(odb);
    check(git_mempack_new(&m->mempack), "git_mempack_new");
    check(git_odb_add_backend(odb, m->mempack, 999), "git_odb_add_backend");
    m->bare = git_repository_is_bare(m->repo.get()) == 1;
    m->cwd = m->bare ? fs::path(git_repository_path(m->repo.get())) : fs::path(git_repository_workdir(m->repo.get()));
    m->zero = std::string(hexSize(oidType(m->repo.get())), '0');
}

Rewriter::~Rewriter() = default;

git_repository* Rewriter::repository() { return m->repo.get(); }

Result Rewriter::compute(const Plan& plan, const gg::CancelToken& cancel)
{
    assertNotUiThread("Rewriter::compute");
    Result result;
    try {
        std::map<std::string, std::string> byKey;     // step key → new commit
        std::map<std::string, std::string> newOf(plan.replaced.begin(), plan.replaced.end()); // original → new
        std::set<std::string> droppedSet(plan.dropped.begin(), plan.dropped.end());
        std::string currentKey; // the step being built (a redirect never points a step at itself)
        std::function<std::string(const std::string&)> resolve = [&](const std::string& ref) -> std::string {
            if (ref.rfind("=", 0) == 0)
                return ref.substr(1);
            if (auto r = plan.parentRedirect.find(ref); r != plan.parentRedirect.end() && r->second != currentKey)
                if (auto it = byKey.find(r->second); it != byKey.end())
                    return it->second;
            if (auto it = byKey.find(ref); it != byKey.end())
                return it->second;
            if (auto it = newOf.find(ref); it != newOf.end())
                return it->second;
            if (droppedSet.count(ref)) {
                Commit c = m->commit(ref);
                if (git_commit_parentcount(c.get()) == 0)
                    return std::string();
                return resolve(toHex(*git_commit_parent_id(c.get(), 0)));
            }
            return ref;
        };

        struct Pending {
            std::string key;
            std::string source;              // "" for Empty and amending steps
            std::vector<std::string> squashed;
            std::vector<std::string> parents;
            std::string tree;
            std::string message;
            std::optional<Person> author;
            bool unchanged = false;
            bool mapSource = true;
            std::vector<std::string> contributors; // original commits whose changes it holds
            std::string dateFrom;            // keepCommitterDate: the commit whose date it keeps
        };
        std::optional<Pending> pending;
        std::map<std::string, std::vector<std::string>> contributorsOf; // new commit → its originals
        std::map<std::string, std::string> reported; // Result::rewritten entries that differ from mapping
        auto originallyEmpty = [&](const std::string& id) {
            Commit c = m->commit(id);
            const std::string base = git_commit_parentcount(c.get()) > 0 ? m->treeOf(toHex(*git_commit_parent_id(c.get(), 0))) : m->emptyTree();
            return toHex(*git_commit_tree_id(c.get())) == base;
        };
        auto flush = [&] {
            if (!pending)
                return;
            // A commit that becomes empty: its change is already in its new parent.
            if (!pending->unchanged && !pending->contributors.empty() && pending->parents.size() <= 1
                && pending->tree == (pending->parents.empty() ? m->emptyTree() : m->treeOf(pending->parents.front()))
                && !std::all_of(pending->contributors.begin(), pending->contributors.end(), originallyEmpty)) {
                BecameEmpty e{pending->key, pending->source, firstLine(pending->message), plan.emptied == Emptied::Drop};
                result.becameEmpty.push_back(e);
                if (e.dropped) {
                    // Left out: what comes after goes onto the parent, and Git reports the commit
                    // as rewritten to that parent.
                    const std::string parent = pending->parents.empty() ? std::string() : pending->parents.front();
                    byKey[pending->key] = parent;
                    result.steps[pending->key] = parent;
                    std::vector<std::string> gone = pending->squashed;
                    if (!pending->source.empty() && pending->mapSource) {
                        if (parent.empty())
                            gone.push_back(pending->source);
                        else
                            newOf[pending->source] = parent;
                    }
                    for (const auto& sq : gone) {
                        droppedSet.insert(sq);
                        if (!parent.empty())
                            reported[sq] = parent;
                    }
                    pending.reset();
                    return;
                }
            }
            std::string id;
            if (pending->unchanged) {
                id = pending->source;
            } else {
                Signature authorSig;
                const git_signature* author = nullptr;
                if (pending->author) {
                    git_signature* raw = nullptr;
                    check(git_signature_new(&raw, pending->author->name.c_str(), pending->author->email.c_str(),
                              pending->author->time, pending->author->offset),
                        "git_signature_new");
                    authorSig.reset(raw);
                    author = authorSig.get();
                } else if (!pending->source.empty()) {
                    Commit c = m->commit(pending->source);
                    git_signature* raw = nullptr;
                    check(git_signature_dup(&raw, git_commit_author(c.get())), "git_signature_dup");
                    authorSig.reset(raw);
                    author = authorSig.get();
                }
                std::optional<git_time> committedAt;
                if (plan.keepCommitterDate && !pending->dateFrom.empty())
                    committedAt = git_commit_committer(m->commit(pending->dateFrom).get())->when;
                id = m->createCommit(pending->parents, pending->tree, pending->message, author,
                    committedAt ? &*committedAt : nullptr);
            }
            byKey[pending->key] = id;
            if (!pending->unchanged && !pending->contributors.empty())
                contributorsOf[id] = pending->contributors;
            if (!pending->source.empty() && id != pending->source && pending->mapSource)
                newOf[pending->source] = id;
            if (pending->unchanged && plan.reportUnchanged && pending->mapSource && !plan.unreported.count(pending->source))
                reported[pending->source] = id;
            // A squashed commit leaves its place: what pointed at it goes to its first parent's
            // replacement (for a squash into the parent that is this very commit). Git's
            // post-rewrite reports it as rewritten to the squash result.
            for (const auto& sq : pending->squashed) {
                droppedSet.insert(sq);
                reported[sq] = id;
            }
            result.steps[pending->key] = id;
            pending.reset();
        };

        for (const Step& step : plan.steps) {
            gg::throwIfCancelled(cancel);
            const std::string key = step.key.empty() ? step.source : step.key;
            if (step.kind == Step::Kind::Squash) {
                if (!pending)
                    throw std::runtime_error("squash without a commit to squash into");
                if (step.amend) {
                    // Finish the previous commit, then amend it (the result gets this key).
                    const std::string previous = pending->key;
                    std::vector<std::string> contributors = pending->contributors;
                    const std::string dateFrom = pending->dateFrom;
                    flush();
                    const std::string target = byKey.at(previous);
                    if (target.empty())
                        throw std::runtime_error("nothing to amend: every commit before it was dropped");
                    Commit t = m->commit(target);
                    Pending p;
                    p.key = key;
                    for (unsigned i = 0; i < git_commit_parentcount(t.get()); ++i)
                        p.parents.push_back(toHex(*git_commit_parent_id(t.get(), i)));
                    p.tree = toHex(*git_commit_tree_id(t.get()));
                    p.message = commitMessage(t.get());
                    const git_signature* a = git_commit_author(t.get());
                    p.author = Person{a->name ? a->name : "", a->email ? a->email : "", a->when.time, a->when.offset};
                    p.mapSource = false;
                    p.contributors = std::move(contributors);
                    p.dateFrom = dateFrom;
                    pending = std::move(p);
                    currentKey = key;
                }
                Commit src = m->commit(step.source);
                const std::string srcTree = toHex(*git_commit_tree_id(src.get()));
                const std::string srcBase = git_commit_parentcount(src.get()) > 0
                    ? m->treeOf(toHex(*git_commit_parent_id(src.get(), 0)))
                    : m->emptyTree();
                pending->tree = m->mergeTrees(srcBase, pending->tree, srcTree, key, step.source, plan, result);
                pending->squashed.push_back(step.source);
                pending->contributors.push_back(step.source);
                pending->unchanged = false;
                if (step.message)
                    pending->message = *step.message;
                if (step.author)
                    pending->author = step.author;
                continue;
            }
            flush();
            currentKey = key;
            Pending p;
            p.key = key;
            p.source = step.kind == Step::Kind::Pick ? step.source : std::string();
            p.mapSource = step.mapSource;
            if (!p.source.empty())
                p.contributors = {p.source};
            p.dateFrom = p.source;
            std::vector<std::string> originalParents;
            if (!step.source.empty()) {
                Commit src = m->commit(step.source);
                for (unsigned i = 0; i < git_commit_parentcount(src.get()); ++i)
                    originalParents.push_back(toHex(*git_commit_parent_id(src.get(), i)));
                p.message = commitMessage(src.get());
            }
            const std::vector<std::string>& parentRefs = step.sourceParents ? originalParents : step.parents;
            for (const auto& r : parentRefs) {
                const std::string id = resolve(r);
                if (!id.empty() && std::find(p.parents.begin(), p.parents.end(), id) == p.parents.end())
                    p.parents.push_back(id);
            }
            if (step.message)
                p.message = *step.message;
            p.author = step.author;
            const std::string newBaseTree = p.parents.empty() ? m->emptyTree() : m->treeOf(p.parents.front());
            if (step.tree) {
                p.tree = *step.tree;
            } else if (step.kind == Step::Kind::Empty) {
                p.tree = newBaseTree;
            } else if (step.kind == Step::Kind::Merge) {
                // Parents merged pairwise onto the first, each against the merge base.
                p.tree = newBaseTree;
                for (size_t k = 1; k < p.parents.size(); ++k) {
                    git_oid a = *fromHex(p.parents.front()), b = *fromHex(p.parents[k]), mb;
                    std::string baseTree = m->emptyTree();
                    if (git_merge_base(&mb, m->repo.get(), &a, &b) == 0)
                        baseTree = m->treeOf(toHex(mb));
                    git_error_clear();
                    p.tree = m->mergeTrees(baseTree, p.tree, m->treeOf(p.parents[k]), key, p.parents[k], plan, result);
                }
            } else {
                Commit src = m->commit(step.source);
                const std::string srcTree = toHex(*git_commit_tree_id(src.get()));
                const std::string oldBaseTree = originalParents.empty() ? m->emptyTree() : m->treeOf(originalParents.front());
                std::string change = srcTree;
                if (!step.onlyPaths.empty() || !step.exceptPaths.empty())
                    change = m->filteredChange(oldBaseTree, srcTree, step.onlyPaths, step.exceptPaths);
                p.tree = m->mergeTrees(oldBaseTree, newBaseTree, change, key, step.source, plan, result);
            }
            if (!step.setFiles.empty()) {
                Tree t = lookupTree(m->repo.get(), *fromHex(p.tree));
                std::vector<git_tree_update> updates;
                for (const auto& [path, content] : step.setFiles) {
                    git_tree_update u{};
                    u.path = path.c_str();
                    if (!content) {
                        u.action = GIT_TREE_UPDATE_REMOVE;
                    } else {
                        u.action = GIT_TREE_UPDATE_UPSERT;
                        u.id = m->writeBlob(*content);
                        u.filemode = GIT_FILEMODE_BLOB;
                        git_tree_entry* raw = nullptr;
                        if (git_tree_entry_bypath(&raw, t.get(), path.c_str()) == 0) {
                            TreeEntry e(raw);
                            u.filemode = git_tree_entry_filemode(e.get());
                        } else {
                            git_error_clear();
                        }
                    }
                    updates.push_back(u);
                }
                git_oid out;
                check(git_tree_create_updated(&out, m->repo.get(), t.get(), updates.size(), updates.data()), "git_tree_create_updated");
                p.tree = toHex(out);
            }
            p.unchanged = step.kind == Step::Kind::Pick && !step.forceNew && p.parents == originalParents && !step.message
                && !step.author && p.tree == m->treeOf(step.source);
            if (step.kind != Step::Kind::Pick && !p.message.size() && step.message)
                p.message = *step.message;
            pending = std::move(p);
        }
        flush();
        for (const auto& d : droppedSet)
            if (!newOf.count(d))
                newOf[d] = resolve(d); // children and branches go to the replacement parent
        for (const auto& [orig, now] : newOf)
            if (orig != now)
                result.mapping[orig] = now;
        result.rewritten = result.mapping;
        for (const auto& [orig, now] : reported)
            result.rewritten[orig] = now;

        // Ref moves: local branches (never remote-tracking ones) and a detached HEAD.
        std::map<std::string, std::string> otherWorktreeBranches;
        {
            git_strarray names{};
            if (git_worktree_list(&names, m->repo.get()) == 0) {
                for (size_t i = 0; i < names.count; ++i) {
                    git_worktree* raw = nullptr;
                    if (git_worktree_lookup(&raw, m->repo.get(), names.strings[i]) != 0)
                        continue;
                    Worktree wt(raw);
                    git_repository* rawRepo = nullptr;
                    if (git_repository_open_from_worktree(&rawRepo, wt.get()) == 0) {
                        Repository wtRepo(rawRepo);
                        git_reference* head = nullptr;
                        if (git_reference_lookup(&head, wtRepo.get(), "HEAD") == 0) {
                            Reference h(head);
                            if (git_reference_type(h.get()) == GIT_REFERENCE_SYMBOLIC)
                                otherWorktreeBranches[git_reference_symbolic_target(h.get())] = names.strings[i];
                        }
                    }
                    git_error_clear();
                }
                git_strarray_dispose(&names);
            }
            git_error_clear();
        }
        std::string headRef; // this worktree's branch, "" when detached
        {
            git_reference* raw = nullptr;
            if (git_reference_lookup(&raw, m->repo.get(), "HEAD") == 0) {
                Reference h(raw);
                if (git_reference_type(h.get()) == GIT_REFERENCE_SYMBOLIC)
                    headRef = git_reference_symbolic_target(h.get());
            }
            git_error_clear();
            git_oid head;
            if (git_reference_name_to_id(&head, m->repo.get(), "HEAD") == 0)
                result.headBefore = toHex(head);
            git_error_clear();
        }
        result.headAfter = result.headBefore;
        if (!plan.keepBranches) {
            forEachReference(m->repo.get(), [&](git_reference* ref) {
                const std::string name = git_reference_name(ref);
                if (name.rfind("refs/heads/", 0) != 0 || git_reference_type(ref) != GIT_REFERENCE_DIRECT)
                    return true;
                const std::string target = toHex(*git_reference_target(ref));
                auto it = result.mapping.find(target);
                if (it == result.mapping.end() || plan.refsToSteps.count(name))
                    return true;
                RefMove mv{name, target, it->second, otherWorktreeBranches.count(name) > 0};
                result.moves.push_back(mv);
                if (name == headRef)
                    result.headAfter = it->second;
                return true;
            });
        }
        // A step key, or "=<id>" for exactly that commit ("" when unknown).
        auto target = [&](const std::string& stepKey) -> std::string {
            if (stepKey.rfind("=", 0) == 0)
                return stepKey.substr(1);
            auto it = byKey.find(stepKey);
            return it == byKey.end() ? std::string() : it->second;
        };
        for (const auto& [ref, stepKey] : plan.refsToSteps) {
            const std::string to = target(stepKey);
            if (to.empty())
                continue;
            git_oid current;
            std::string old;
            if (git_reference_name_to_id(&current, m->repo.get(), ref.c_str()) == 0)
                old = toHex(current);
            git_error_clear();
            if (old == to)
                continue;
            result.moves.push_back(RefMove{ref, old, to, otherWorktreeBranches.count(ref) > 0});
            if (ref == headRef)
                result.headAfter = to;
        }
        if (!plan.detachHeadAt.empty()) {
            if (const std::string to = target(plan.detachHeadAt); !to.empty() && to != result.headBefore) {
                // HEAD becomes detached: its symbolic value is replaced (see apply).
                result.moves.push_back(RefMove{"HEAD", result.headBefore, to, false});
                result.headAfter = to;
            }
        } else if (headRef.empty() && !plan.keepHead && !result.headBefore.empty()) {
            if (auto it = result.mapping.find(result.headBefore); it != result.mapping.end()) {
                result.moves.push_back(RefMove{"HEAD", result.headBefore, it->second, false});
                result.headAfter = it->second;
            }
        }

        // Commits that gain or lose first-class conflicts, compared with every original commit
        // they hold (a squashed conflicted commit carries its conflicts along, it adds none).
        for (const auto& [now, sources] : contributorsOf) {
            std::set<std::pair<std::string, int>> before;
            std::set<std::string> beforePaths;
            for (const auto& src : sources)
                for (const auto& f : gg::conflicts::commitConflicts(m->repo.get(), *fromHex(src), m->conflictCache)) {
                    before.emplace(f.path, f.sides);
                    beforePaths.insert(f.path);
                }
            std::set<std::string> afterPaths;
            bool gained = false;
            for (const auto& f : gg::conflicts::commitConflicts(m->repo.get(), *fromHex(now), m->conflictCache)) {
                afterPaths.insert(f.path);
                gained = gained || !before.count({f.path, f.sides});
            }
            if (gained)
                result.conflicted.push_back(now);
            if (std::any_of(beforePaths.begin(), beforePaths.end(), [&](const std::string& p) { return !afterPaths.count(p); }))
                result.resolved.push_back(now);
        }
        std::vector<git_oid> remoteTips;
        forEachReference(m->repo.get(), [&](git_reference* ref) {
            if (std::string(git_reference_name(ref)).rfind("refs/remotes/", 0) == 0 && git_reference_type(ref) == GIT_REFERENCE_DIRECT)
                remoteTips.push_back(*git_reference_target(ref));
            return true;
        });
        if (!remoteTips.empty())
            for (const auto& [orig, now] : result.mapping) {
                const git_oid o = *fromHex(orig);
                if (git_graph_reachable_from_any(m->repo.get(), &o, remoteTips.data(), remoteTips.size()) == 1)
                    result.published.push_back(orig);
                git_error_clear();
            }
        result.ok = result.unresolved.empty();
        if (!result.ok)
            result.error = std::to_string(result.unresolved.size()) + " conflict(s) need a decision";
    } catch (const std::exception& e) {
        result.ok = false;
        result.error = e.what();
    }
    return result;
}

bool Rewriter::apply(const Plan& plan, Result& result, std::string& error)
{
    assertNotUiThread("Rewriter::apply");
    if (!result.ok) {
        error = result.error.empty() ? "the rewrite is not complete" : result.error;
        return false;
    }
    if (!result.changed())
        return true;
    // 1. pre-rebase may veto a rebase-like rewrite (before anything is written).
    if (plan.rebaseLike && !m->bare) {
        std::vector<std::string> args{"hook", "run", "--ignore-missing", "pre-rebase", "--", plan.upstream};
        const RunResult r = git(m->cwd, args);
        if (!r.ok()) {
            error = "the pre-rebase hook refused the rewrite: " + r.message();
            return false;
        }
    }
    // 2. Objects: the in-memory store becomes one pack.
    try {
        git_buf pack = GIT_BUF_INIT;
        check(git_mempack_dump(&pack, m->repo.get(), m->mempack), "git_mempack_dump");
        if (pack.size > 0) {
            git_odb* rawOdb = nullptr;
            check(git_repository_odb(&rawOdb, m->repo.get()), "git_repository_odb");
            Odb odb(rawOdb);
            git_odb_writepack* wp = nullptr;
            check(git_odb_write_pack(&wp, odb.get(), nullptr, nullptr), "git_odb_write_pack");
            git_indexer_progress stats{};
            const int a = wp->append(wp, pack.ptr, pack.size, &stats);
            const int c = a == 0 ? wp->commit(wp, &stats) : a;
            wp->free(wp);
            git_buf_dispose(&pack);
            check(c, "writing the rewritten objects");
        } else {
            git_buf_dispose(&pack);
        }
    } catch (const std::exception& e) {
        error = e.what();
        return false;
    }
    // 3. Working tree: check first that it can follow (local changes may be in the way).
    const bool headMoved = !m->bare && !result.headBefore.empty() && result.headAfter != result.headBefore;
    const bool moveWorktree = headMoved && !plan.keepWorktree;
    std::string oldTree, newTree;
    if (moveWorktree) {
        git(m->cwd, {"update-index", "-q", "--refresh"}); // stat data, so unchanged files count as unchanged
        oldTree = result.headBefore + "^{tree}";
        newTree = result.headAfter + "^{tree}";
        const RunResult dry = git(m->cwd, {"read-tree", "-m", "-u", "-n", oldTree, newTree});
        if (!dry.ok()) {
            error = "local changes would be overwritten: " + dry.message();
            return false;
        }
    }
    // 4. Every ref in one transaction.
    auto transaction = [&](bool forward) {
        std::string input = "option no-deref\n";
        for (const auto& mv : result.moves) {
            const std::string from = forward ? mv.oldId : mv.newId;
            const std::string to = forward ? mv.newId : mv.oldId;
            if (to.empty())
                input += "delete " + mv.ref + " " + from + "\n";
            else if (from.empty())
                input += "create " + mv.ref + " " + to + "\n";
            else
                input += "update " + mv.ref + " " + to + " " + from + "\n";
        }
        return git(m->cwd, {"update-ref", "--create-reflog", "-m", plan.reflogMessage, "--stdin"}, input);
    };
    const RunResult refs = transaction(true);
    if (!refs.ok()) {
        error = refs.message();
        return false;
    }
    // 5. The working tree follows HEAD (or only the index does).
    if (headMoved && plan.keepWorktree) {
        git(m->cwd, {"read-tree", result.headAfter + "^{tree}"});
        git(m->cwd, {"update-index", "-q", "--refresh"});
    }
    if (moveWorktree) {
        const RunResult r = git(m->cwd, {"read-tree", "-m", "-u", oldTree, newTree});
        if (!r.ok()) {
            transaction(false);
            error = r.message();
            return false;
        }
    }
    // 6. Hooks git would have run, and the mapping in the journal.
    std::vector<std::pair<std::string, std::string>> rewrites;
    for (const auto& [orig, now] : result.rewritten)
        rewrites.emplace_back(orig, now);
    if (!rewrites.empty() && !m->bare) {
        const fs::path mapFile = fs::path(git_repository_commondir(m->repo.get())) / "gg" / ("rewrite-" + journal::Journal::newOperationId());
        std::error_code ec;
        fs::create_directories(mapFile.parent_path(), ec);
        {
            std::ofstream f(mapFile, std::ios::binary);
            for (const auto& [a, b] : rewrites)
                f << a << " " << b << "\n";
        }
        git(m->cwd, {"hook", "run", "--ignore-missing", "--to-stdin=" + mapFile.string(), "post-rewrite", "--", plan.rewriteKind});
        fs::remove(mapFile, ec);
    }
    if (moveWorktree)
        git(m->cwd, {"hook", "run", "--ignore-missing", "post-checkout", "--", result.headBefore, result.headAfter, "1"});
    if (const std::string op = currentOperation(); !op.empty() && !rewrites.empty()) {
        journal::Journal j{fs::path(git_repository_commondir(m->repo.get()))};
        j.appendRewrites(op, rewrites);
    }
    return true;
}

std::vector<std::string> descendants(git_repository* repo, const std::vector<std::string>& changed)
{
    assertNotUiThread("rewrite::descendants");
    std::set<std::string> affected(changed.begin(), changed.end());
    git_revwalk* raw = nullptr;
    check(git_revwalk_new(&raw, repo), "git_revwalk_new");
    Revwalk walk(raw);
    git_revwalk_sorting(walk.get(), GIT_SORT_TOPOLOGICAL | GIT_SORT_REVERSE);
    bool pushed = false;
    forEachReference(repo, [&](git_reference* ref) {
        if (std::string(git_reference_name(ref)).rfind("refs/heads/", 0) == 0 && git_reference_type(ref) == GIT_REFERENCE_DIRECT)
            pushed = git_revwalk_push(walk.get(), git_reference_target(ref)) == 0 || pushed;
        return true;
    });
    git_oid head;
    if (git_reference_name_to_id(&head, repo, "HEAD") == 0)
        pushed = git_revwalk_push(walk.get(), &head) == 0 || pushed;
    for (const auto& c : changed) {
        Commit commit = lookupCommit(repo, *fromHex(c));
        for (unsigned i = 0; i < git_commit_parentcount(commit.get()); ++i)
            git_revwalk_hide(walk.get(), git_commit_parent_id(commit.get(), i));
        git_revwalk_push(walk.get(), git_commit_id(commit.get())); // included even when unreachable
    }
    git_error_clear();
    std::vector<std::string> out;
    git_oid id;
    while (git_revwalk_next(&id, walk.get()) == 0) {
        const std::string hex = toHex(id);
        bool isAffected = affected.count(hex) > 0;
        if (!isAffected) {
            Commit c = lookupCommit(repo, id);
            for (unsigned i = 0; i < git_commit_parentcount(c.get()) && !isAffected; ++i)
                isAffected = affected.count(toHex(*git_commit_parent_id(c.get(), i))) > 0;
        }
        if (isAffected) {
            affected.insert(hex);
            out.push_back(hex);
        }
    }
    git_error_clear();
    return out;
}

Plan replayPlan(git_repository* repo, const std::vector<std::string>& changed)
{
    Plan plan;
    for (const auto& c : descendants(repo, changed)) {
        Step s;
        s.source = c;
        plan.steps.push_back(std::move(s));
    }
    return plan;
}

Plan insertPlan(git_repository* repo, const std::string& at, bool before, const std::string& message)
{
    std::string msg = message;
    if (!msg.empty() && msg.back() != '\n')
        msg.push_back('\n');
    Commit atCommit = lookupCommit(repo, *fromHex(at));
    std::vector<std::string> atParents;
    for (unsigned i = 0; i < git_commit_parentcount(atCommit.get()); ++i)
        atParents.push_back(toHex(*git_commit_parent_id(atCommit.get(), i)));
    Step inserted;
    inserted.kind = Step::Kind::Empty;
    inserted.key = "inserted";
    inserted.sourceParents = false;
    inserted.forceNew = true;
    inserted.message = msg;
    Plan plan;
    if (before) {
        for (const auto& p : atParents)
            inserted.parents.push_back("=" + p);
        plan = replayPlan(repo, {at});
        plan.steps.insert(plan.steps.begin(), inserted);
        for (auto& st : plan.steps)
            if (st.source == at) {
                st.sourceParents = false;
                st.parents = {"inserted"};
                for (size_t k = 1; k < atParents.size(); ++k)
                    st.parents.push_back(atParents[k]);
            }
        plan.reflogMessage = "new commit before " + at.substr(0, 10);
        return plan;
    }
    inserted.parents = {"=" + at};
    for (const auto& c : descendants(repo, {at})) {
        if (c == at)
            continue;
        Step st;
        st.source = c;
        plan.steps.push_back(std::move(st));
    }
    plan.steps.insert(plan.steps.begin(), inserted);
    plan.parentRedirect[at] = "inserted";
    plan.reflogMessage = "new commit after " + at.substr(0, 10);
    if (plan.steps.size() == 1) {
        // Nothing after it: the branches (and a detached HEAD) at the commit advance.
        forEachReference(repo, [&](git_reference* ref) {
            const std::string name = git_reference_name(ref);
            if (name.rfind("refs/heads/", 0) == 0 && git_reference_type(ref) == GIT_REFERENCE_DIRECT
                && toHex(*git_reference_target(ref)) == at)
                plan.refsToSteps[name] = "inserted";
            return true;
        });
        git_oid head;
        if (git_repository_head_detached(repo) == 1 && git_reference_name_to_id(&head, repo, "HEAD") == 0 && toHex(head) == at)
            plan.detachHeadAt = "inserted";
        git_error_clear();
    }
    return plan;
}

std::string applyPatchToTree(git_repository* repo, const std::string& tree, const std::string& patch)
{
    git_diff* rawDiff = nullptr;
    git_diff_parse_options parse = GIT_DIFF_PARSE_OPTIONS_INIT;
    parse.oid_type = oidType(repo);
    check(git_diff_from_buffer(&rawDiff, patch.data(), patch.size(), &parse), "git_diff_from_buffer");
    Diff diff(rawDiff);
    Tree base = lookupTree(repo, *fromHex(tree));
    git_index* rawIndex = nullptr;
    git_apply_options opts = GIT_APPLY_OPTIONS_INIT;
    check(git_apply_to_tree(&rawIndex, repo, base.get(), diff.get(), &opts), "git_apply_to_tree");
    Index index(rawIndex);
    git_oid out;
    check(git_index_write_tree_to(&out, index.get(), repo), "git_index_write_tree_to");
    return toHex(out);
}

std::string reversePatch(const std::string& patch)
{
    std::istringstream in(patch);
    std::string out, line, minus;
    while (std::getline(in, line)) {
        const bool cr = !line.empty() && line.back() == '\r';
        if (cr)
            line.pop_back();
        std::string rewritten = line;
        // Names swap sides, and with them their a/ and b/ prefixes.
        auto swapPrefix = [](std::string name) {
            if (name.rfind("a/", 0) == 0)
                name[0] = 'b';
            else if (name.rfind("b/", 0) == 0)
                name[0] = 'a';
            return name;
        };
        if (line.rfind("diff --git ", 0) == 0) {
            const std::string names = line.substr(11);
            const auto sep = names.find(" b/");
            if (sep != std::string::npos)
                rewritten = "diff --git a/" + names.substr(sep + 3) + " b/" + names.substr(2, sep - 2);
            out += rewritten + (cr ? "\r\n" : "\n");
            continue;
        }
        if (line.rfind("--- ", 0) == 0) {
            minus = line.substr(4); // held until the "+++" line
            continue;
        }
        if (line.rfind("+++ ", 0) == 0) {
            out += "--- " + swapPrefix(line.substr(4)) + (cr ? "\r\n" : "\n");
            out += "+++ " + swapPrefix(minus) + (cr ? "\r\n" : "\n");
            continue;
        }
        if (line.rfind("new file mode ", 0) == 0)
            rewritten = "deleted file mode " + line.substr(14);
        else if (line.rfind("deleted file mode ", 0) == 0)
            rewritten = "new file mode " + line.substr(18);
        else if (line.rfind("old mode ", 0) == 0)
            rewritten = "new mode " + line.substr(9);
        else if (line.rfind("new mode ", 0) == 0)
            rewritten = "old mode " + line.substr(9);
        else if (line.rfind("index ", 0) == 0) {
            const auto dots = line.find("..");
            const auto space = line.find(' ', dots == std::string::npos ? 6 : dots);
            if (dots != std::string::npos)
                rewritten = "index " + line.substr(dots + 2, (space == std::string::npos ? line.size() : space) - dots - 2) + ".."
                    + line.substr(6, dots - 6) + (space == std::string::npos ? "" : line.substr(space));
        } else if (line.rfind("@@ ", 0) == 0) {
            // @@ -a,b +c,d @@ tail
            const auto minusPos = line.find(" -", 2);
            const auto plusPos = line.find(" +", minusPos + 2);
            const auto end = line.find(" @@", plusPos + 2);
            if (minusPos != std::string::npos && plusPos != std::string::npos && end != std::string::npos) {
                const std::string oldRange = line.substr(minusPos + 2, plusPos - minusPos - 2);
                const std::string newRange = line.substr(plusPos + 2, end - plusPos - 2);
                rewritten = "@@ -" + newRange + " +" + oldRange + line.substr(end);
            }
        } else if (!line.empty() && line[0] == '+') {
            rewritten[0] = '-';
        } else if (!line.empty() && line[0] == '-') {
            rewritten[0] = '+';
        }
        out += rewritten + (cr ? "\r\n" : "\n");
    }
    return out;
}

} // namespace gg::rewrite
