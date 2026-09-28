#include "SequenceEditor.hpp"

#include <libgg/GitRunner.hpp>
#include <libgg/Launch.hpp>
#include <libgg/LocalSocket.hpp>
#include <libgg/NativeRebase.hpp>
#include <libgg/SequenceEditorLink.hpp>

#include <chrono>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <optional>
#include <thread>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace gitgg {

namespace fs = std::filesystem;

namespace {

constexpr const char* kPrefix = "git gg sequence-editor: ";

// ---- the ggui program ---------------------------------------------------------------------------

bool hasDisplay()
{
#if defined(_WIN32) || defined(__APPLE__)
    return true;
#else
    for (const char* name : {"DISPLAY", "WAYLAND_DISPLAY"})
        if (const char* v = std::getenv(name); v && *v)
            return true;
    return false;
#endif
}

// The working tree of a git dir: a linked worktree's `gitdir` file names its .git file.
fs::path worktreeOf(const fs::path& gitDir)
{
    std::ifstream in(gitDir / "gitdir");
    std::string line;
    if (in && std::getline(in, line) && !gg::trim(line).empty())
        return fs::path(gg::trim(line)).parent_path();
    if (gitDir.filename() == ".git")
        return gitDir.parent_path();
    return gitDir;
}

// ---- talking to ggui ---------------------------------------------------------------------------

bool writeFile(const fs::path& path, const std::string& text)
{
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out << text;
    return static_cast<bool>(out);
}

// The final answer on an accepted connection; returns git-gg's exit status.
int awaitAnswer(gg::net::Socket s, const fs::path& file, bool remaining)
{
    std::string line;
    const bool got = gg::net::recvLine(s, line);
    if (got && line.rfind("SAVE ", 0) == 0) {
        const size_t size = static_cast<size_t>(std::strtoull(line.c_str() + 5, nullptr, 10));
        std::string text;
        const bool ok = gg::net::recvExact(s, text, size);
        gg::net::closeSocket(s);
        if (!ok) {
            std::cerr << kPrefix << "ggui closed the connection while sending the list\n";
            return 1;
        }
        if (!writeFile(file, text)) {
            std::cerr << kPrefix << "cannot write " << file.string() << "\n";
            return 1;
        }
        return 0;
    }
    gg::net::closeSocket(s);
    if (got && line.rfind("ERROR ", 0) == 0) {
        std::cerr << kPrefix << line.substr(6) << "\n";
        return 1;
    }
    if (!got || line != "CANCEL")
        std::cerr << kPrefix << "ggui closed the todo editor without saving\n";
    // Cancel: an empty list makes a starting rebase stop ("nothing to do"), like deleting every
    // line in git's editor; git rebase --edit-todo keeps the list as it is.
    if (!remaining && !writeFile(file, "")) {
        std::cerr << kPrefix << "cannot write " << file.string() << "\n";
        return 1;
    }
    return 0;
}

// Hands `file` to a ggui that has `gitDir` open; nullopt when none takes it.
std::optional<int> handOver(const std::string& gitDir, const fs::path& file, bool remaining)
{
    for (const auto& instance : gg::seqlink::instances()) {
        if (instance.gitDir != gitDir)
            continue;
        const auto s = gg::net::connectLoopback(instance.port);
        if (s == gg::net::kInvalid)
            continue; // a stale registration
        std::string reply;
        const bool accepted = gg::net::sendAll(s,
                                  instance.token + "\n" + gg::seqlink::kCommand + "\n" + gitDir + "\n" + file.string() + "\n")
            && gg::net::recvLine(s, reply, 10000) && reply == "OK";
        if (!accepted) {
            gg::net::closeSocket(s);
            continue;
        }
        return awaitAnswer(s, file, remaining);
    }
    return std::nullopt;
}

// Without a ggui to show it: git's own editor (core.editor, VISUAL, EDITOR), as if
// sequence.editor were not set.
int gitEditor(const fs::path& file, const std::string& why)
{
    std::cerr << kPrefix << why << "; using git's editor\n";
    gg::RunRequest r;
    r.args = {"git", "var", "GIT_EDITOR"};
    r.gitEnvironment = false;
    const auto res = gg::run(r);
    const std::string editor = gg::trim(res.out);
    if (!res.ok() || editor.empty()) {
        std::cerr << kPrefix << "no editor: " << res.message() << "\n";
        return 1;
    }
    if (editor == ":")
        return 0;
#ifdef _WIN32
    const std::string command = editor + " \"" + file.string() + "\"";
#else
    std::string quoted = "'";
    for (char c : file.string())
        quoted += c == '\'' ? std::string("'\\''") : std::string(1, c);
    quoted += "'";
    const std::string command = editor + " " + quoted;
#endif
    std::cout.flush();
    const int status = std::system(command.c_str());
#ifdef _WIN32
    return status;
#else
    return WIFEXITED(status) ? WEXITSTATUS(status) : 1;
#endif
}

int editInGgui(const fs::path& given)
{
    const fs::path file = fs::absolute(given);
    if (file.filename() != "git-rebase-todo" || file.parent_path().filename() != "rebase-merge") {
        std::cerr << kPrefix << file.string()
                  << " is not git's rebase todo: set git gg sequence-editor as sequence.editor, not core.editor\n";
        return 1;
    }
    const fs::path gitDir = file.parent_path().parent_path();
    // git rebase --edit-todo (a rebase under way) has done steps; a starting rebase has none.
    std::error_code ec;
    const bool remaining = fs::exists(file.parent_path() / "done", ec);
    const std::string key = gg::seqlink::gitDirKey(gitDir);
    if (auto status = handOver(key, file, remaining))
        return *status;

    if (!hasDisplay())
        return gitEditor(file, "no display to show ggui on");
    const fs::path program = gg::gguiProgram();
    if (program.empty())
        return gitEditor(file, "ggui was not found");
    gg::DetachedProcess child;
    std::string error;
    if (!gg::spawnDetached({program.string(), worktreeOf(gitDir).string()}, child, error))
        return gitEditor(file, error);
    // Wait for the new ggui to open the repository, then hand the list over.
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::minutes(2);
    while (std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        if (auto status = handOver(key, file, remaining))
            return *status;
        if (!gg::stillRunning(child)) {
            std::cerr << kPrefix << "ggui exited before it showed the todo list\n";
            return 1;
        }
    }
    std::cerr << kPrefix << "ggui did not open " << worktreeOf(gitDir).string() << " in time\n";
    return 1;
}

} // namespace

int runSequenceEditor(const std::string& file)
{
    // ggui's own git rebase -i: the prepared todo and messages.
    if (const char* dir = std::getenv("GG_SEQUENCE_DIR"); dir && *dir) {
        std::string error;
        const int status = gg::native::sequenceEditor(file, error);
        if (status != 0)
            std::cerr << kPrefix << error << "\n";
        return status;
    }
    // Plain git rebase -i with ggui's todo editor as sequence.editor (P4-02).
    return editInGgui(file);
}

} // namespace gitgg
