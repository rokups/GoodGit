// Repository-independent background services: the askpass bridge, the todo editor link and cloning.
#pragma once

#include <libgg/Cancel.hpp>

#include <atomic>
#include <condition_variable>
#include <deque>
#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace gg::todo {
struct Context;
}

namespace ggui::core {

// ggui as GIT_ASKPASS / SSH_ASKPASS (product spec §4.8, T1). git runs git-gg with the prompt;
// git-gg connects to this loopback server (token-protected); the prompt is shown in a dialog on
// the UI thread and the answer goes back. Answers are never stored.
class AskpassServer {
public:
    struct Request {
        std::uint64_t id = 0;
        std::string prompt;
        bool secret = true;      // password / passphrase prompts are masked
    };

    AskpassServer();
    ~AskpassServer();
    bool start(const std::filesystem::path& gitGgProgram);
    void stop();
    // UI thread: the next unanswered request (does not remove it).
    std::optional<Request> pending();
    void answer(std::uint64_t id, const std::string& text);
    void cancel(std::uint64_t id);
    // Refuses every pending prompt (closing: nobody is left to answer them).
    void cancelAll();
    int port() const { return m_port; }

private:
    void loop();
    struct Slot {
        Request request;
        bool done = false;
        bool ok = false;
        std::string answer;
    };
    std::intptr_t m_listener = -1;
    int m_port = 0;
    std::string m_token;
    std::atomic<bool> m_stop{false};
    std::thread m_thread;
    std::mutex m_mutex;
    std::condition_variable m_cv;
    std::deque<std::shared_ptr<Slot>> m_slots;
    std::uint64_t m_next = 1;
};

// ggui's todo editor as Git's sequence.editor (product spec §4.13; protocol in
// libgg/SequenceEditorLink.hpp). The server registers this process with the git dir of the open
// repository; `git gg sequence-editor FILE` run by a plain `git rebase -i` connects, and the list
// git wrote is read here, on the connection's own thread with its own repository handle. The UI
// thread shows it in the todo editor and answers with the saved list or a cancel; git-gg blocks
// meanwhile. A git-gg that goes away (Ctrl+C in the terminal) ends its request.
class SequenceEditorServer {
public:
    struct Request {
        std::uint64_t id = 0;
        std::string gitDir;          // the repository's git dir as setRepository() named it
        std::filesystem::path file;  // <git dir>/rebase-merge/git-rebase-todo
        bool remaining = false;      // git rebase --edit-todo (a rebase under way), else a starting rebase
        std::string text;            // the list as git wrote it
        std::shared_ptr<const gg::todo::Context> context;
    };

    SequenceEditorServer();
    ~SequenceEditorServer();
    bool start();
    void stop();
    // UI thread: the git dir of the repository requests are accepted for ("" = none).
    void setRepository(const std::string& gitDir);
    // UI thread: the oldest request not yet shown; `shown` marks it as taken.
    std::optional<Request> pending();
    void shown(std::uint64_t id);
    // git-gg still waits for this request's answer.
    bool waiting(std::uint64_t id);
    void save(std::uint64_t id, const std::string& text);
    void cancel(std::uint64_t id);
    void cancelAll();

private:
    struct Slot {
        Request request;
        bool shown = false;
        bool done = false;
        bool saved = false;
        std::string text;
    };
    void loop();
    void serve(std::intptr_t client);
    void answer(std::uint64_t id, bool saved, const std::string& text);

    std::intptr_t m_listener = -1;
    int m_port = 0;
    std::string m_token;
    std::atomic<bool> m_stop{false};
    std::thread m_thread;
    std::mutex m_mutex;
    std::condition_variable m_cv;
    std::string m_gitDir;           // as named by the UI
    std::string m_gitDirKey;        // its gitDirKey (computed on the loop thread)
    bool m_registered = false;
    std::string m_registeredGitDir;
    std::vector<std::shared_ptr<Slot>> m_slots;
    struct Client {
        std::thread thread;
        std::shared_ptr<std::atomic<bool>> finished;
    };
    std::vector<Client> m_clients; // connection threads (loop thread only)
    std::uint64_t m_next = 1;
};

// `git clone --progress` on a background thread with progress and Cancel.
class CloneService {
public:
    enum class State { Idle, Running, Done, Failed, Cancelled };
    ~CloneService();
    void start(const std::string& url, const std::filesystem::path& destination);
    void cancel();
    State state() const { return m_state.load(); }
    int percent() const { return m_percent.load(); }
    std::string phase() const;
    std::string error() const;
    std::filesystem::path destination() const;
    void reset();

private:
    std::thread m_thread;
    std::atomic<State> m_state{State::Idle};
    std::atomic<int> m_percent{-1};
    gg::CancelToken m_cancel;
    mutable std::mutex m_mutex;
    std::string m_phase;
    std::string m_error;
    std::filesystem::path m_destination;
};

} // namespace ggui::core
