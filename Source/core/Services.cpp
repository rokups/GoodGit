#include "core/Services.hpp"

#include <libgg/GitRunner.hpp>
#include <libgg/LocalSocket.hpp>

#include <random>

namespace ggui::core {

namespace fs = std::filesystem;

// ---- AskpassServer ---------------------------------------------------------------------------------

AskpassServer::AskpassServer() = default;

AskpassServer::~AskpassServer() { stop(); }

bool AskpassServer::start(const fs::path& gitGgProgram)
{
    if (m_thread.joinable())
        return true;
    m_listener = gg::net::listenLoopback(m_port);
    if (m_listener == gg::net::kInvalid)
        return false;
    std::random_device rd;
    char buf[40];
    std::snprintf(buf, sizeof(buf), "%08x%08x%08x", rd(), rd(), rd());
    m_token = buf;
    gg::setAskpassProgram(gitGgProgram.string(), std::to_string(m_port) + ":" + m_token);
    m_stop = false;
    m_thread = std::thread([this] { loop(); });
    return true;
}

void AskpassServer::stop()
{
    m_stop = true;
    m_cv.notify_all();
    if (m_thread.joinable())
        m_thread.join();
    gg::net::closeSocket(m_listener);
    m_listener = gg::net::kInvalid;
}

void AskpassServer::loop()
{
    while (!m_stop) {
        const auto client = gg::net::acceptWithTimeout(m_listener, 100);
        if (client == gg::net::kInvalid)
            continue;
        std::string token, prompt;
        if (!gg::net::recvLine(client, token, 2000) || token != m_token || !gg::net::recvLine(client, prompt, 2000)) {
            gg::net::closeSocket(client);
            continue;
        }
        auto slot = std::make_shared<Slot>();
        slot->request.prompt = prompt;
        const bool username = prompt.find("sername") != std::string::npos;
        slot->request.secret = !username;
        {
            std::lock_guard lock(m_mutex);
            slot->request.id = m_next++;
            m_slots.push_back(slot);
        }
        std::unique_lock lock(m_mutex);
        m_cv.wait(lock, [&] { return slot->done || m_stop.load(); });
        const bool ok = slot->done && slot->ok;
        const std::string answer = slot->answer;
        std::erase(m_slots, slot);
        lock.unlock();
        gg::net::sendAll(client, ok ? "OK\n" + answer + "\n" : std::string("CANCEL\n"));
        gg::net::closeSocket(client);
    }
}

std::optional<AskpassServer::Request> AskpassServer::pending()
{
    std::lock_guard lock(m_mutex);
    for (const auto& s : m_slots)
        if (!s->done)
            return s->request;
    return std::nullopt;
}

void AskpassServer::answer(std::uint64_t id, const std::string& text)
{
    std::lock_guard lock(m_mutex);
    for (auto& s : m_slots)
        if (s->request.id == id) {
            s->done = true;
            s->ok = true;
            s->answer = text;
        }
    m_cv.notify_all();
}

void AskpassServer::cancel(std::uint64_t id)
{
    std::lock_guard lock(m_mutex);
    for (auto& s : m_slots)
        if (s->request.id == id) {
            s->done = true;
            s->ok = false;
        }
    m_cv.notify_all();
}

void AskpassServer::cancelAll()
{
    std::lock_guard lock(m_mutex);
    for (auto& s : m_slots) {
        s->done = true;
        s->ok = false;
    }
    m_cv.notify_all();
}

// ---- CloneService ----------------------------------------------------------------------------------

CloneService::~CloneService()
{
    m_cancel.cancel();
    if (m_thread.joinable())
        m_thread.join();
}

void CloneService::reset()
{
    if (m_thread.joinable())
        m_thread.join();
    m_state = State::Idle;
    m_percent = -1;
    std::lock_guard lock(m_mutex);
    m_phase.clear();
    m_error.clear();
}

void CloneService::start(const std::string& url, const fs::path& destination)
{
    reset();
    m_cancel = gg::CancelToken();
    {
        std::lock_guard lock(m_mutex);
        m_destination = destination;
    }
    m_state = State::Running;
    m_thread = std::thread([this, url, destination] {
        std::error_code ec;
        const bool existed = fs::exists(destination, ec);
        gg::RunRequest r;
        r.args = {"git", "clone", "--progress", "--", url, destination.string()};
        r.cwd = destination.parent_path();
        r.cancel = m_cancel;
        r.onProgress = [this](const gg::GitProgress& p) {
            m_percent = p.percent;
            std::lock_guard lock(m_mutex);
            m_phase = p.phase;
        };
        const gg::RunResult res = gg::run(r);
        if (!res.ok() && !existed)
            fs::remove_all(destination, ec); // no half-cloned directory is left behind
        std::lock_guard lock(m_mutex);
        if (res.cancelled) {
            m_state = State::Cancelled;
        } else if (!res.ok()) {
            m_error = res.message();
            m_state = State::Failed;
        } else {
            m_state = State::Done;
        }
    });
}

void CloneService::cancel() { m_cancel.cancel(); }

std::string CloneService::phase() const
{
    std::lock_guard lock(m_mutex);
    return m_phase;
}

std::string CloneService::error() const
{
    std::lock_guard lock(m_mutex);
    return m_error;
}

fs::path CloneService::destination() const
{
    std::lock_guard lock(m_mutex);
    return m_destination;
}

} // namespace ggui::core
