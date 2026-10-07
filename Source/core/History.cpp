#include "History.hpp"

#include "Readers.hpp"

#include <libgg/Keep.hpp>
#include <libgg/Thread.hpp>

#include <spdlog/spdlog.h>

#include <algorithm>
#include <chrono>
#include <cstring>

namespace ggui::core {

using namespace gg::git2;

namespace {

constexpr int kFirstBatch = 200;
constexpr int kBatch = 2000;

std::string lower(std::string s)
{
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

void addBadge(HistoryState& st, const Oid& id, RefKind kind, std::string name, bool current = false)
{
    if (id.isNull())
        return;
    st.badges[id].push_back(RefBadge{kind, std::move(name), current});
}

void buildBadges(HistoryState& st)
{
    const Snapshot& s = *st.snapshot;
    if (s.headDetached && !s.head.isNull())
        addBadge(st, s.head, RefKind::Head, "HEAD");
    for (const auto& b : s.branches)
        addBadge(st, b.target, RefKind::LocalBranch, b.name, b.isHead);
    for (const auto& r : s.remoteBranches)
        addBadge(st, r.target, RefKind::RemoteBranch, r.name);
    for (const auto& t : s.tags)
        addBadge(st, t.target, RefKind::Tag, t.name);
    // When the detached HEAD's own commit is kept, its Head badge says so already.
    for (const auto& k : s.kept)
        if (!(s.headDetached && k.id == s.head))
            addBadge(st, k.id, RefKind::Keep, k.id.hex());
    for (const auto& w : s.worktrees)
        if (!w.isCurrent && !w.bare && !w.head.isNull())
            addBadge(st, w.head, RefKind::Worktree, w.name);
    for (const auto& z : s.stashes)
        addBadge(st, z.base, RefKind::Stash, "stash@{" + std::to_string(z.index) + "}");
}

bool refVisible(const HistoryScope& scope, const std::string& fullName)
{
    if (scope.allRefs)
        return true;
    return std::find(scope.refs.begin(), scope.refs.end(), fullName) != scope.refs.end();
}

int allocLane(HistoryState& st)
{
    for (size_t i = 0; i < st.lanes.size(); ++i)
        if (!st.lanes[i]) {
            st.laneColors[i] = st.nextColor++;
            return static_cast<int>(i);
        }
    st.lanes.emplace_back();
    st.laneColors.push_back(st.nextColor++);
    return static_cast<int>(st.lanes.size() - 1);
}

void enqueue(git_repository* repo, HistoryState& st, const Oid& id)
{
    if (!st.queued.insert(id).second)
        return;
    const git_oid g = toGit(id);
    git_commit* c = nullptr;
    if (git_commit_lookup(&c, repo, &g) != 0) {
        git_error_clear(); // missing (shallow clone) or not a commit
        return;
    }
    st.queue.push({git_commit_time(c), st.queueOrder++, id});
    git_commit_free(c);
}

// Walks until `stop` returns true, the walk ends or `limit` rows are emitted.
void walk(git_repository* repo, HistoryState& st, int limit, const gg::CancelToken& cancel, const HistoryEmit& emit,
    bool firstIsReset, const std::function<bool(const Oid&)>& stop)
{
    auto batch = std::make_shared<HistoryBatch>();
    batch->query = st.query;
    batch->reset = firstIsReset;
    int batchLimit = firstIsReset ? kFirstBatch : kBatch;
    auto flush = [&](bool final) {
        batch->complete = st.complete;
        batch->truncated = !st.complete && st.emitted >= limit;
        batch->maxLanes = st.maxLanes;
        for (const Oid& m : st.countsChanged)
            batch->collapsedCounts.emplace_back(m, st.collapsedCount[m]);
        st.countsChanged.clear();
        if (!batch->rows.empty() || batch->reset || final || !batch->collapsedCounts.empty())
            emit(batch);
        batch = std::make_shared<HistoryBatch>();
        batch->query = st.query;
        batchLimit = kBatch;
    };

    while (!st.complete && st.emitted < limit) {
        if ((st.emitted & 63) == 0)
            gg::throwIfCancelled(cancel);
        if (st.queue.empty()) {
            st.complete = true;
            break;
        }
        const git_oid oid = toGit(st.queue.top().id);
        st.queue.pop();
        const Oid id = toOid(oid);
        const bool inScope = st.scopeTips.count(id) || st.inScope.count(id);
        const bool published = st.remoteTips.count(id) || st.published.count(id);
        Commit commit = lookupCommit(repo, oid);
        const unsigned parentCount = git_commit_parentcount(commit.get());
        std::vector<Oid> parents;
        parents.reserve(parentCount);
        for (unsigned i = 0; i < parentCount; ++i) {
            parents.push_back(toOid(*git_commit_parent_id(commit.get(), i)));
            enqueue(repo, st, parents.back());
        }
        for (const auto& p : parents) {
            if (inScope)
                st.inScope.insert(p);
            if (published)
                st.published.insert(p);
        }
        st.inScope.erase(id);
        st.published.erase(id);
        if (!inScope)
            continue;

        // Shown when a ref points at it or a shown child leads to it; otherwise it lies on the side
        // of a collapsed merge. Walking in time order, children come first.
        const bool visible = st.scopeTips.count(id) || st.reach.count(id);
        st.reach.erase(id);
        if (!visible) {
            auto owner = st.hiddenOwner.find(id);
            if (owner != st.hiddenOwner.end()) {
                const Oid merge = owner->second;
                st.hiddenOwner.erase(owner);
                st.hiddenBy[id] = merge;
                ++st.collapsedCount[merge];
                st.countsChanged.insert(merge);
                for (const auto& p : parents)
                    if (!st.reach.count(p))
                        st.hiddenOwner.emplace(p, merge);
            }
            // Continue any lane waiting for it through its first parent.
            for (auto& lane : st.lanes)
                if (lane && *lane == id)
                    lane = parents.empty() ? std::nullopt : std::optional<Oid>(parents[0]);
            // A commit being revealed that a collapsed merge hides gets no row: the caller expands the merge.
            if (stop && stop(id))
                break;
            continue;
        }
        st.hiddenOwner.erase(id);

        HistoryRow row;
        row.id = id;
        row.parents = parents;
        const char* summary = git_commit_summary(commit.get());
        row.subject = summary ? summary : "";
        const git_signature* author = git_commit_author(commit.get());
        row.author = author->name;
        row.authorEmail = author->email;
        row.time = author->when.time;
        row.published = published;
        row.shortId = id.shortHex(kShortIdLength);
        if (auto it = st.badges.find(id); it != st.badges.end())
            row.refs = it->second;
        if (parents.size() > 1) {
            // Collapsing hides nothing when every merged-in parent is shown anyway (a ref points at
            // it, a shown child already leads to it, or it is shown): such a merge stays expanded.
            for (size_t i = 1; i < parents.size(); ++i)
                if (!st.scopeTips.count(parents[i]) && !st.reach.count(parents[i]) && !st.rowOf.count(parents[i]))
                    row.collapsible = true;
            const bool toggled = std::find(st.scope.toggledMerges.begin(), st.scope.toggledMerges.end(), id)
                != st.scope.toggledMerges.end();
            row.collapsed = row.collapsible && st.scope.mergesCollapsed != toggled;
        }
        for (size_t i = 0; i < parents.size(); ++i) {
            if (i == 0 || !row.collapsed)
                st.reach.insert(parents[i]);
            else if (!st.reach.count(parents[i]))
                st.hiddenOwner.emplace(parents[i], id);
        }

        // ---- lane layout ------------------------------------------------------------------
        std::vector<Oid> layoutParents = parents;
        if (row.collapsed && !layoutParents.empty())
            layoutParents.resize(1);
        // A parent already shown (clock skew in the time-ordered walk) gets no edge.
        layoutParents.erase(std::remove_if(layoutParents.begin(), layoutParents.end(),
                                [&](const Oid& p) { return st.rowOf.count(p) != 0; }),
            layoutParents.end());

        int lane = -1;
        for (size_t i = 0; i < st.lanes.size(); ++i)
            if (st.lanes[i] && *st.lanes[i] == id) {
                lane = static_cast<int>(i);
                break;
            }
        const bool expected = lane >= 0;
        if (!expected)
            lane = allocLane(st);
        row.lane = lane;
        row.color = st.laneColors[static_cast<size_t>(lane)];
        if (expected)
            row.lines.push_back(GraphLine{static_cast<std::int16_t>(lane), static_cast<std::int16_t>(lane), 0, 1, row.color});
        // Other lanes converging into this commit; pass-through for the rest.
        std::vector<bool> active(st.lanes.size(), false);
        for (size_t k = 0; k < st.lanes.size(); ++k) {
            if (static_cast<int>(k) == lane || !st.lanes[k])
                continue;
            if (*st.lanes[k] == id) {
                row.lines.push_back(GraphLine{static_cast<std::int16_t>(k), static_cast<std::int16_t>(lane), 0, 1,
                    st.laneColors[k]});
                st.lanes[k].reset();
            } else {
                active[k] = true;
            }
        }
        st.lanes[static_cast<size_t>(lane)].reset();
        // Parents
        for (size_t pi = 0; pi < layoutParents.size(); ++pi) {
            const Oid& p = layoutParents[pi];
            int target = -1;
            for (size_t k = 0; k < st.lanes.size(); ++k)
                if (st.lanes[k] && *st.lanes[k] == p) {
                    target = static_cast<int>(k);
                    break;
                }
            if (target > lane && pi == 0 && !st.lanes[static_cast<size_t>(lane)]) {
                // The first parent is already awaited further right: keep the first-parent chain in
                // this column too. Both lanes run down to the parent, whose row takes the leftmost
                // one; the other converges into it there.
                st.lanes[static_cast<size_t>(lane)] = p;
                target = lane;
            } else if (target < 0) {
                if (pi == 0 && !st.lanes[static_cast<size_t>(lane)]) {
                    target = lane;
                } else {
                    target = allocLane(st);
                    if (static_cast<size_t>(target) >= active.size())
                        active.resize(static_cast<size_t>(target) + 1, false);
                }
                st.lanes[static_cast<size_t>(target)] = p;
            }
            const std::uint8_t color = target == lane ? row.color : st.laneColors[static_cast<size_t>(target)];
            row.lines.push_back(GraphLine{static_cast<std::int16_t>(lane), static_cast<std::int16_t>(target), 1, 2, color});
        }
        for (size_t k = 0; k < active.size() && k < st.lanes.size(); ++k)
            if (active[k])
                row.lines.push_back(GraphLine{static_cast<std::int16_t>(k), static_cast<std::int16_t>(k), 0, 2,
                    st.laneColors[k]});
        while (!st.lanes.empty() && !st.lanes.back()) {
            st.lanes.pop_back();
            st.laneColors.pop_back();
        }
        st.maxLanes = std::max<int>(st.maxLanes, std::max<int>(lane + 1, static_cast<int>(st.lanes.size())));

        // ---- search index -----------------------------------------------------------------
        std::string text = id.hex() + " " + git_commit_message(commit.get()) + " " + row.author + " " + row.authorEmail;
        for (const auto& r : row.refs)
            text += " " + r.name;
        st.searchText.push_back(lower(std::move(text)));
        st.rowOf[id] = static_cast<int>(st.order.size());
        st.order.push_back(id);
        ++st.emitted;

        batch->rows.push_back(std::move(row));
        const bool hit = stop && stop(id);
        if (static_cast<int>(batch->rows.size()) >= batchLimit)
            flush(false);
        if (hit)
            break;
    }
    flush(true);
}

} // namespace

void historyStart(git_repository* repo, HistoryState& st, std::uint64_t query, const HistoryScope& scope,
    SnapshotPtr snapshot, int limit, const gg::CancelToken& cancel, const HistoryEmit& emit)
{
    gg::assertNotUiThread("historyStart");
    st = HistoryState{};
    st.query = query;
    st.scope = scope;
    st.snapshot = std::move(snapshot);
    st.limit = limit;
    buildBadges(st);

    st.started = true;
    auto push = [&](const Oid& id, bool inScope, bool remote) {
        if (id.isNull())
            return;
        git_oid g = toGit(id);
        git_object* obj = nullptr;
        if (git_object_lookup(&obj, repo, &g, GIT_OBJECT_COMMIT) != 0) {
            git_error_clear();
            return; // not a commit (e.g. a tag on a tree)
        }
        git_object_free(obj);
        enqueue(repo, st, id);
        if (inScope)
            st.scopeTips.insert(id);
        if (remote)
            st.remoteTips.insert(id);
    };
    const Snapshot& s = *st.snapshot;
    if (!s.head.isNull() && (scope.allRefs || refVisible(scope, "HEAD"))) {
        push(s.head, true, false);
        // The Working tree row above the history hangs on lane 0 and leads down to HEAD.
        if (!s.bare) {
            st.lanes.emplace_back(s.head);
            st.laneColors.push_back(st.nextColor++);
        }
    }
    for (const auto& b : s.branches)
        push(b.target, refVisible(scope, "refs/heads/" + b.name), false);
    for (const auto& r : s.remoteBranches)
        push(r.target, refVisible(scope, "refs/remotes/" + r.name), true);
    for (const auto& t : s.tags)
        push(t.target, refVisible(scope, "refs/tags/" + t.name), false);
    for (const auto& k : s.kept)
        push(k.id, refVisible(scope, gg::keep::refName(k.id.hex())), false);
    for (const auto& w : s.worktrees)
        if (!w.isCurrent && !w.head.isNull())
            push(w.head, scope.allRefs, false);

    walk(repo, st, limit, cancel, emit, true, {});
}

void historyContinue(git_repository* repo, HistoryState& st, int limit, const gg::CancelToken& cancel,
    const HistoryEmit& emit)
{
    gg::assertNotUiThread("historyContinue");
    if (!st.started)
        return;
    st.limit = limit;
    walk(repo, st, limit, cancel, emit, false, {});
}

HistoryRevealResult historyReveal(git_repository* repo, HistoryState& st, const Oid& id,
    const gg::CancelToken& cancel, const HistoryEmit& emit)
{
    gg::assertNotUiThread("historyReveal");
    HistoryRevealResult result;
    if (st.rowOf.count(id)) {
        result.found = true;
        return result;
    }
    // The walk already dropped it: it is behind us and a walk would run to the end of the history.
    if (auto hidden = st.hiddenBy.find(id); hidden != st.hiddenBy.end()) {
        result.hiddenBy = hidden->second;
        return result;
    }
    if (!st.started || st.complete)
        return result;
    walk(repo, st, std::numeric_limits<int>::max(), cancel, emit, false, [&](const Oid& o) { return o == id; });
    st.limit = std::max(st.limit, st.emitted);
    result.found = st.rowOf.count(id) != 0;
    if (auto hidden = st.hiddenBy.find(id); hidden != st.hiddenBy.end())
        result.hiddenBy = hidden->second;
    return result;
}

std::vector<Oid> historySearch(const HistoryState& st, const std::string& text, const gg::CancelToken& cancel)
{
    std::vector<Oid> matches;
    const std::string needle = lower(text);
    if (needle.empty())
        return matches;
    for (size_t i = 0; i < st.searchText.size(); ++i) {
        if ((i & 4095) == 0)
            gg::throwIfCancelled(cancel);
        if (st.searchText[i].find(needle) != std::string::npos)
            matches.push_back(st.order[i]);
    }
    return matches;
}

} // namespace ggui::core
