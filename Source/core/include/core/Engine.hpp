// The repository engine: commands in, immutable snapshots and events out (product spec §3, §3.1).
//
// The UI thread only calls the methods of this class; they enqueue work and return at once.
// Work runs on worker threads, each with its own git_repository*:
//   1. mutations        serialized, one at a time
//   2. snapshot/status
//   3. history/graph    incremental, cancellable
//   4. diff/blame/file  latest request per slot wins, stale requests dropped
//   5. network          git processes
//   6. preview          interactive rebase preview, latest request wins
// Results come back through poll() as events holding plain C++ values.
#pragma once

#include "core/Types.hpp"

#include <libgg/Cancel.hpp>
#include <libgg/GitRunner.hpp>
#include <libgg/Hooks.hpp>
#include <libgg/Journal.hpp>
#include <libgg/Todo.hpp>

#include <atomic>
#include <chrono>
#include <deque>
#include <functional>
#include <map>
#include <optional>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <variant>
#include <vector>

struct git_repository;

namespace ggui::core {

using RequestId = std::uint64_t;

// ---- Events ------------------------------------------------------------------------------------

struct OpenedEvent {
    RequestId request = 0;
    SnapshotPtr snapshot;
};
struct SnapshotEvent {
    RequestId request = 0;
    SnapshotPtr snapshot;
};
struct StatusEvent {
    RequestId request = 0;
    StatusPtr status;
};
struct HistoryEvent {
    RequestId request = 0;
    std::shared_ptr<HistoryBatch> batch; // moved into the UI model
};
struct RevealEvent {
    RequestId request = 0;
    Oid id;
    bool found = false;
};
struct SearchEvent {
    RequestId request = 0;
    std::string text;
    std::vector<Oid> matches;           // in history order
    bool complete = true;
};
struct DiffEvent {
    RequestId request = 0;
    int slot = 0;
    DiffPtr diff;
};
struct BlameEvent {
    RequestId request = 0;
    BlamePtr blame;
};
struct ReflogEvent {
    RequestId request = 0;
    ReflogPtr reflog;
};
struct CommitDetailsEvent {
    RequestId request = 0;
    CommitDetailsPtr details;
};
// The full messages of the requested commits, in request order.
struct CommitMessagesEvent {
    RequestId request = 0;
    std::vector<std::string> messages;
};
struct ErrorEvent {
    RequestId request = 0;
    std::string title;                  // e.g. "Open failed"
    std::string message;                // git / libgit2 text, verbatim
};
struct TaskFinishedEvent {
    RequestId request = 0;
    bool cancelled = false;
    bool failed = false;
};
struct WatchEvent {
    bool worktree = false;
    bool index = false;
    bool refs = false;
    bool journal = false;
};

// Result classes the UI reacts to (offers "Stash and switch", "Pull then push", …).
enum class Outcome {
    Ok,
    Failed,          // git (or a hook) failed; message has git's output verbatim
    Cancelled,
    LocalChanges,    // local changes would be overwritten (checkout, pull, stash pop, …)
    PushRejected,    // non-fast-forward
    Refused,         // ggui refused before running git (e.g. conflicted commits in a push)
};

struct MutationFinishedEvent {
    RequestId request = 0;
    std::string label;
    Outcome outcome = Outcome::Ok;
    std::string message;             // error text (first lines of git's stderr) or info
    std::string detail;              // full output
    std::string operation;           // journal operation id
    std::string journalError;        // the undo journal could not record it (busy or unwritable)
    std::string result;              // action-specific result (e.g. the new commit id)
};

struct OperationsEvent {
    RequestId request = 0;
    std::vector<gg::journal::Operation> operations; // journal order
    size_t skipped = 0;
    std::string error;
    bool hooksInstalled = false;
};

struct CommitConflicts {
    Oid commit;
    std::vector<std::pair<std::string, int>> files; // path, sides
};
struct ConflictsEvent {
    RequestId request = 0;
    std::vector<CommitConflicts> commits; // only commits with conflicts are listed
    std::vector<Oid> scanned;
};

// The tags on one remote (git ls-remote --tags), or why they could not be read.
struct RemoteTagsEvent {
    RequestId request = 0;
    std::string remote;
    bool ok = false;
    std::vector<std::string> tags; // names without refs/tags/
    std::string error;
};

struct ConfigEvent {
    RequestId request = 0;
    // key → value per scope ("user", "repository", "worktree", "effective").
    std::map<std::string, std::map<std::string, std::string>> values;
};

struct HooksEvent {
    RequestId request = 0;
    gg::hooks::Status status;
};

struct RebasePreviewEvent {
    RequestId request = 0;
    RebasePreviewPtr preview;
};

using Event = std::variant<OpenedEvent, SnapshotEvent, StatusEvent, HistoryEvent, RevealEvent, SearchEvent,
    DiffEvent, BlameEvent, ReflogEvent, CommitDetailsEvent, CommitMessagesEvent, ErrorEvent, TaskFinishedEvent, WatchEvent,
    MutationFinishedEvent, OperationsEvent, ConflictsEvent, ConfigEvent, HooksEvent, RebasePreviewEvent,
    RemoteTagsEvent>;

class Engine;

// What a mutation job can do (runs on a worker thread).
class MutationContext {
public:
    git_repository* repo() const { return m_repo; }
    const std::filesystem::path& cwd() const { return m_cwd; }
    const gg::CancelToken& token() const { return m_token; }
    // Runs git; throws on failure (the mutation then finishes with its output).
    gg::RunResult git(std::vector<std::string> args, std::string input = {}, bool progress = false);
    gg::RunResult gitMayFail(std::vector<std::string> args, std::string input = {}, bool progress = false);
    void progress(int percent, const std::string& label);
    // Extra environment for every git step of this mutation.
    std::vector<std::pair<std::string, std::optional<std::string>>> env;
    // The action's result shown to / used by the UI (e.g. a new commit id).
    std::string result;
    std::string info;
    // Set when the step changed the working tree along with the index (checkout, stash, …).
    bool worktreeFollowsIndex = false;
    // Linked worktrees the mutation added, removed, locked or unlocked (journaled so Undo can
    // do the opposite).
    std::vector<gg::journal::WorktreeChange> worktrees;

private:
    friend class Engine;
    MutationContext(Engine& e, RequestId id, git_repository* repo, std::filesystem::path cwd, const gg::CancelToken& t)
        : m_engine(e), m_id(id), m_repo(repo), m_cwd(std::move(cwd)), m_token(t) { }
    Engine& m_engine;
    RequestId m_id;
    git_repository* m_repo;
    std::filesystem::path m_cwd;
    const gg::CancelToken& m_token;
};

// Thrown by MutationContext::git on failure; also usable by jobs to refuse with a message.
struct MutationError {
    Outcome outcome = Outcome::Failed;
    std::string message;
    std::string detail;
};

struct MutationSpec {
    std::string label;
    bool network = false;            // runs on the network queue
    bool journal = true;             // record in the undo journal
    bool captureIndex = true;
    bool refreshAfter = true;
    std::function<void(MutationContext&)> run;
};

// Classifies git's error output.
Outcome classifyFailure(const std::string& stderrText);

struct Activity {
    RequestId id = 0;
    std::string label;
    int percent = -1;
    bool cancellable = true;
    std::chrono::steady_clock::time_point started;
};

enum class Queue { Mutation = 0, Snapshot = 1, History = 2, Content = 3, Network = 4, Preview = 5, Remote = 6 };
constexpr int kQueueCount = 7;

class Worker;
class Watcher;
struct HistoryState;

class Engine {
public:
    struct Options {
        std::filesystem::path path;
        bool watch = true;
    };

    explicit Engine(Options options);
    ~Engine();
    Engine(const Engine&) = delete;
    Engine& operator=(const Engine&) = delete;

    // ---- read requests --------------------------------------------------------------------
    RequestId open();
    RequestId refresh(bool withStatus = true);
    RequestId refreshStatus();
    RequestId loadHistory(const HistoryScope& scope, int limit, SnapshotPtr snapshot);
    RequestId showMoreHistory(int additional);
    RequestId revealCommit(const Oid& id);
    RequestId searchHistory(const std::string& text);
    RequestId diff(const DiffQuery& query, int slot);
    RequestId blame(const BlameQuery& query);
    RequestId reflog(const std::string& ref);
    RequestId commitDetails(const Oid& id);
    // The full messages of `ids` (CommitMessagesEvent, in the same order).
    RequestId commitMessages(const std::vector<Oid>& ids);

    // ---- mutations ------------------------------------------------------------------------
    RequestId mutate(MutationSpec spec);
    // Label of the running mutation ("" when none): conflicting actions are disabled meanwhile.
    std::string busyLabel() const;
    RequestId readOperations();
    RequestId scanConflicts(std::vector<Oid> commits);
    RequestId readConfig(std::vector<std::string> keys);
    // The tags on each remote, one RemoteTagsEvent per remote. Never asks for credentials (a remote
    // that needs them reports an error); on its own queue so a slow remote holds up nothing else.
    RequestId readRemoteTags(std::vector<std::string> remotes);
    RequestId readHooksStatus();
    // The result of an interactive rebase todo, computed in memory (RebasePreviewEvent; nothing
    // is written). A newer request cancels the older one.
    RequestId rebasePreview(gg::todo::Todo todo, std::shared_ptr<const gg::todo::Context> context,
        gg::todo::Options options);

    void cancel(RequestId id);
    void cancelAll();

    // Up to `maxEvents` events, oldest first.
    std::vector<Event> poll(size_t maxEvents);
    // No queued or running work and no undelivered events.
    bool idle() const;
    std::vector<Activity> activities() const;

    const std::filesystem::path& path() const { return m_options.path; }

    // ---- used by workers ------------------------------------------------------------------
    void emit(Event event);
    void setProgress(RequestId id, int percent, const std::string& label = {});

private:
    struct Job;
    RequestId submit(Queue queue, std::string label, int slot, bool cancellable,
        std::function<void(Job&)> fn);
    void finish(RequestId id, bool cancelled, bool failed);

    Options m_options;
    std::atomic<RequestId> m_nextId{1};
    std::unique_ptr<Worker> m_workers[kQueueCount];
    std::unique_ptr<Watcher> m_watcher;
    std::shared_ptr<HistoryState> m_history; // owned by the history worker thread
    std::atomic<std::uint64_t> m_generation{0};

    mutable std::mutex m_mutex;
    std::deque<Event> m_events;
    std::vector<Activity> m_activities;
    std::vector<std::pair<RequestId, gg::CancelToken>> m_tokens;

    std::string m_busy;
    std::shared_ptr<void> m_statusConflicts;  // gg::conflicts::Cache, snapshot worker only
    std::shared_ptr<void> m_historyConflicts; // gg::conflicts::Cache, history worker only

    friend class Worker;
    friend class MutationContext;
};

// Summaries for the recent-repository list, off the UI thread (one shared background worker).
class SummaryService {
public:
    SummaryService();
    ~SummaryService();
    void request(const std::vector<std::filesystem::path>& paths);
    std::vector<RepoSummary> poll();
    bool idle() const;

private:
    std::unique_ptr<Worker> m_worker;
    mutable std::mutex m_mutex;
    std::vector<RepoSummary> m_results;
    std::atomic<int> m_pending{0};
};

} // namespace ggui::core
