// Repository-independent background services: the askpass bridge and cloning.
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

namespace ggui::core {

// ggui as GIT_ASKPASS / SSH_ASKPASS (REBUILD_PLAN §4.8, T1). git runs git-gg with the prompt;
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

// `git clone --progress` on a background thread with progress and Cancel (P2-16).
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
