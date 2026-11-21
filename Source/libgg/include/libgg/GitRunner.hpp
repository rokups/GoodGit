// The git process runner (product spec §3 "git runner", G2).
//
// Spawns `git` (or another program) with an argument vector, never through a shell, and
// collects stdout/stderr. Parsed output always runs with LC_ALL=C; progress lines from
// `--progress` are parsed best-effort; cancellation kills the whole process tree; no
// console windows on Windows. Never runs on the UI thread (asserted).
#pragma once

#include "libgg/Cancel.hpp"

#include <chrono>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace gg {

struct GitProgress {
    std::string phase;   // e.g. "Receiving objects"
    int percent = -1;    // -1 when unknown
    std::string line;    // the raw progress line
};

struct RunRequest {
    std::vector<std::string> args;        // args[0] is the program, resolved through PATH
    std::filesystem::path cwd;            // empty = inherit
    std::string input;                    // written to stdin, then stdin is closed
    // Environment overrides; nullopt removes the variable.
    std::vector<std::pair<std::string, std::optional<std::string>>> env;
    CancelToken cancel = CancelToken::none();
    std::function<void(const GitProgress&)> onProgress; // called on the calling thread
    bool cLocale = true;                  // LC_ALL=C (parsed output)
    bool gitEnvironment = true;           // GIT_TERMINAL_PROMPT=0, askpass
};

struct RunResult {
    int exitCode = -1;
    std::string out;
    std::string err;
    bool cancelled = false;
    bool startFailed = false;             // program not found or not executable
    std::chrono::milliseconds duration{0};

    bool ok() const { return !cancelled && !startFailed && exitCode == 0; }
    // stderr trimmed, or a synthesized message; used for error banners.
    std::string message() const;
};

// Runs the process to completion (or cancellation). Asserts it is not on the UI thread.
RunResult run(const RunRequest& request);

// Convenience: `git <args...>` in `cwd`.
RunResult git(const std::filesystem::path& cwd, std::vector<std::string> args, std::string input = {});

// The version of the `git` on PATH as major * 100 + minor (2.40 → 240); 0 when unknown.
// Runs `git version` each time (a test may put another git on PATH).
int gitVersion(const std::filesystem::path& cwd = {});

// ---- Process-wide git environment ---------------------------------------------------------

// Program used for GIT_ASKPASS / SSH_ASKPASS (empty = leave unset) and the endpoint it talks
// to (exported as GG_ASKPASS_ENDPOINT).
void setAskpassProgram(std::string program, std::string endpoint = {});
std::string askpassProgram();
std::string askpassEndpoint();

// The journal operation open on this thread (thread-local; not passed to child processes).
void setCurrentOperation(std::string id);
std::string currentOperation();

// ---- Test hooks (§8.1): slow-git latency switch and the git command log ---------------------

void setSlowGitLatency(std::chrono::milliseconds latency);
std::chrono::milliseconds slowGitLatency();

struct CommandLogEntry {
    std::vector<std::string> args;
    std::filesystem::path cwd;
    int exitCode = -1;
    std::chrono::milliseconds duration{0};
    std::string err;
};
void setCommandLogEnabled(bool enabled);
std::vector<CommandLogEntry> commandLog();
void clearCommandLog();
// Commands running now (while the log is enabled): what a hung test waits for. `duration` is how
// long each has run so far.
std::vector<CommandLogEntry> runningCommands();

// ---- Helpers -----------------------------------------------------------------------------

// Parses one progress line such as "Receiving objects:  45% (1/2)".
std::optional<GitProgress> parseProgressLine(const std::string& line);

// Splits NUL-separated output (`-z`), dropping a trailing empty field.
std::vector<std::string> splitNul(const std::string& text);
// Splits on '\n', dropping a trailing empty line.
std::vector<std::string> splitLines(const std::string& text);
std::string trim(std::string text);

// Quotes one argument by the CommandLineToArgvW rules (UTF-8 in, UTF-8 out).
std::string quoteWindowsArg(const std::string& arg);
// CreateProcess takes at most 32767 characters with the end NUL.
constexpr size_t kMaxWindowsCommandLine = 32766;
bool windowsCommandLineFits(size_t length);
// Whether the name ends in .cmd or .bat (any case).
bool isBatchFileName(const std::string& name);

// The command-line tail that runs a batch file through `cmd.exe`: `/d /v:off /s /c ""script" "arg""`.
// Every argument is quoted and encoded: `"` becomes `""`, `%` becomes `%%cd:~,%`, and the backslashes
// before a `"` or the closing quote are doubled. `^&|<>` stay as they are (plain text in quotes).
// It gives "" with `error` for a control character (line break too) in an argument, or `%`, `"` or a
// line break in the script path.
std::string batchShellArgs(const std::string& script, const std::vector<std::string>& args, std::string& error);
// File names to try for `program`. On Windows: .exe, .cmd, .bat, then the name itself when it already
// ends in .exe, .com, .cmd or .bat (CreateProcess cannot run a file with another extension).
std::vector<std::string> programCandidates(const std::string& program, bool windows);
#ifdef _WIN32
// Builds the CreateProcess command line for `args` with `program` as the program path. A batch file
// runs through cmd.exe: `application` is then cmd.exe, else it is empty.
bool buildWindowsCommandLine(const std::vector<std::string>& args, const std::string& program,
    std::wstring& application, std::wstring& cmd, std::string& error);
#endif

// Full path of `program` found through PATH, or empty. Tries .exe, .cmd and .bat on Windows.
std::filesystem::path findInPath(const std::string& program);

} // namespace gg
