// Interactive rebase live preview (product spec §4.13 "Live preview", §3.1).
//
// The todo goes through the same plan (gg::todo::toPlan) and the same in-memory rewrite engine
// (gg::rewrite::Rewriter::compute) as Start, so the preview is the result Start would produce.
// The Rewriter works on its own repository instance whose object database has an in-memory
// backend (mempack) with the highest priority: every object the computation creates goes there
// and is dropped with the Rewriter. apply() is never called, so no ref, index, working tree or
// object file changes. The only on-disk effect libgit2 may have is refreshing the mtime of an
// object that already exists ("freshen"), as git itself does on every read-write access.
#include "Readers.hpp"

#include <libgg/Conflicts.hpp>
#include <libgg/Rewrite.hpp>

#include <algorithm>
#include <map>
#include <optional>
#include <set>

namespace ggui::core {

using namespace gg::git2;
namespace todo = gg::todo;
namespace rw = gg::rewrite;

namespace {

std::string shortRef(const std::string& ref) { return ref.rfind("refs/heads/", 0) == 0 ? ref.substr(11) : ref; }

std::string summaryOf(git_repository* repo, const std::string& id)
{
    Commit c = lookupCommit(repo, *fromHex(id));
    const char* s = git_commit_summary(c.get());
    return s ? s : "";
}

std::string treeOf(git_repository* repo, const std::string& id)
{
    Commit c = lookupCommit(repo, *fromHex(id));
    return toHex(*git_commit_tree_id(c.get()));
}

} // namespace

RebasePreviewPtr readRebasePreview(const std::filesystem::path& repoPath, const todo::Todo& list,
    const todo::Context& context, const todo::Options& options, const gg::CancelToken& cancel)
{
    auto out = std::make_shared<RebasePreview>();
    out->onto = context.onto;
    try {
        rw::Plan plan = todo::toPlan(list, context, true);
        plan.keepCommitterDate = options.keepCommitterDate;
        plan.emptied = options.emptied == rw::Emptied::Drop ? rw::Emptied::Drop : rw::Emptied::Keep;
        rw::Rewriter rewriter(repoPath);
        const rw::Result result = rewriter.compute(plan, cancel);
        if (!result.ok && result.unresolved.empty()) {
            out->error = result.error;
            return out;
        }
        git_repository* repo = rewriter.repository();
        if (!context.onto.empty())
            out->ontoSubject = summaryOf(repo, context.onto);
        std::set<std::string> dropped; // step keys of commits left out because they became empty
        for (const auto& e : result.becameEmpty)
            if (e.dropped) {
                dropped.insert(e.step);
                out->droppedEmpty.push_back(e.subject);
            }

        // One row per commit row that starts a commit; squash/fixup rows join the current one. A
        // squash/fixup that amends a finished commit (after an update-ref or label row) replaces
        // its row's commit; the finished one stays only for the branches pointing at it. Merge rows
        // (--rebase-merges) make a row unless Git leaves them out (nothing left to merge); label
        // and reset rows move what "current" is.
        std::map<size_t, size_t> rowOf; // todo row → preview row
        std::map<std::string, size_t> amended; // finished commit → the row that amends it
        std::optional<size_t> current;  // preview row HEAD is at (nullopt = the base or outside)
        std::map<std::string, std::optional<size_t>> labelRow;
        bool skipping = false; // followers of a commit that was left out
        for (size_t i = 0; i < list.items.size(); ++i) {
            const todo::Item& item = list.items[i];
            const std::string key = "row:" + std::to_string(i);
            switch (item.action) {
            case todo::Action::Pick:
            case todo::Action::Reword:
            case todo::Action::Edit: {
                skipping = dropped.count(key) > 0;
                if (skipping)
                    break;
                RebasePreview::Row row;
                row.todoRow = i;
                row.sources = {item.commit};
                row.id = result.steps.at(key);
                current = rowOf[i] = out->rows.size();
                out->rows.push_back(std::move(row));
                break;
            }
            case todo::Action::Squash:
            case todo::Action::Fixup:
                if (auto st = result.steps.find(key); st != result.steps.end()) { // amends
                    skipping = false;
                    if (!current) { // amends the base: a commit of its own
                        RebasePreview::Row row;
                        row.todoRow = i;
                        current = out->rows.size();
                        out->rows.push_back(std::move(row));
                    } else {
                        amended[out->rows[*current].id] = *current;
                    }
                    if (dropped.count(key)) {
                        out->rows.erase(out->rows.begin() + static_cast<std::ptrdiff_t>(*current));
                        current.reset();
                        skipping = true;
                        break;
                    }
                    out->rows[*current].id = st->second;
                    out->rows[*current].sources.push_back(item.commit);
                    rowOf[i] = *current;
                } else if (!skipping && current) { // validation keeps a squash from coming first
                    out->rows[*current].sources.push_back(item.commit);
                    rowOf[i] = *current;
                }
                break;
            case todo::Action::Label:
                labelRow[item.arg] = current;
                break;
            case todo::Action::Reset:
                if (auto it = labelRow.find(item.arg); it != labelRow.end())
                    current = it->second;
                else
                    current.reset();
                skipping = false;
                break;
            case todo::Action::Merge: {
                skipping = false;
                const std::string& id = result.steps.at(key);
                // Nothing to merge (every head already merged): no commit, HEAD stays.
                const bool skipped = current ? out->rows[*current].id == id
                                             : id == context.onto || git_commit_parentcount(lookupCommit(repo, *fromHex(id)).get()) < 2;
                if (skipped)
                    break;
                RebasePreview::Row row;
                row.todoRow = i;
                row.merge = true;
                if (!item.commit.empty() && item.fixup != todo::FixupMessage::None)
                    row.sources = {item.commit};
                row.id = id;
                current = rowOf[i] = out->rows.size();
                out->rows.push_back(std::move(row));
                break;
            }
            default:
                break;
            }
        }

        gg::conflicts::Cache cache; // memory only
        std::set<std::string> conflicted(result.conflicted.begin(), result.conflicted.end());
        std::map<std::string, size_t> byId;
        for (size_t k = 0; k < out->rows.size(); ++k) {
            gg::throwIfCancelled(cancel);
            RebasePreview::Row& row = out->rows[k];
            byId[row.id] = k;
            row.subject = summaryOf(repo, row.id);
            row.unchanged = row.sources.size() == 1 && row.id == row.sources.front();
            const std::string tree = treeOf(repo, row.id);
            row.tree = tree;
            {
                Commit c = lookupCommit(repo, *fromHex(row.id));
                for (unsigned p = 0; p < git_commit_parentcount(c.get()); ++p)
                    row.parents.push_back(toHex(*git_commit_parent_id(c.get(), p)));
            }
            if (row.parents.empty()) {
                Tree t = lookupTree(repo, *fromHex(tree));
                row.empty = git_tree_entrycount(t.get()) == 0;
            } else {
                row.empty = row.parents.size() == 1 && tree == treeOf(repo, row.parents.front());
            }
            row.wasEmpty = std::all_of(row.sources.begin(), row.sources.end(), [&](const std::string& id) {
                const auto it = context.commits.find(id);
                return it != context.commits.end() && it->second.empty;
            });
            for (const auto& f : gg::conflicts::commitConflicts(repo, *fromHex(row.id), cache, cancel))
                row.conflicts.emplace_back(f.path, f.sides);
            row.newConflicts = conflicted.count(row.id) > 0;
            if (!row.unchanged) {
                std::set<std::string> before;
                for (const auto& src : row.sources)
                    for (const auto& f : gg::conflicts::commitConflicts(repo, *fromHex(src), cache, cancel))
                        before.insert(f.path);
                for (const auto& [path, sides] : row.conflicts)
                    before.erase(path);
                row.resolved.assign(before.begin(), before.end());
            }
        }
        for (const auto& c : result.unresolved) {
            const size_t todoRow = std::stoul(c.step.substr(c.step.find(':') + 1));
            if (auto it = rowOf.find(todoRow); it != rowOf.end())
                out->rows[it->second].decisions.push_back({c.path, c.kind});
        }

        // Where the tip, the update-ref branches and a detached HEAD end up.
        auto place = [&](const std::string& name, const std::string& stepKey) {
            std::string target;
            if (stepKey.rfind("=", 0) == 0)
                target = stepKey.substr(1);
            else if (auto it = result.steps.find(stepKey); it != result.steps.end())
                target = it->second;
            if (auto it = byId.find(target); it != byId.end())
                out->rows[it->second].branches.push_back(name);
            else if (!target.empty() && target == context.onto)
                out->ontoBranches.push_back(name);
            else if (auto am = amended.find(target); am != amended.end() && am->second < out->rows.size())
                out->aside.push_back({name, target, treeOf(repo, target), summaryOf(repo, target), am->second});
        };
        for (const auto& [ref, stepKey] : plan.refsToSteps)
            place(shortRef(ref), stepKey);
        if (!plan.detachHeadAt.empty())
            place("HEAD", plan.detachHeadAt);
        for (const auto& mv : result.moves)
            out->moves.push_back({shortRef(mv.ref), mv.oldId, mv.newId});

        // Branches in the range that do not move stay on the old commits (Git without
        // --update-refs), unless their commit is kept as it is.
        std::set<std::string> staying;
        std::vector<std::string> inRange = context.range;
        if (todo::hasMergeRows(list))
            inRange.insert(inRange.end(), context.merges.begin(), context.merges.end());
        for (const auto& id : inRange)
            if (auto it = context.branchesAt.find(id); it != context.branchesAt.end() && !byId.count(id))
                for (const auto& ref : it->second)
                    if (!plan.refsToSteps.count(ref))
                        staying.insert(shortRef(ref));
        out->staying.assign(staying.begin(), staying.end());
        out->ok = true;
    } catch (const todo::NoPreview& e) {
        out->rows.clear();
        out->error = e.what();
        out->unsupported = true;
    } catch (const std::exception& e) {
        out->rows.clear();
        out->error = e.what();
    }
    return out;
}

} // namespace ggui::core
