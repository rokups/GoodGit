// History walk and lane layout, run on the history worker (product spec §3.1, §4.2).
//
// The walk is topological (children before parents), published in bounded batches, and keeps
// its state between requests so "Show more", reveal and search continue where it stopped.
#pragma once

#include "core/Engine.hpp"
#include "core/Types.hpp"

#include <libgg/Cancel.hpp>
#include <libgg/Git2.hpp>

#include <functional>
#include <optional>
#include <unordered_map>
#include <queue>
#include <unordered_set>

namespace ggui::core {

struct HistoryState {
    std::uint64_t query = 0;
    HistoryScope scope;
    SnapshotPtr snapshot;
    // Incremental date-ordered walk (newest committer time first). libgit2's sorted revwalk
    // walks the whole history before returning the first commit; this one does not.
    struct Pending {
        std::int64_t time;
        std::uint64_t order; // FIFO among equal times
        Oid id;
        bool operator<(const Pending& o) const { return time != o.time ? time < o.time : order > o.order; }
    };
    std::priority_queue<Pending> queue;
    std::unordered_set<Oid, OidHash> queued;
    std::uint64_t queueOrder = 0;
    bool started = false;
    bool complete = false;
    int limit = 0;
    int emitted = 0;
    int maxLanes = 0;

    std::unordered_set<Oid, OidHash> scopeTips;
    std::unordered_set<Oid, OidHash> remoteTips;
    std::unordered_set<Oid, OidHash> inScope;     // propagated from children
    std::unordered_set<Oid, OidHash> published;   // propagated from children
    std::unordered_set<Oid, OidHash> reach;       // shown through a visible child (edge not collapsed)
    std::unordered_map<Oid, Oid, OidHash> hiddenOwner; // pending hidden commit → collapsed merge hiding it
    std::unordered_map<Oid, Oid, OidHash> hiddenBy; // commit dropped by a collapsed merge → that merge
    std::unordered_map<Oid, int, OidHash> collapsedCount;
    std::unordered_set<Oid, OidHash> countsChanged;   // merges whose count changed since the last batch
    std::unordered_map<Oid, std::vector<RefBadge>, OidHash> badges;

    std::vector<std::optional<Oid>> lanes;
    std::vector<std::uint8_t> laneColors;
    std::uint8_t nextColor = 0;

    // Emitted rows (ids and searchable text) for reveal and search.
    std::vector<Oid> order;
    std::unordered_map<Oid, int, OidHash> rowOf;
    std::vector<std::string> searchText; // lowercase "id subject body author refs"
};

using HistoryEmit = std::function<void(std::shared_ptr<HistoryBatch>)>;

// Starts a new walk (replaces `state`) and publishes the first batches up to `limit`.
void historyStart(git_repository* repo, HistoryState& state, std::uint64_t query, const HistoryScope& scope,
    SnapshotPtr snapshot, int limit, const gg::CancelToken& cancel, const HistoryEmit& emit);

// Continues the current walk until `limit` rows (or the end).
void historyContinue(git_repository* repo, HistoryState& state, int limit, const gg::CancelToken& cancel,
    const HistoryEmit& emit);

struct HistoryRevealResult {
    bool found = false;
    Oid hiddenBy; // when not found: the collapsed merge that hides the commit (null: none)
};

// Continues until `id` is emitted or dropped by a collapsed merge. Returns whether it was found.
HistoryRevealResult historyReveal(git_repository* repo, HistoryState& state, const Oid& id, const gg::CancelToken& cancel,
    const HistoryEmit& emit);

// Ids of emitted rows matching `text` (case-insensitive: id, message, author, ref names).
std::vector<Oid> historySearch(const HistoryState& state, const std::string& text, const gg::CancelToken& cancel);

} // namespace ggui::core
