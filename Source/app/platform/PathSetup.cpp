#include "platform/PathSetup.hpp"

#include "util/Env.hpp"

#include <libgg/Files.hpp>

#include <cctype>
#include <cerrno>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <system_error>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#endif

namespace ggui {

namespace fs = std::filesystem;

namespace {

constexpr const char* kTail = ":${PATH}";

std::string trim(const std::string& s)
{
    const auto a = s.find_first_not_of(" \t\r");
    if (a == std::string::npos)
        return {};
    return s.substr(a, s.find_last_not_of(" \t\r") - a + 1);
}

// The directory in the file's `PATH=<dir>:${PATH}` line ("" when there is none).
std::string pathEntry(const std::string& content)
{
    std::istringstream in(content);
    std::string line;
    while (std::getline(in, line)) {
        line = trim(line);
        if (line.rfind("PATH=", 0) != 0)
            continue;
        std::string value = line.substr(5);
        const std::string tail = kTail;
        if (value.size() >= tail.size() && value.compare(value.size() - tail.size(), tail.size(), tail) == 0)
            value.resize(value.size() - tail.size());
        return value;
    }
    return {};
}

std::vector<std::string> splitList(const std::string& list)
{
    std::vector<std::string> out;
    std::size_t start = 0;
    for (;;) {
        const std::size_t semi = list.find(';', start);
        if (semi == std::string::npos) {
            out.push_back(list.substr(start));
            return out;
        }
        out.push_back(list.substr(start, semi - start));
        start = semi + 1;
    }
}

// The form entries are compared in: trimmed, unquoted, '\\' for '/', no trailing separator, ASCII lower case.
std::string normalizeEntry(const std::string& entry)
{
    std::string s = trim(entry);
    if (s.size() >= 2 && s.front() == '"' && s.back() == '"')
        s = trim(s.substr(1, s.size() - 2));
    for (char& c : s)
        c = c == '/' ? '\\' : static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    while (s.size() > 1 && s.back() == '\\')
        s.pop_back();
    return s;
}

#ifdef _WIN32

// UTF-8 to UTF-16, strict. False when `s` is not valid UTF-8 or cannot be converted.
bool widen(const std::string& s, std::wstring& out)
{
    out.clear();
    if (s.empty())
        return true;
    const int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s.c_str(), static_cast<int>(s.size()), nullptr, 0);
    if (n <= 0)
        return false;
    out.assign(static_cast<std::size_t>(n), L'\0');
    return MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s.c_str(), static_cast<int>(s.size()), out.data(), n)
        == n;
}

// UTF-16 to UTF-8, strict. False when `w` has an invalid surrogate or cannot be converted.
bool narrow(const std::wstring& w, std::string& out)
{
    out.clear();
    if (w.empty())
        return true;
    const int n = WideCharToMultiByte(
        CP_UTF8, WC_ERR_INVALID_CHARS, w.c_str(), static_cast<int>(w.size()), nullptr, 0, nullptr, nullptr);
    if (n <= 0)
        return false;
    out.assign(static_cast<std::size_t>(n), '\0');
    return WideCharToMultiByte(
               CP_UTF8, WC_ERR_INVALID_CHARS, w.c_str(), static_cast<int>(w.size()), out.data(), n, nullptr, nullptr)
        == n;
}

// Reads the user PATH value and its registry type. A missing key or value reads as "" with type
// REG_EXPAND_SZ (what Windows itself uses for a new user PATH). Returns an error message, "" on
// success. A value that cannot be edited safely (not text, an embedded NUL, not valid UTF-16) is an
// error, so a write never starts from a damaged copy.
std::string readUserPath(std::string& value, DWORD& type)
{
    value.clear();
    type = REG_EXPAND_SZ;
    HKEY key = nullptr;
    LONG rc = RegOpenKeyExW(HKEY_CURRENT_USER, L"Environment", 0, KEY_QUERY_VALUE, &key);
    if (rc == ERROR_FILE_NOT_FOUND)
        return {};
    if (rc != ERROR_SUCCESS)
        return "Cannot open HKEY_CURRENT_USER\\Environment (error " + std::to_string(rc) + ").";
    std::string err;
    DWORD kind = 0;
    DWORD size = 0;
    rc = RegQueryValueExW(key, L"Path", nullptr, &kind, nullptr, &size);
    if (rc == ERROR_SUCCESS) {
        // One spare character, in case the value grows between the two calls.
        std::wstring buf(size / sizeof(wchar_t) + 1, L'\0');
        DWORD got = static_cast<DWORD>(buf.size() * sizeof(wchar_t));
        rc = RegQueryValueExW(key, L"Path", nullptr, &kind, reinterpret_cast<LPBYTE>(buf.data()), &got);
        if (rc == ERROR_SUCCESS) {
            if (kind != REG_SZ && kind != REG_EXPAND_SZ) {
                err = "The user PATH in the registry is not a text value, so it was not changed.";
            } else {
                buf.resize(got / sizeof(wchar_t));
                if (!buf.empty() && buf.back() == L'\0')
                    buf.pop_back(); // the final terminator
                if (buf.find(L'\0') != std::wstring::npos)
                    err = "The user PATH in the registry holds a NUL character, so it was not changed.";
                else if (!narrow(buf, value))
                    err = "The user PATH in the registry is not valid text, so it was not changed.";
                else
                    type = kind;
            }
        }
    }
    if (err.empty() && rc != ERROR_SUCCESS && rc != ERROR_FILE_NOT_FOUND)
        err = "Cannot read the user PATH from the registry (error " + std::to_string(rc) + ").";
    RegCloseKey(key);
    if (!err.empty())
        value.clear();
    return err;
}

std::string writeUserPath(const std::string& value, DWORD type)
{
    std::wstring wide;
    if (!widen(value, wide))
        return "The new user PATH cannot be converted to UTF-16, so it was not written.";
    HKEY key = nullptr;
    LONG rc = RegCreateKeyExW(
        HKEY_CURRENT_USER, L"Environment", 0, nullptr, 0, KEY_SET_VALUE, nullptr, &key, nullptr);
    if (rc != ERROR_SUCCESS)
        return "Cannot open HKEY_CURRENT_USER\\Environment for writing (error " + std::to_string(rc) + ").";
    rc = RegSetValueExW(key, L"Path", 0, type, reinterpret_cast<const BYTE*>(wide.c_str()),
        static_cast<DWORD>((wide.size() + 1) * sizeof(wchar_t)));
    RegCloseKey(key);
    if (rc != ERROR_SUCCESS)
        return "Cannot write the user PATH to the registry (error " + std::to_string(rc) + ").";
    return {};
}

// Tells running programs (Explorer, which starts new terminals) that the environment changed.
void broadcastEnvironmentChange()
{
    DWORD_PTR result = 0;
    SendMessageTimeoutW(HWND_BROADCAST, WM_SETTINGCHANGE, 0, reinterpret_cast<LPARAM>(L"Environment"),
        SMTO_ABORTIFHUNG, 5000, &result);
}

std::string writeUserPathEntry(const std::string& dir, bool enable)
{
    if (enable) {
        if (dir.empty())
            return "Cannot tell where the GoodGit executable is.";
        if (dir.find_first_of(";\"\r\n") != std::string::npos)
            return "The GoodGit directory (" + dir
                + ") contains a ; or a quote or a line break, so it was not added to PATH. "
                  "Move GoodGit to a plainer path.";
    }
    std::string before;
    DWORD type = REG_EXPAND_SZ;
    std::string err = readUserPath(before, type);
    if (!err.empty())
        return err;
    const std::string after = enable ? pathListAdd(before, dir) : pathListRemove(before, dir);
    if (after == before)
        return {};
    err = writeUserPath(after, type);
    if (!err.empty())
        return err;
    broadcastEnvironmentChange();
    return {};
}

PathSetupState readUserPathState(const std::string& dir)
{
    PathSetupState st;
    std::string value;
    DWORD type = REG_EXPAND_SZ;
    if (!readUserPath(value, type).empty())
        return st;
    st.enabled = pathListContains(value, dir);
    st.present = st.enabled;
    return st;
}

#endif // _WIN32

} // namespace

bool pathListContains(const std::string& list, const std::string& dir)
{
    const std::string want = normalizeEntry(dir);
    if (want.empty())
        return false;
    for (const std::string& entry : splitList(list)) {
        if (normalizeEntry(entry) == want)
            return true;
    }
    return false;
}

std::string pathListAdd(const std::string& list, const std::string& dir)
{
    if (normalizeEntry(dir).empty() || pathListContains(list, dir))
        return list;
    if (list.empty())
        return dir;
    return list.back() == ';' ? list + dir : list + ";" + dir;
}

std::string pathListRemove(const std::string& list, const std::string& dir)
{
    const std::string want = normalizeEntry(dir);
    if (want.empty())
        return list;
    std::string out;
    bool first = true;
    for (const std::string& entry : splitList(list)) {
        if (normalizeEntry(entry) == want)
            continue;
        if (!first)
            out += ';';
        out += entry;
        first = false;
    }
    return out;
}

bool pathSetupUsable(PathSetupSupport support)
{
    return support == PathSetupSupport::Available || support == PathSetupSupport::Registry;
}

PathSetupSupport pathSetupSupport([[maybe_unused]] const std::string& root)
{
#if defined(_WIN32)
    return PathSetupSupport::Registry;
#elif defined(__linux__)
    std::error_code ec;
    return fs::is_directory(fs::path(root + "/run/systemd/system"), ec) ? PathSetupSupport::Available
                                                                       : PathSetupSupport::NoSystemd;
#else
    return PathSetupSupport::Unsupported;
#endif
}

std::string pathSetupUnavailableReason(PathSetupSupport support)
{
    switch (support) {
    case PathSetupSupport::Available:
    case PathSetupSupport::Registry:
        return {};
    case PathSetupSupport::NoSystemd:
        return "Adding GoodGit to PATH needs systemd (it writes a systemd user environment.d file), "
               "which this system does not run.";
    case PathSetupSupport::Unsupported:
        break;
    }
    return "Adding GoodGit to PATH is not supported on this platform.";
}

std::string pathSetupConfigHome()
{
    const std::string xdg = getEnv("XDG_CONFIG_HOME");
    if (!xdg.empty() && xdg.front() == '/')
        return xdg;
    const std::string home = getEnv("HOME");
    return home.empty() ? std::string() : home + "/.config";
}

std::string pathSetupFile(const std::string& configHome)
{
    return (fs::path(configHome) / "environment.d" / "60-goodgit.conf").string();
}

PathSetupState readPathSetup(const std::string& configHome, const std::string& dir)
{
    PathSetupState st;
    if (configHome.empty())
        return st;
    std::ifstream in(pathSetupFile(configHome), std::ios::binary);
    if (!in)
        return st;
    std::ostringstream buf;
    buf << in.rdbuf();
    st.present = true;
    const std::string entry = pathEntry(buf.str());
    st.enabled = !entry.empty() && entry == dir;
    if (!st.enabled)
        st.otherDir = entry;
    return st;
}

PathSetupState readPathSetup(
    [[maybe_unused]] PathSetupSupport support, const std::string& configHome, const std::string& dir)
{
#ifdef _WIN32
    if (support == PathSetupSupport::Registry)
        return readUserPathState(dir);
#endif
    return readPathSetup(configHome, dir);
}

std::string writePathSetup(
    PathSetupSupport support, const std::string& configHome, const std::string& dir, bool enable)
{
    if (support == PathSetupSupport::Registry) {
#ifdef _WIN32
        return writeUserPathEntry(dir, enable);
#else
        return pathSetupUnavailableReason(PathSetupSupport::Unsupported);
#endif
    }
    if (support != PathSetupSupport::Available)
        return pathSetupUnavailableReason(support);
    if (configHome.empty())
        return "Cannot find the user configuration directory (neither XDG_CONFIG_HOME nor HOME is set).";
    const fs::path file = pathSetupFile(configHome);
    std::error_code ec;
    if (!enable) {
        fs::remove(file, ec);
        if (ec)
            return "Cannot remove " + file.string() + ": " + ec.message();
        return {};
    }
    if (dir.empty())
        return "Cannot tell where the GoodGit executable is.";
    if (dir.find_first_of("$\\\"':\n\r") != std::string::npos)
        return "The GoodGit directory (" + dir
            + ") contains a character (one of $ \\ \" ' : or a line break) that environment.d would expand or "
              "misread, so it was not added to PATH. Move GoodGit to a plainer path.";
    fs::create_directories(file.parent_path(), ec);
    if (ec)
        return "Cannot create " + file.parent_path().string() + ": " + ec.message();
    const fs::path tmp = file.string() + ".tmp";
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!out)
            return "Cannot write " + tmp.string() + ": " + std::strerror(errno);
        out << "# Managed by GoodGit (ggui): Settings > General > Add GoodGit to PATH.\n"
               "# Puts the directory holding git-gg on the login PATH. Delete this file to undo.\n"
            << "PATH=" << dir << kTail << "\n";
        out.flush();
        if (!out) {
            out.close();
            fs::remove(tmp, ec);
            return "Cannot write " + tmp.string() + ".";
        }
    }
    ec = gg::replaceFile(tmp, file);
    if (ec) {
        std::error_code ignore;
        fs::remove(tmp, ignore);
        return "Cannot write " + file.string() + ": " + ec.message();
    }
    return {};
}

} // namespace ggui
