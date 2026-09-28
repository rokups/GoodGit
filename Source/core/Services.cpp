#include "core/Services.hpp"

#include <libgg/Git2.hpp>
#include <libgg/GitRunner.hpp>
#include <libgg/LocalSocket.hpp>
#include <libgg/SequenceEditorLink.hpp>
#include <libgg/Todo.hpp>

#include <git2.h>

#include <fstream>
#include <random>
#include <sstream>

#ifdef _WIN32
#include <process.h>
#else
#include <unistd.h>
#endif

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

// ---- SequenceEditorServer --------------------------------------------------------------------------

namespace {

long long ownPid()
{
#ifdef _WIN32
    return static_cast<long long>(_getpid());
#else
    return static_cast<long long>(getpid());
#endif
}

std::string readText(const fs::path& path)
{
    std::ifstream in(path, std::ios::binary);
    if (!in)
        throw std::runtime_error("cannot read " + path.string());
    std::ostringstream out;
    out << in.rdbuf();
    return out.str();
}

} // namespace

SequenceEditorServer::SequenceEditorServer() = default;

SequenceEditorServer::~SequenceEditorServer() { stop(); }

bool SequenceEditorServer::start()
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
    m_stop = false;
    m_thread = std::thread([this] { loop(); });
    return true;
}

void SequenceEditorServer::stop()
{
    cancelAll();
    m_stop = true;
    m_cv.notify_all();
    if (m_thread.joinable())
        m_thread.join();
    for (auto& c : m_clients)
        c.thread.join();
    m_clients.clear();
    gg::net::closeSocket(m_listener);
    m_listener = gg::net::kInvalid;
}

void SequenceEditorServer::setRepository(const std::string& gitDir)
{
    std::lock_guard lock(m_mutex);
    m_gitDir = gitDir;
}

void SequenceEditorServer::loop()
{
    while (!m_stop) {
        // Keep the registration current (file I/O stays off the UI thread).
        std::string gitDir;
        {
            std::lock_guard lock(m_mutex);
            gitDir = m_gitDir;
        }
        if (!m_registered || gitDir != m_registeredGitDir) {
            const std::string key = gitDir.empty() ? std::string() : gg::seqlink::gitDirKey(gitDir);
            const bool written = gg::seqlink::writeInstance({ownPid(), m_port, m_token, key});
            std::lock_guard lock(m_mutex);
            m_registered = written;
            m_gitDirKey = key;
            m_registeredGitDir = gitDir;
        }
        std::erase_if(m_clients, [](Client& c) {
            if (!c.finished->load())
                return false;
            c.thread.join();
            return true;
        });
        const auto client = gg::net::acceptWithTimeout(m_listener, 50);
        if (client == gg::net::kInvalid)
            continue;
        auto finished = std::make_shared<std::atomic<bool>>(false);
        m_clients.push_back({std::thread([this, client, finished] {
                                 serve(client);
                                 finished->store(true);
                             }),
            finished});
    }
    gg::seqlink::removeInstance(ownPid());
}

void SequenceEditorServer::serve(std::intptr_t client)
{
    std::string token, command, gitDir, file;
    if (!gg::net::recvLine(client, token, 2000) || token != m_token || !gg::net::recvLine(client, command, 2000)
        || command != gg::seqlink::kCommand || !gg::net::recvLine(client, gitDir, 2000)
        || !gg::net::recvLine(client, file, 2000)) {
        gg::net::closeSocket(client);
        return;
    }
    auto slot = std::make_shared<Slot>();
    Request& r = slot->request;
    {
        std::lock_guard lock(m_mutex);
        if (gitDir.empty() || gitDir != m_gitDirKey || m_gitDir != m_registeredGitDir || m_stop) {
            gg::net::sendAll(client, "NO\n");
            gg::net::closeSocket(client);
            return;
        }
        r.gitDir = m_gitDir;
    }
    gg::net::sendAll(client, "OK\n");
    // Read the list git wrote, with this thread's own repository handle.
    r.file = fs::path(file);
    try {
        const fs::path dir = r.file.parent_path();
        r.remaining = fs::exists(dir / "done");
        r.text = readText(r.file);
        const auto repo = gg::git2::openRepositoryExact(dir.parent_path());
        r.context = std::make_shared<const gg::todo::Context>(r.remaining
                ? gg::todo::readRemaining(repo.get(), r.text, readText(dir / "head-name"))
                : gg::todo::readStarting(repo.get(), r.text, readText(dir / "onto"), readText(dir / "orig-head"),
                      readText(dir / "head-name")));
    } catch (const std::exception& e) {
        std::string message = e.what();
        for (auto& c : message)
            if (c == '\n' || c == '\r')
                c = ' ';
        gg::net::sendAll(client, "ERROR ggui cannot read the todo list: " + message + "\n");
        gg::net::closeSocket(client);
        return;
    }
    {
        std::lock_guard lock(m_mutex);
        r.id = m_next++;
        m_slots.push_back(slot);
    }
    std::unique_lock lock(m_mutex);
    while (!slot->done && !m_stop) {
        m_cv.wait_for(lock, std::chrono::milliseconds(100));
        if (!slot->done && gg::net::peerClosed(client))
            break; // git-gg went away (the rebase was interrupted)
    }
    const bool saved = slot->done && slot->saved;
    const std::string text = slot->text;
    std::erase(m_slots, slot);
    lock.unlock();
    gg::net::sendAll(client, saved ? "SAVE " + std::to_string(text.size()) + "\n" + text : std::string("CANCEL\n"));
    gg::net::closeSocket(client);
}

std::optional<SequenceEditorServer::Request> SequenceEditorServer::pending()
{
    std::lock_guard lock(m_mutex);
    for (const auto& s : m_slots)
        if (!s->shown && !s->done)
            return s->request;
    return std::nullopt;
}

void SequenceEditorServer::shown(std::uint64_t id)
{
    std::lock_guard lock(m_mutex);
    for (auto& s : m_slots)
        if (s->request.id == id)
            s->shown = true;
}

bool SequenceEditorServer::waiting(std::uint64_t id)
{
    std::lock_guard lock(m_mutex);
    for (const auto& s : m_slots)
        if (s->request.id == id)
            return !s->done;
    return false;
}

void SequenceEditorServer::answer(std::uint64_t id, bool saved, const std::string& text)
{
    std::lock_guard lock(m_mutex);
    for (auto& s : m_slots)
        if (s->request.id == id && !s->done) {
            s->done = true;
            s->saved = saved;
            s->text = text;
        }
    m_cv.notify_all();
}

void SequenceEditorServer::save(std::uint64_t id, const std::string& text) { answer(id, true, text); }

void SequenceEditorServer::cancel(std::uint64_t id) { answer(id, false, {}); }

void SequenceEditorServer::cancelAll()
{
    std::lock_guard lock(m_mutex);
    for (auto& s : m_slots)
        s->done = true;
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
