#include "core/Engine.hpp"

#include "History.hpp"
#include "Readers.hpp"
#include "Watcher.hpp"
#include "Worker.hpp"

#include <libgg/Conflicts.hpp>
#include <libgg/GitRunner.hpp>
#include <libgg/Hooks.hpp>
#include <libgg/Journal.hpp>
#include <libgg/Operation.hpp>

#include <spdlog/spdlog.h>

#include <algorithm>
#include <thread>

namespace ggui::core {

namespace {

// Slots for latest-wins requests.
constexpr int kSlotSnapshot = 1;
constexpr int kSlotStatus = 2;
constexpr int kSlotHistoryQuery = 1;
constexpr int kSlotHistorySearch = 2;
constexpr int kSlotDiffBase = 10;
constexpr int kSlotBlame = 20;
constexpr int kSlotReflog = 30;
constexpr int kSlotDetails = 40;
constexpr int kSlotOperations = 3;
constexpr int kSlotConfig = 4;
constexpr int kSlotHooks = 5;
constexpr int kSlotRebasePreview = 1;
constexpr int kSlotRemoteTags = 1;

std::string firstLines(const std::string& text, int n = 6)
{
    std::string out;
    int lines = 0;
    for (char c : gg::trim(text)) {
        if (c == '\n' && ++lines >= n)
            break;
        out.push_back(c);
    }
    return out;
}

const char* queueName(Queue q)
{
    switch (q) {
    case Queue::Mutation: return "mutation";
    case Queue::Snapshot: return "snapshot";
    case Queue::History: return "history";
    case Queue::Content: return "content";
    case Queue::Network: return "network";
    case Queue::Preview: return "preview";
    case Queue::Remote: return "remote";
    }
    return "?";
}

// Test switch (§8.1): the slow-git latency also delays repository opening, so "cancel while
// opening" can be exercised from a scenario.
void simulateLatency(const gg::CancelToken& token)
{
    const auto latency = gg::slowGitLatency();
    if (latency.count() <= 0)
        return;
    const auto until = std::chrono::steady_clock::now() + latency;
    while (std::chrono::steady_clock::now() < until) {
        gg::throwIfCancelled(token);
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
}

} // namespace

struct Engine::Job {
    Engine& engine;
    Worker& worker;
    RequestId id;
    const gg::CancelToken& token;
    git_repository* repo() { return worker.repo(); }
};

Engine::Engine(Options options) : m_options(std::move(options))
{
    m_history = std::make_shared<HistoryState>();
    for (int i = 0; i < kQueueCount; ++i) {
        m_workers[i] = std::make_unique<Worker>(queueName(static_cast<Queue>(i)), m_options.path,
            [this](std::uint64_t id) { finish(id, true, false); });
    }
}

Engine::~Engine()
{
    // Stop the workers first so no job can create the watcher after it is gone.
    for (auto& w : m_workers)
        w->cancelAll();
    for (auto& w : m_workers)
        w->stop();
    m_watcher.reset();
}

RequestId Engine::submit(Queue queue, std::string label, int slot, bool cancellable, std::function<void(Job&)> fn)
{
    const RequestId id = m_nextId++;
    gg::CancelToken token;
    {
        std::lock_guard lock(m_mutex);
        m_activities.push_back(Activity{id, std::move(label), -1, cancellable, std::chrono::steady_clock::now()});
        m_tokens.emplace_back(id, token);
    }
    Worker::Task task;
    task.id = id;
    task.slot = slot;
    task.token = token;
    task.fn = [this, id, name = m_activities.back().label, fn = std::move(fn)](Worker& worker,
                  const gg::CancelToken& tok) {
        Job job{*this, worker, id, tok};
        const auto started = std::chrono::steady_clock::now();
        bool cancelled = false;
        bool failed = false;
        try {
            if (tok.cancelled())
                throw gg::Cancelled{};
            fn(job);
        } catch (const gg::Cancelled&) {
            cancelled = true;
        } catch (const gg::git2::Error& e) {
            failed = true;
            if (tok.cancelled())
                cancelled = true;
            else
                emit(ErrorEvent{id, "Repository error", e.what()});
        } catch (const std::exception& e) {
            failed = true;
            emit(ErrorEvent{id, "Error", e.what()});
        }
        const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started);
        if (ms.count() >= 100)
            spdlog::info("slow task: {} took {} ms{}", name, ms.count(), cancelled ? " (cancelled)" : "");
        finish(id, cancelled, failed);
    };
    m_workers[static_cast<int>(queue)]->post(std::move(task));
    return id;
}

void Engine::finish(RequestId id, bool cancelled, bool failed)
{
    std::lock_guard lock(m_mutex);
    const auto before = m_activities.size();
    std::erase_if(m_activities, [id](const Activity& a) { return a.id == id; });
    std::erase_if(m_tokens, [id](const auto& p) { return p.first == id; });
    if (m_activities.size() != before)
        m_events.push_back(TaskFinishedEvent{id, cancelled, failed});
}

void Engine::emit(Event event)
{
    std::lock_guard lock(m_mutex);
    m_events.push_back(std::move(event));
}

void Engine::setProgress(RequestId id, int percent, const std::string& label)
{
    std::lock_guard lock(m_mutex);
    for (auto& a : m_activities)
        if (a.id == id) {
            a.percent = percent;
            if (!label.empty())
                a.label = label;
        }
}

RequestId Engine::open()
{
    return submit(Queue::Snapshot, "Opening repository", 0, true, [this](Job& job) {
        simulateLatency(job.token);
        job.worker.resetRepo();
        git_repository* repo = job.repo();
        SnapshotPtr snap = readSnapshot(repo, ++m_generation, job.token);
        gg::throwIfCancelled(job.token);
        if (m_options.watch && !m_watcher) {
            try {
                m_watcher = std::make_unique<Watcher>(snap->workdir, snap->gitDir, snap->commonDir,
                    [this](const WatchEvent& e) {
                        emit(e);
                        if (e.refs)
                            refresh(true);
                        else if (e.index || e.worktree)
                            refreshStatus();
                    });
            } catch (const std::exception& e) {
                spdlog::warn("file watcher unavailable: {}", e.what());
            }
        }
        emit(OpenedEvent{job.id, std::move(snap)});
    });
}

RequestId Engine::refresh(bool withStatus)
{
    const RequestId id = submit(Queue::Snapshot, "Reading references", kSlotSnapshot, true, [this](Job& job) {
        SnapshotPtr snap = readSnapshot(job.repo(), ++m_generation, job.token);
        emit(SnapshotEvent{job.id, std::move(snap)});
    });
    if (withStatus)
        refreshStatus();
    return id;
}

RequestId Engine::refreshStatus()
{
    return submit(Queue::Snapshot, "Reading working tree status", kSlotStatus, true, [this](Job& job) {
        const RequestId id = job.id;
        StatusPtr status = readStatus(job.repo(), m_generation.load(), job.token,
            [this, id](StatusPtr partial) { emit(StatusEvent{id, std::move(partial)}); });
        if (!m_statusConflicts)
            m_statusConflicts = std::make_shared<gg::conflicts::Cache>(
                std::filesystem::path(git_repository_commondir(job.repo())));
        auto full = std::make_shared<StatusResult>(*status);
        addFirstClassConflicts(job.repo(), *full, m_statusConflicts.get());
        emit(StatusEvent{job.id, std::move(full)});
    });
}

RequestId Engine::loadHistory(const HistoryScope& scope, int limit, SnapshotPtr snapshot)
{
    auto state = m_history;
    return submit(Queue::History, "Loading history", kSlotHistoryQuery, true,
        [this, state, scope, limit, snapshot = std::move(snapshot)](Job& job) {
            const RequestId id = job.id;
            historyStart(job.repo(), *state, id, scope, snapshot, limit, job.token,
                [this, id](std::shared_ptr<HistoryBatch> b) { emit(HistoryEvent{id, std::move(b)}); });
        });
}

RequestId Engine::showMoreHistory(int additional)
{
    auto state = m_history;
    return submit(Queue::History, "Loading more history", 0, true, [this, state, additional](Job& job) {
        const RequestId id = job.id;
        historyContinue(job.repo(), *state, state->limit + additional, job.token,
            [this, id](std::shared_ptr<HistoryBatch> b) { emit(HistoryEvent{id, std::move(b)}); });
    });
}

RequestId Engine::revealCommit(const Oid& target)
{
    auto state = m_history;
    return submit(Queue::History, "Revealing commit " + target.shortHex(), 0, true, [this, state, target](Job& job) {
        const RequestId id = job.id;
        const bool found = historyReveal(job.repo(), *state, target, job.token,
            [this, id](std::shared_ptr<HistoryBatch> b) { emit(HistoryEvent{id, std::move(b)}); });
        emit(RevealEvent{job.id, target, found});
    });
}

RequestId Engine::searchHistory(const std::string& text)
{
    auto state = m_history;
    return submit(Queue::History, "Searching history", kSlotHistorySearch, true, [this, state, text](Job& job) {
        auto matches = historySearch(*state, text, job.token);
        emit(SearchEvent{job.id, text, std::move(matches), true});
    });
}

RequestId Engine::diff(const DiffQuery& query, int slot)
{
    return submit(Queue::Content, "Computing diff", kSlotDiffBase + slot, true, [this, query, slot](Job& job) {
        simulateLatency(job.token);
        DiffPtr d = readDiff(job.repo(), query, job.token);
        emit(DiffEvent{job.id, slot, std::move(d)});
    });
}

RequestId Engine::blame(const BlameQuery& query)
{
    return submit(Queue::Content, "Blaming " + query.path, kSlotBlame, true, [this, query](Job& job) {
        simulateLatency(job.token);
        BlamePtr b = readBlame(job.repo(), query, job.token);
        emit(BlameEvent{job.id, std::move(b)});
    });
}

RequestId Engine::reflog(const std::string& ref)
{
    return submit(Queue::Content, "Reading reflog", kSlotReflog, true, [this, ref](Job& job) {
        emit(ReflogEvent{job.id, readReflog(job.repo(), ref)});
    });
}

RequestId Engine::commitDetails(const Oid& id)
{
    return submit(Queue::Content, "Reading commit", kSlotDetails, true, [this, id](Job& job) {
        emit(CommitDetailsEvent{job.id, readCommitDetails(job.repo(), id)});
    });
}

void Engine::cancel(RequestId id)
{
    gg::CancelToken token;
    bool found = false;
    {
        std::lock_guard lock(m_mutex);
        for (auto& [rid, tok] : m_tokens)
            if (rid == id) {
                token = tok;
                found = true;
            }
    }
    if (!found)
        return;
    token.cancel();
    for (auto& w : m_workers)
        w->cancel(id);
}

void Engine::cancelAll()
{
    std::vector<RequestId> ids;
    {
        std::lock_guard lock(m_mutex);
        for (auto& [rid, tok] : m_tokens) {
            tok.cancel();
            ids.push_back(rid);
        }
    }
    for (auto id : ids)
        for (auto& w : m_workers)
            w->cancel(id);
}

std::vector<Event> Engine::poll(size_t maxEvents)
{
    std::vector<Event> out;
    std::lock_guard lock(m_mutex);
    while (!m_events.empty() && out.size() < maxEvents) {
        out.push_back(std::move(m_events.front()));
        m_events.pop_front();
    }
    return out;
}

bool Engine::idle() const
{
    std::lock_guard lock(m_mutex);
    return m_events.empty() && m_activities.empty();
}

std::vector<Activity> Engine::activities() const
{
    std::lock_guard lock(m_mutex);
    return m_activities;
}

// ---- mutations -------------------------------------------------------------------------------

Outcome classifyFailure(const std::string& text)
{
    auto has = [&](const char* s) { return text.find(s) != std::string::npos; };
    if (has("would be overwritten by") || has("Please commit your changes or stash them")
        || has("Your local changes to the following files") || has("You have unstaged changes")
        || has("cannot pull with rebase") || has("contains uncommitted changes"))
        return Outcome::LocalChanges;
    if (has("[rejected]") && (has("non-fast-forward") || has("fetch first")))
        return Outcome::PushRejected;
    if (has("Updates were rejected because"))
        return Outcome::PushRejected;
    return Outcome::Failed;
}

gg::RunResult MutationContext::gitMayFail(std::vector<std::string> args, std::string input, bool progress)
{
    gg::RunRequest r;
    r.args.reserve(args.size() + 1);
    r.args.emplace_back("git");
    for (auto& a : args)
        r.args.push_back(std::move(a));
    r.cwd = m_cwd;
    r.input = std::move(input);
    r.cancel = m_token;
    r.env = env;
    if (progress) {
        Engine* engine = &m_engine;
        const RequestId id = m_id;
        r.onProgress = [engine, id](const gg::GitProgress& p) { engine->setProgress(id, p.percent, p.phase); };
    }
    gg::RunResult res = gg::run(r);
    if (res.cancelled)
        throw MutationError{Outcome::Cancelled, "Cancelled", {}};
    return res;
}

gg::RunResult MutationContext::git(std::vector<std::string> args, std::string input, bool progress)
{
    gg::RunResult res = gitMayFail(std::move(args), std::move(input), progress);
    if (!res.ok()) {
        const std::string all = res.err + res.out;
        throw MutationError{classifyFailure(all), firstLines(res.message()), all};
    }
    return res;
}

void MutationContext::progress(int percent, const std::string& label) { m_engine.setProgress(m_id, percent, label); }

RequestId Engine::mutate(MutationSpec spec)
{
    const Queue queue = spec.network ? Queue::Network : Queue::Mutation;
    std::string label = spec.label;
    return submit(queue, std::move(label), 0, true, [this, spec = std::move(spec)](Job& job) {
        {
            std::lock_guard lock(m_mutex);
            m_busy = spec.label;
        }
        MutationFinishedEvent ev;
        ev.request = job.id;
        ev.label = spec.label;
        git_repository* repo = job.repo();
        const bool bare = git_repository_is_bare(repo) == 1;
        const std::filesystem::path cwd = bare ? std::filesystem::path(git_repository_path(repo))
                                               : std::filesystem::path(git_repository_workdir(repo));
        std::optional<gg::OperationRecorder> recorder;
        if (spec.journal) {
            recorder.emplace(repo, "ggui", spec.label, spec.captureIndex && !bare);
            recorder->begin();
        }
        MutationContext ctx(*this, job.id, repo, cwd, job.token);
        try {
            spec.run(ctx);
            ev.outcome = Outcome::Ok;
            ev.result = ctx.result;
            ev.message = ctx.info;
        } catch (const MutationError& e) {
            ev.outcome = e.outcome;
            ev.message = e.message;
            ev.detail = e.detail;
        } catch (const gg::Cancelled&) {
            ev.outcome = Outcome::Cancelled;
            ev.message = "Cancelled";
        } catch (const std::exception& e) {
            ev.outcome = Outcome::Failed;
            ev.message = e.what();
        }
        if (recorder) {
            for (auto& w : ctx.worktrees)
                recorder->addWorktree(std::move(w));
            recorder->finish(ev.outcome == Outcome::Ok, ctx.worktreeFollowsIndex);
            ev.operation = recorder->id();
            ev.journalError = recorder->journalError();
        }
        {
            std::lock_guard lock(m_mutex);
            m_busy.clear();
        }
        emit(std::move(ev));
        if (spec.refreshAfter)
            refresh(true);
        readOperations();
    });
}

std::string Engine::busyLabel() const
{
    std::lock_guard lock(m_mutex);
    return m_busy;
}

RequestId Engine::readOperations()
{
    return submit(Queue::Snapshot, "Reading the undo journal", kSlotOperations, true, [this](Job& job) {
        git_repository* repo = job.repo();
        gg::journal::Journal journal{std::filesystem::path(git_repository_commondir(repo))};
        OperationsEvent ev;
        ev.request = job.id;
        ev.operations = journal.read(&ev.error, &ev.skipped);
        // Cheap installed check: config-defined entry or wrapper script.
        gg::git2::Config cfg = gg::git2::repositoryConfig(repo);
        ev.hooksInstalled = gg::git2::configString(cfg.get(), "hook.ggui-reference-transaction.command").has_value();
        if (!ev.hooksInstalled) {
            std::filesystem::path hooksDir = std::filesystem::path(git_repository_commondir(repo)) / "hooks";
            if (auto p = gg::git2::configString(cfg.get(), "core.hooksPath"))
                hooksDir = std::filesystem::path(*p);
            std::error_code ec;
            ev.hooksInstalled = std::filesystem::exists(hooksDir / "reference-transaction.gg-previous", ec)
                || std::filesystem::exists(std::filesystem::path(git_repository_commondir(repo)) / "gg" / "hooks" / "run", ec);
        }
        emit(std::move(ev));
    });
}

RequestId Engine::scanConflicts(std::vector<Oid> commits)
{
    return submit(Queue::History, "Checking for conflicts", 0, true, [this, commits = std::move(commits)](Job& job) {
        git_repository* repo = job.repo();
        if (!m_historyConflicts)
            m_historyConflicts = std::make_shared<gg::conflicts::Cache>(std::filesystem::path(git_repository_commondir(repo)));
        auto& cache = *std::static_pointer_cast<gg::conflicts::Cache>(m_historyConflicts);
        ConflictsEvent ev;
        ev.request = job.id;
        for (size_t i = 0; i < commits.size(); ++i) {
            const git_oid oid = toGit(commits[i]);
            const auto files = gg::conflicts::commitConflicts(repo, oid, cache, job.token);
            ev.scanned.push_back(commits[i]);
            if (!files.empty()) {
                CommitConflicts c{commits[i], {}};
                for (const auto& f : files)
                    c.files.emplace_back(f.path, f.sides);
                ev.commits.push_back(std::move(c));
            }
            if (ev.scanned.size() >= 500) {
                emit(ev);
                ev.commits.clear();
                ev.scanned.clear();
            }
        }
        if (cache.dirty)
            cache.save();
        emit(std::move(ev));
    });
}

RequestId Engine::readConfig(std::vector<std::string> keys)
{
    return submit(Queue::Snapshot, "Reading configuration", kSlotConfig, true, [this, keys = std::move(keys)](Job& job) {
        // A fresh handle: the job's repository keeps the config files it found when it was opened
        // (config.worktree only appears once extensions.worktreeConfig is set).
        const char* workdir = git_repository_workdir(job.repo());
        gg::git2::Repository fresh = gg::git2::openRepositoryExact(workdir ? workdir : git_repository_path(job.repo()));
        git_repository* repo = fresh.get();
        ConfigEvent ev;
        ev.request = job.id;
        git_config* rawAll = nullptr;
        gg::git2::check(git_repository_config(&rawAll, repo), "git_repository_config");
        gg::git2::Config all(rawAll);
        // Each scope's files, lowest precedence first: "user" is what git config --global reads
        // ($XDG_CONFIG_HOME/git/config, then ~/.gitconfig); "system" is shown as hints only.
        struct ScopeLevels {
            const char* name;
            std::vector<git_config_level_t> levels;
        };
        const ScopeLevels scopes[] = {{"system", {GIT_CONFIG_LEVEL_PROGRAMDATA, GIT_CONFIG_LEVEL_SYSTEM}},
            {"user", {GIT_CONFIG_LEVEL_XDG, GIT_CONFIG_LEVEL_GLOBAL}}, {"repository", {GIT_CONFIG_LEVEL_LOCAL}},
            {"worktree", {GIT_CONFIG_LEVEL_WORKTREE}}};
        ev.values["effective"]; // every scope is present, even with nothing set ("loaded")
        for (const auto& scope : scopes) {
            ev.values[scope.name];
            for (const git_config_level_t level : scope.levels) {
                git_config* raw = nullptr;
                if (git_config_open_level(&raw, all.get(), level) != 0) {
                    git_error_clear();
                    continue;
                }
                gg::git2::Config cfg(raw);
                for (const auto& key : keys) {
                    git_buf buf = GIT_BUF_INIT;
                    if (git_config_get_string_buf(&buf, cfg.get(), key.c_str()) == 0)
                        ev.values[scope.name][key] = std::string(buf.ptr, buf.size);
                    git_buf_dispose(&buf);
                    git_error_clear();
                }
            }
        }
        gg::git2::Config snap = gg::git2::repositoryConfig(repo);
        for (const auto& key : keys)
            if (auto v = gg::git2::configString(snap.get(), key.c_str()))
                ev.values["effective"][key] = *v;
        emit(std::move(ev));
    });
}

RequestId Engine::readRemoteTags(std::vector<std::string> remotes)
{
    return submit(Queue::Remote, "Reading tags on remotes", kSlotRemoteTags, true,
        [this, remotes = std::move(remotes)](Job& job) {
            for (const auto& remote : remotes) {
                gg::throwIfCancelled(job.token);
                gg::RunRequest r;
                r.args = {"git", "ls-remote", "--tags", "--refs", remote};
                r.cwd = path();
                r.cancel = job.token;
                // No prompt: ggui's credentials dialog is for what the user started.
                r.env = {{"GIT_ASKPASS", std::nullopt}, {"SSH_ASKPASS", std::nullopt},
                    {"SSH_ASKPASS_REQUIRE", std::nullopt}};
                const gg::RunResult res = gg::run(r);
                gg::throwIfCancelled(job.token);
                RemoteTagsEvent ev;
                ev.request = job.id;
                ev.remote = remote;
                ev.ok = res.ok();
                if (!ev.ok)
                    ev.error = res.message();
                for (const auto& line : gg::splitLines(res.out)) {
                    const auto tab = line.find('\t');
                    const std::string prefix = "refs/tags/";
                    if (tab != std::string::npos && line.compare(tab + 1, prefix.size(), prefix) == 0)
                        ev.tags.push_back(line.substr(tab + 1 + prefix.size()));
                }
                emit(std::move(ev));
            }
        });
}

RequestId Engine::readHooksStatus()
{
    return submit(Queue::Snapshot, "Reading hook status", kSlotHooks, true, [this](Job& job) {
        git_repository* repo = job.repo();
        const std::filesystem::path dir = git_repository_workdir(repo) ? std::filesystem::path(git_repository_workdir(repo))
                                                                        : std::filesystem::path(git_repository_path(repo));
        emit(HooksEvent{job.id, gg::hooks::status(dir)});
    });
}

RequestId Engine::rebasePreview(gg::todo::Todo todo, std::shared_ptr<const gg::todo::Context> context,
    gg::todo::Options options)
{
    return submit(Queue::Preview, "Previewing the interactive rebase", kSlotRebasePreview, true,
        [this, todo = std::move(todo), context = std::move(context), options](Job& job) {
            simulateLatency(job.token);
            emit(RebasePreviewEvent{job.id, readRebasePreview(job.worker.repoPath(), todo, *context, options, job.token)});
        });
}

// ---- SummaryService ------------------------------------------------------------------------------

SummaryService::SummaryService()
{
    m_worker = std::make_unique<Worker>("summary", std::filesystem::path(), [this](std::uint64_t) { --m_pending; });
}

SummaryService::~SummaryService() { m_worker->stop(); }

void SummaryService::request(const std::vector<std::filesystem::path>& paths)
{
    ++m_pending;
    Worker::Task task;
    static std::atomic<std::uint64_t> next{1};
    task.id = next++;
    task.slot = 1;
    task.fn = [this, paths](Worker&, const gg::CancelToken& token) {
        std::vector<RepoSummary> results;
        for (const auto& p : paths) {
            if (token.cancelled())
                break;
            results.push_back(readSummary(p));
        }
        {
            std::lock_guard lock(m_mutex);
            m_results = std::move(results);
        }
        --m_pending;
    };
    m_worker->post(std::move(task));
}

std::vector<RepoSummary> SummaryService::poll()
{
    std::lock_guard lock(m_mutex);
    return std::exchange(m_results, {});
}

bool SummaryService::idle() const { return m_pending.load() == 0; }

} // namespace ggui::core
