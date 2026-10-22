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
#include <libgg/Outgoing.hpp>
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
    Oid hiddenBy; // when not found: the collapsed merge that hides the commit (null: none)
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
    std::string warning;             // Ok, but something minor failed (e.g. the submodule update): a Warning notification
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

// The staged files a commit would make (or leave) conflicted (gg::outgoing::stagedConflictWarnings).
struct CommitWarningsEvent {
    RequestId request = 0;
    std::vector<gg::outgoing::StagedWarning> warnings;
};

// Whether a local branch's tip is merged into a target (git branch -d's check). Failed: git could not
// tell (an error, not "no").
struct BranchMergedEvent {
    enum class Result { Merged, NotMerged, Failed };
    RequestId request = 0;
    Result result = Result::Failed;
};

struct RebasePreviewEvent {
    RequestId request = 0;
    RebasePreviewPtr preview;
};

using Event = std::variant<OpenedEvent, SnapshotEvent, StatusEvent, HistoryEvent, RevealEvent, SearchEvent,
    DiffEvent, BlameEvent, ReflogEvent, CommitDetailsEvent, CommitMessagesEvent, ErrorEvent, TaskFinishedEvent, WatchEvent,
    MutationFinishedEvent, OperationsEvent, ConflictsEvent, ConfigEvent, RebasePreviewEvent,
    RemoteTagsEvent, CommitWarningsEvent, BranchMergedEvent>;

class Engine;

// How the engine updates the submodules after a mutation body succeeded (see MutationContext::submodules).
enum class SubmoduleSync {
    None,       // nothing
    Update,     // `git submodule update` to the commits the index records
    ForceUpdate // the same, discarding changes in the submodule working trees
};

// What a mutation job can do (runs on a worker thread).
class MutationContext {
public:
    git_repository* repo() const { return m_repo; }
    const std::filesystem::path& cwd() const { return m_cwd; }
    const gg::CancelToken& token() const { return m_rollingBack ? gg::CancelToken::none() : m_token; }
    // Runs git; throws on failure (the mutation then finishes with its output).
    gg::RunResult git(std::vector<std::string> args, std::string input = {}, bool progress = false);
    gg::RunResult gitMayFail(std::vector<std::string> args, std::string input = {}, bool progress = false);
    void progress(int percent, const std::string& label);
    // Extra environment for every git step of this mutation.
    std::vector<std::pair<std::string, std::optional<std::string>>> env;
    // The action's result shown to / used by the UI (e.g. a new commit id).
    std::string result;
    std::string info;
    // A minor failure after the work succeeded; the outcome stays Ok. Shown as a Warning notification.
    std::string warning;
    // Set when the step changed the working tree along with the index (checkout, stash, …).
    bool worktreeFollowsIndex = false;
    // Set by the body. When the body succeeds, the engine updates the submodules (not in a bare
    // repository, and only with a .gitmodules file). A failure or a cancel there is a text in
    // `warning`; the outcome stays Ok and nothing rolls back. MutationSpec::submodulesAutoUpdate
    // (off: no update) and submodulesAutoInit (also initializes new submodules) come from the settings.
    SubmoduleSync submodules = SubmoduleSync::None;
    // Commits the keep refs (libgg/Keep.hpp) must keep although nothing else anchors them: what a
    // rewrite put in place of the commits it replaced (rewrite::Result::keepExtra).
    std::vector<std::string> keepExtra;
    // Linked worktrees the mutation added, removed, locked or unlocked (journaled so Undo can
    // do the opposite).
    std::vector<gg::journal::WorktreeChange> worktrees;
    // Steps that put things back. They run in reverse order when the mutation fails or is
    // cancelled; a cancel does not stop their git commands. A step returns an error text, or an
    // empty string; the first error stops the other steps.
    std::vector<std::function<std::string(MutationContext&)>> rollback;

private:
    friend class Engine;
    MutationContext(Engine& e, RequestId id, git_repository* repo, std::filesystem::path cwd, const gg::CancelToken& t)
        : m_engine(e), m_id(id), m_repo(repo), m_cwd(std::move(cwd)), m_token(t) { }
    Engine& m_engine;
    RequestId m_id;
    git_repository* m_repo;
    std::filesystem::path m_cwd;
    const gg::CancelToken& m_token;
    bool m_rollingBack = false; // the rollback steps run: their git commands ignore the cancel
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
    bool createsCommits = false;     // makes commits with plain git: the recorder keeps a detached HEAD's new commit
    bool submodulesAutoUpdate = true; // settings: off = MutationContext::submodules has no effect
    bool submodulesAutoInit = false;  // settings: the update also initializes submodules (--init)
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

// Diff has its own queue: blame, reflog, commit details and messages (Content) never delay a diff.
// Status has its own queue: a long working tree scan never delays the snapshot, config, journal and
// merge-check jobs (Snapshot).
enum class Queue {
    Mutation = 0, Snapshot = 1, History = 2, Content = 3, Network = 4, Preview = 5, Remote = 6, Diff = 7, Status = 8
};
constexpr int kQueueCount = 9;

class Worker;
class Watcher;
struct HistoryState;

// What the window does now. The file watcher refreshes the status by this state.
enum class WindowState { Focused, Unfocused, Minimized };

class Engine {
public:
    struct Options {
        std::filesystem::path path;
        bool watch = true;
        // Unfocused window: at most one status refresh from the watcher in this time.
        std::chrono::milliseconds unfocusedStatusInterval{2000};
        // How the working tree status is read.
        StatusBackend statusBackend = StatusBackend::Git;
    };

    explicit Engine(Options options);
    ~Engine();
    Engine(const Engine&) = delete;
    Engine& operator=(const Engine&) = delete;

    // ---- read requests --------------------------------------------------------------------
    RequestId open();
    RequestId refresh(bool withStatus = true);
    // `cancelRunning` false (the file watcher): a running scan ends by itself and exactly one
    // more scan runs after it. True: the running scan is cancelled.
    RequestId refreshStatus(bool cancelRunning = true);
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
    // Journals what plain git did to the refs since the last pass (gg::reconcile::run), on the
    // mutation queue; coalesced, so calling it for every watcher event is fine.
    RequestId reconcile();
    RequestId scanConflicts(std::vector<Oid> commits);
    RequestId readConfig(std::vector<std::string> keys);
    // The tags on each remote, one RemoteTagsEvent per remote. Never asks for credentials (a remote
    // that needs them reports an error); on its own queue so a slow remote holds up nothing else.
    RequestId readRemoteTags(std::vector<std::string> remotes);
    // The warnings the commit dialog shows for what is staged (CommitWarningsEvent; a newer
    // request replaces an older one).
    RequestId readCommitWarnings();
    // Whether the local `branch` is merged into `target` (a revision), as BranchMergedEvent.
    RequestId readBranchMerged(std::string branch, std::string target);
    // The result of an interactive rebase todo, computed in memory (RebasePreviewEvent; nothing
    // is written). A newer request cancels the older one.
    RequestId rebasePreview(gg::todo::Todo todo, std::shared_ptr<const gg::todo::Context> context,
        gg::todo::Options options);

    // The window state, from any thread. The status refreshes that the watcher asks for follow it.
    // Focused: each one runs. Unfocused: at most one in each `unfocusedStatusInterval`; poll() runs
    // the held one when the time ends. Minimized: all are held. A focus gain or a restore runs
    // exactly one refresh when any was held. Mutation and user refreshes are not affected.
    void setWindowState(WindowState state);
    // What the watcher thread does with one notice. Public so a test can send a fake event.
    void onWatchEvent(const WatchEvent& event);

    void cancel(RequestId id);
    void cancelAll();

    // Up to `maxEvents` events, oldest first.
    std::vector<Event> poll(size_t maxEvents);
    // No queued or running work and no undelivered events.
    bool idle() const;
    std::vector<Activity> activities() const;
    // A job of `queue` is running now (queued jobs do not count). Used by tests.
    bool running(Queue queue) const;

    const std::filesystem::path& path() const { return m_options.path; }

    // ---- used by workers ------------------------------------------------------------------
    void emit(Event event);
    void setProgress(RequestId id, int percent, const std::string& label = {});

private:
    struct Job;
    RequestId submit(Queue queue, std::string label, int slot, bool cancellable,
        std::function<void(Job&)> fn, bool cancelRunning = true);
    void finish(RequestId id, bool cancelled, bool failed);
    void requestWatcherStatus();
    void runHeldWatcherStatus();
    // True when an index-only watcher notice comes from a write of our own mutation (see onWatchEvent).
    bool ownIndexWrite() const;

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

    // Watcher status refreshes by window state. Own mutex: the watcher thread and the UI thread use it.
    std::mutex m_windowMutex;
    WindowState m_windowState = WindowState::Focused;
    bool m_statusHeld = false;
    std::chrono::steady_clock::time_point m_lastWatcherStatus{};

    // Self-write window: the mutations that run now and queue a refresh (with a status scan) when
    // they end, and the time the last one ended. Own mutex: the watcher thread reads it.
    mutable std::mutex m_selfMutex;
    int m_selfRunning = 0;
    std::chrono::steady_clock::time_point m_selfEnded{};

    std::string m_busy;
    std::shared_ptr<void> m_statusConflicts;  // gg::conflicts::Cache, status worker only
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
