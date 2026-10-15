#include "platform/SelfUpdate.hpp"

#include "libgg/GitRunner.hpp"
#include "util/Sha256.hpp"

#include <miniz.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <cctype>
#include <condition_variable>
#include <fstream>
#include <iterator>
#include <mutex>
#include <set>
#include <system_error>
#include <thread>

namespace ggui::update {

namespace fs = std::filesystem;

namespace {

constexpr std::string_view kUrlPrefix = "https://github.com/rokups/GoodGit/releases/download/";
constexpr std::string_view kDigestPrefix = "sha256:";

std::string stringField(const nlohmann::json& object, const char* key)
{
    const auto it = object.find(key);
    if (it == object.end() || !it->is_string()) {
        return {};
    }
    return it->get<std::string>();
}

bool startsWith(std::string_view text, std::string_view prefix)
{
    return text.size() >= prefix.size() && text.compare(0, prefix.size(), prefix) == 0;
}

bool endsWith(std::string_view text, std::string_view suffix)
{
    return text.size() >= suffix.size() && text.compare(text.size() - suffix.size(), suffix.size(), suffix) == 0;
}

bool isHex(char c)
{
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}

bool isCommitId(std::string_view text)
{
    return text.size() == 40 && std::all_of(text.begin(), text.end(), isHex);
}

bool isLowerHexDigest(std::string_view text)
{
    return text.size() == 64 &&
           std::all_of(text.begin(), text.end(), [](char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'); });
}

char lower(char c)
{
    return static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
}

} // namespace

bool parseRelease(std::string_view json, Release& out, std::string& error)
{
    error.clear();
    out = Release{};
    const nlohmann::json root = nlohmann::json::parse(json.begin(), json.end(), nullptr, false);
    if (root.is_discarded()) {
        error = "The release data is not valid JSON";
        return false;
    }
    if (!root.is_object()) {
        error = "The release data is not a JSON object";
        return false;
    }
    out.commit = stringField(root, "target_commitish");
    const auto assets = root.find("assets");
    if (assets == root.end() || !assets->is_array()) {
        return true;
    }
    for (const nlohmann::json& item : *assets) {
        if (!item.is_object() || stringField(item, "state") != "uploaded") {
            continue;
        }
        Asset asset;
        asset.name = stringField(item, "name");
        asset.url = stringField(item, "browser_download_url");
        const std::string digest = stringField(item, "digest");
        if (startsWith(digest, kDigestPrefix)) {
            asset.sha256 = digest.substr(kDigestPrefix.size());
        }
        const auto size = item.find("size");
        if (size != item.end() && size->is_number_unsigned()) {
            asset.size = size->get<std::uint64_t>();
        }
        out.assets.push_back(std::move(asset));
    }
    return true;
}

const Asset* selectAsset(const Release& release, std::string_view packageSystem)
{
    if (packageSystem.empty()) {
        return nullptr;
    }
    const std::string suffix = "-" + std::string(packageSystem) + ".zip";
    for (const Asset& asset : release.assets) {
        if (!startsWith(asset.name, "ggui-") || !endsWith(asset.name, suffix)) {
            continue;
        }
        if (!startsWith(asset.url, kUrlPrefix) || !isLowerHexDigest(asset.sha256) || asset.size == 0) {
            return nullptr;
        }
        return &asset;
    }
    return nullptr;
}

Freshness compare(std::string_view local, std::string_view release)
{
    if (!isCommitId(local) || !isCommitId(release)) {
        return Freshness::Unknown;
    }
    const bool same = std::equal(local.begin(), local.end(), release.begin(),
                                 [](char a, char b) { return lower(a) == lower(b); });
    return same ? Freshness::UpToDate : Freshness::Newer;
}

namespace {

#ifdef _WIN32
constexpr const char* kGguiName = "ggui.exe";
constexpr const char* kGitGgName = "git-gg.exe";
#else
constexpr const char* kGguiName = "ggui";
constexpr const char* kGitGgName = "git-gg";
#endif

// The scripts are plain text with LF here; windowsUpdateScript converts the Windows one to CRLF.
constexpr const char* kUnixScript = R"SH(#!/bin/sh
# ggui self-update. Arg 1: pid of ggui. Arg 2: "nostart" skips the restart.
PID=$1
NOSTART=$2
STAGE=$(cd -- "$(dirname -- "$0")" && pwd -P) || exit 1
DEST=$(dirname -- "$STAGE")
NEW=$STAGE/new
RESULT=$STAGE/result.txt
p=0

finish() {
    if [ "$NOSTART" != "nostart" ]; then
        exec "$DEST/ggui"
    fi
    exit 0
}

undo() {
    if [ "$p" -ge 2 ]; then
        mv -f "$DEST/git-gg.old" "$DEST/git-gg" || U=1
    fi
    if [ "$p" -ge 1 ]; then
        mv -f "$DEST/ggui.old" "$DEST/ggui" || U=1
    fi
}

bad() {
    U=0
    undo
    echo "failed: $1" > "$RESULT"
    if [ "$U" = 1 ]; then
        echo "failed: undo" >> "$RESULT"
    fi
    finish
}

n=0
while kill -0 "$PID" 2>/dev/null; do
    n=$((n + 1))
    if [ "$n" -gt 120 ]; then
        echo "failed: ggui did not exit" > "$RESULT"
        finish
    fi
    sleep 1
done

rm -f "$DEST/ggui.old" "$DEST/git-gg.old"
[ ! -e "$DEST/ggui.old" ] && [ ! -e "$DEST/git-gg.old" ] || bad "delete old files"
mv -f "$DEST/ggui" "$DEST/ggui.old" || bad "backup ggui"
p=1
mv -f "$DEST/git-gg" "$DEST/git-gg.old" || bad "backup git-gg"
p=2
mv -f "$NEW/ggui" "$DEST/ggui" || bad "install ggui"
p=3
mv -f "$NEW/git-gg" "$DEST/git-gg" || bad "install git-gg"
p=4
echo "ok" > "$RESULT"
rm -f "$DEST/ggui.old" "$DEST/git-gg.old"
finish
)SH";

constexpr const char* kWindowsScript = R"CMD(@echo off
setlocal EnableExtensions DisableDelayedExpansion
rem ggui self-update. Arg 1: pid of ggui. Arg 2: "nostart" skips the restart.
set "STAGE=%~dp0"
for %%I in ("%~dp0..") do set "DEST=%%~fI"
set "NEW=%STAGE%new"
set "RESULT=%STAGE%result.txt"
set "TRIES=0"
set "UNDOFAIL="

:wait
"%SystemRoot%\System32\tasklist.exe" /fi "PID eq %~1" /nh >"%STAGE%tasks.txt" 2>nul
if errorlevel 1 goto again
"%SystemRoot%\System32\find.exe" " %~1 " "%STAGE%tasks.txt" >nul
if errorlevel 2 goto again
if errorlevel 1 goto swap
:again
set /a TRIES+=1
if %TRIES% gtr 120 goto timeout
"%SystemRoot%\System32\ping.exe" -n 2 127.0.0.1 >nul
goto wait

:timeout
>"%RESULT%" echo failed: ggui did not exit
goto start

:swap
del /f /q "%DEST%\ggui.exe.old" "%DEST%\git-gg.exe.old" >nul 2>&1
if exist "%DEST%\ggui.exe.old" goto fail_stale
if exist "%DEST%\git-gg.exe.old" goto fail_stale
move /y "%DEST%\ggui.exe" "%DEST%\ggui.exe.old" >nul 2>&1
if errorlevel 1 goto fail_backup_ggui
move /y "%DEST%\git-gg.exe" "%DEST%\git-gg.exe.old" >nul 2>&1
if errorlevel 1 goto fail_backup_gitgg
move /y "%NEW%\ggui.exe" "%DEST%\ggui.exe" >nul 2>&1
if errorlevel 1 goto fail_install_ggui
move /y "%NEW%\git-gg.exe" "%DEST%\git-gg.exe" >nul 2>&1
if errorlevel 1 goto fail_install_gitgg
if exist "%NEW%\LICENSE.txt" copy /y "%NEW%\LICENSE.txt" "%DEST%\LICENSE.txt" >nul 2>&1
>"%RESULT%" echo ok
del /f /q "%DEST%\ggui.exe.old" "%DEST%\git-gg.exe.old" >nul 2>&1
goto start

:fail_stale
set "STEP=delete old files"
goto undo0
:fail_backup_ggui
set "STEP=backup ggui"
goto undo0
:fail_backup_gitgg
set "STEP=backup git-gg"
goto undo1
:fail_install_ggui
set "STEP=install ggui"
goto undo2
:fail_install_gitgg
set "STEP=install git-gg"
goto undo3

:undo3
:undo2
move /y "%DEST%\git-gg.exe.old" "%DEST%\git-gg.exe" >nul 2>&1
if errorlevel 1 set "UNDOFAIL=1"
:undo1
move /y "%DEST%\ggui.exe.old" "%DEST%\ggui.exe" >nul 2>&1
if errorlevel 1 set "UNDOFAIL=1"
:undo0
>"%RESULT%" echo failed: %STEP%
if defined UNDOFAIL >>"%RESULT%" echo failed: undo

:start
if /i "%~2"=="nostart" goto done
start "" "%DEST%\ggui.exe"
:done
endlocal
exit /b 0
)CMD";

bool isSpace(char c)
{
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

std::string trimmed(const std::string& text)
{
    size_t b = 0;
    size_t e = text.size();
    while (b < e && isSpace(text[b])) {
        ++b;
    }
    while (e > b && isSpace(text[e - 1])) {
        --e;
    }
    return text.substr(b, e - b);
}

// Removes `<dir>/<name>.old` when `<dir>/<name>` exists (else the .old file may be the only copy).
void removeOldFile(const fs::path& dir, const char* name)
{
    std::error_code ec;
    if (fs::exists(dir / name, ec)) {
        fs::remove(dir / (std::string(name) + ".old"), ec);
    }
}

} // namespace

std::string installProblem(const fs::path& exe)
{
    if (exe.filename() != fs::path("ggui") && exe.filename() != fs::path("ggui.exe")) {
        return "This is not an installed ggui.";
    }
#ifdef __linux__
    // Text only, no disk access: a package manager owns /usr, but /usr/local is a manual install.
    const std::string text = exe.lexically_normal().generic_string();
    if (startsWith(text, "/usr/") && !startsWith(text, "/usr/local/")) {
        return "Installed by the package manager. Update it with apt.";
    }
#endif
    const fs::path dir = exe.parent_path();
    std::error_code ec;
    if (!fs::exists(dir / "git-gg", ec) && !fs::exists(dir / "git-gg.exe", ec)) {
        return "This is not an installed ggui.";
    }
    const fs::path stage = stageDir(dir);
    const bool stageExisted = fs::exists(stage, ec);
    const fs::path probe = stage / "probe";
    bool writable = false;
    fs::create_directories(stage, ec);
    if (!ec) {
        std::ofstream file(probe, std::ios::binary);
        writable = file.good();
    }
    fs::remove(probe, ec);
    if (!stageExisted) {
        fs::remove(stage, ec);
    }
    return writable ? std::string() : std::string("The install folder is not writable.");
}

fs::path stageDir(const fs::path& installDir)
{
    return installDir / ".ggui-update";
}

std::string windowsUpdateScript()
{
    std::string out;
    for (const char* c = kWindowsScript; *c; ++c) {
        if (*c == '\n') {
            out += '\r';
        }
        out += *c;
    }
    return out;
}

std::string unixUpdateScript()
{
    return kUnixScript;
}

fs::path scriptPath(const fs::path& stage)
{
#ifdef _WIN32
    return stage / "update.cmd";
#else
    return stage / "update.sh";
#endif
}

std::string writeScript(const fs::path& stage)
{
    std::error_code ec;
    fs::create_directories(stage, ec);
    if (ec) {
        return "Cannot create the update folder: " + ec.message();
    }
#ifdef _WIN32
    const std::string text = windowsUpdateScript();
#else
    const std::string text = unixUpdateScript();
#endif
    const fs::path path = scriptPath(stage);
    {
        std::ofstream file(path, std::ios::binary | std::ios::trunc);
        file.write(text.data(), static_cast<std::streamsize>(text.size()));
        file.flush();
        if (!file) {
            return "Cannot write the update script.";
        }
    }
#ifndef _WIN32
    fs::permissions(path,
        fs::perms::owner_all | fs::perms::group_read | fs::perms::group_exec | fs::perms::others_read |
            fs::perms::others_exec,
        fs::perm_options::replace, ec);
    if (ec) {
        return "Cannot make the update script executable: " + ec.message();
    }
#endif
    return {};
}

std::optional<std::string> takeResult(const fs::path& installDir)
{
    const fs::path stage = stageDir(installDir);
    std::error_code ec;
    if (!fs::is_directory(stage, ec)) {
        return std::nullopt;
    }
    std::optional<std::string> result;
    {
        std::ifstream file(stage / "result.txt", std::ios::binary);
        if (file) {
            const std::string text = trimmed(std::string(std::istreambuf_iterator<char>(file), {}));
            if (!text.empty()) {
                result = text;
            }
        }
    }
    if (!result) {
        // A second ggui can start while the script of the first still runs: keep a young stage.
        fs::file_time_type time = fs::last_write_time(scriptPath(stage), ec);
        if (ec) {
            ec.clear();
            time = fs::last_write_time(stage, ec);
        }
        if (ec || fs::file_time_type::clock::now() - time < std::chrono::minutes(10)) {
            return std::nullopt;
        }
    }
    fs::remove_all(stage, ec);
    if (result) {
        removeOldFile(installDir, kGguiName);
        removeOldFile(installDir, kGitGgName);
    }
    return result;
}

namespace {

constexpr size_t kMaxEntries = 1000;
constexpr std::uint64_t kMaxTotalBytes = std::uint64_t(1) << 30; // 1 GiB
constexpr mz_uint kUnixHost = 3;                                 // "version made by" host: Unix
constexpr mz_uint kModeTypeMask = 0170000;
constexpr mz_uint kModeFile = 0100000;
constexpr mz_uint kModeDir = 0040000;
constexpr auto kVersionTimeout = std::chrono::seconds(20);

// gg::run reads its arguments as UTF-8.
std::string pathUtf8(const fs::path& path)
{
    const auto text = path.u8string();
    return std::string(text.begin(), text.end());
}

std::string lowerCase(std::string text)
{
    for (char& c : text) {
        c = lower(c);
    }
    return text;
}

// Splits a ZIP entry name into its parts when it is a plain relative path. A folder entry may end
// in one "/".
bool splitEntryName(const std::string& name, std::vector<std::string>& parts)
{
    parts.clear();
    std::string text = name;
    if (!text.empty() && text.back() == '/') {
        text.pop_back();
    }
    size_t start = 0;
    while (true) {
        const size_t end = text.find('/', start);
        const std::string part = text.substr(start, end == std::string::npos ? std::string::npos : end - start);
        if (part.empty() || part == "." || part == ".." || part.back() == '.' || part.back() == ' ') {
            return false;
        }
        for (const char c : part) {
            const unsigned char u = static_cast<unsigned char>(c);
            if (u < 0x20 || u >= 0x7f || c == '\\' || c == ':' || c == '*' || c == '?' || c == '"' || c == '<' ||
                c == '>' || c == '|') {
                return false;
            }
        }
        parts.push_back(part);
        if (end == std::string::npos) {
            return true;
        }
        start = end + 1;
    }
}

size_t readZipBlock(void* opaque, mz_uint64 offset, void* buffer, size_t count)
{
    std::ifstream& file = *static_cast<std::ifstream*>(opaque);
    file.clear();
    file.seekg(static_cast<std::streamoff>(offset));
    if (!file) {
        return 0;
    }
    file.read(static_cast<char*>(buffer), static_cast<std::streamsize>(count));
    return static_cast<size_t>(file.gcount());
}

struct ZipReader {
    mz_zip_archive zip;
    bool open = false;

    ZipReader() { mz_zip_zero_struct(&zip); }
    ZipReader(const ZipReader&) = delete;
    ZipReader& operator=(const ZipReader&) = delete;
    ~ZipReader()
    {
        if (open) {
            mz_zip_reader_end(&zip);
        }
    }
};

struct PlannedEntry {
    mz_uint index = 0;
    std::vector<std::string> parts;
    bool isDir = false;
    std::uint64_t size = 0;
};

fs::path joinParts(const fs::path& root, const std::vector<std::string>& parts)
{
    fs::path out = root;
    for (const std::string& part : parts) {
        out /= part;
    }
    return out;
}

// Checks every entry and lists the ones to write. Returns "" or an error text.
std::string planEntries(ZipReader& reader, std::vector<PlannedEntry>& plan)
{
    const mz_uint count = mz_zip_reader_get_num_files(&reader.zip);
    if (count == 0) {
        return "The update is empty.";
    }
    if (count > kMaxEntries) {
        return "The update has too many files.";
    }
    std::set<std::string> seen;
    std::set<std::string> tops;
    std::uint64_t total = 0;
    for (mz_uint i = 0; i < count; ++i) {
        mz_zip_archive_file_stat st;
        if (!mz_zip_reader_file_stat(&reader.zip, i, &st)) {
            return "The update is damaged.";
        }
        const std::string name = st.m_filename;
        PlannedEntry entry;
        entry.index = i;
        entry.isDir = mz_zip_reader_is_file_a_directory(&reader.zip, i) != 0;
        if (!splitEntryName(name, entry.parts)) {
            return "The update has an unsafe file name: " + name;
        }
        std::string key;
        for (const std::string& part : entry.parts) {
            key += lowerCase(part) + '/';
        }
        if (!seen.insert(key).second) {
            return "The update has a duplicate file name: " + name;
        }
        if (!st.m_is_supported || st.m_is_encrypted) {
            return "The update has an unsupported entry: " + name;
        }
        if (static_cast<mz_uint>(st.m_version_made_by >> 8) == kUnixHost) {
            const mz_uint type = (st.m_external_attr >> 16) & kModeTypeMask;
            if (type != 0 && type != (entry.isDir ? kModeDir : kModeFile)) {
                return "The update has a link or a special file: " + name;
            }
        }
        if (entry.isDir) {
            if (st.m_uncomp_size != 0) {
                return "The update has a folder with data: " + name;
            }
        } else if (entry.parts.size() < 2) {
            return "The update must hold exactly one top folder.";
        }
        entry.size = st.m_uncomp_size;
        if (entry.size > kMaxTotalBytes - total) {
            return "The update is too big.";
        }
        total += entry.size;
        tops.insert(lowerCase(entry.parts.front()));
        plan.push_back(std::move(entry));
    }
    if (tops.size() != 1) {
        return "The update must hold exactly one top folder.";
    }
    return {};
}

// Writes one file entry. `budget` is the number of bytes still allowed in total.
std::string writeEntry(ZipReader& reader, const PlannedEntry& entry, const fs::path& path, std::uint64_t& budget)
{
    std::error_code ec;
    fs::create_directories(path.parent_path(), ec);
    if (ec) {
        return "Cannot create a folder for the update: " + ec.message();
    }
    mz_zip_reader_extract_iter_state* iter = mz_zip_reader_extract_iter_new(&reader.zip, entry.index, 0);
    if (iter == nullptr) {
        return "The update is damaged.";
    }
    std::string error;
    std::uint64_t written = 0;
    {
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        if (!out) {
            error = "Cannot write a file of the update.";
        }
        std::vector<char> buffer(64 * 1024);
        while (error.empty()) {
            const size_t got = mz_zip_reader_extract_iter_read(iter, buffer.data(), buffer.size());
            if (got == 0) {
                break;
            }
            if (got > budget || written + got > entry.size) {
                error = "The update holds more data than it announces.";
                break;
            }
            budget -= got;
            written += got;
            out.write(buffer.data(), static_cast<std::streamsize>(got));
            if (!out) {
                error = "Cannot write a file of the update.";
            }
        }
        out.flush();
        if (error.empty() && !out) {
            error = "Cannot write a file of the update.";
        }
    }
    // The call checks the CRC when all data was read.
    const bool crcOk = mz_zip_reader_extract_iter_free(iter) != 0;
    if (error.empty() && (!crcOk || written != entry.size)) {
        error = "The update is damaged (a file does not match its checksum).";
    }
    return error;
}

std::string unpackZip(const fs::path& zipFile, std::uint64_t zipSize, const fs::path& unpackDir)
{
    std::ifstream file(zipFile, std::ios::binary);
    if (!file) {
        return "Cannot open the downloaded update.";
    }
    ZipReader reader;
    reader.zip.m_pRead = readZipBlock;
    reader.zip.m_pIO_opaque = &file;
    if (!mz_zip_reader_init(&reader.zip, zipSize, 0)) {
        return "The downloaded update is not a valid ZIP file.";
    }
    reader.open = true;
    std::vector<PlannedEntry> plan;
    if (std::string error = planEntries(reader, plan); !error.empty()) {
        return error;
    }
    std::error_code ec;
    fs::create_directories(unpackDir, ec);
    if (ec) {
        return "Cannot create the update folder: " + ec.message();
    }
    std::uint64_t budget = kMaxTotalBytes;
    for (const PlannedEntry& entry : plan) {
        const fs::path path = joinParts(unpackDir, entry.parts);
        if (entry.isDir) {
            fs::create_directories(path, ec);
            if (ec) {
                return "Cannot create a folder for the update: " + ec.message();
            }
        } else if (std::string error = writeEntry(reader, entry, path, budget); !error.empty()) {
            return error;
        }
    }
    return {};
}

// Moves `from` to `to` when `from` is a regular file (never a link). A `required` file gives an
// error when it is missing.
std::string moveFile(const fs::path& from, const fs::path& to, const char* what, bool required)
{
    std::error_code ec;
    if (!fs::is_regular_file(fs::symlink_status(from, ec))) {
        return required ? std::string("The update has no ") + what + "." : std::string();
    }
    fs::rename(from, to, ec);
    return ec ? std::string("Cannot move ") + what + ": " + ec.message() : std::string();
}

// Moves the program files from the single top folder of `unpackDir` into `newDir`.
std::string collectFiles(const fs::path& unpackDir, const fs::path& newDir)
{
    std::error_code ec;
    fs::path top;
    int count = 0;
    for (fs::directory_iterator it(unpackDir, ec), end; !ec && it != end; it.increment(ec)) {
        top = it->path();
        ++count;
    }
    if (ec || count != 1 || !fs::is_directory(fs::symlink_status(top, ec))) {
        return "The update must hold exactly one top folder.";
    }
    fs::create_directories(newDir, ec);
    if (ec) {
        return "Cannot create the update folder: " + ec.message();
    }
#ifdef _WIN32
    const fs::path programs = top;
#else
    const fs::path programs = top / "bin";
#endif
    if (std::string error = moveFile(programs / kGguiName, newDir / kGguiName, kGguiName, true); !error.empty()) {
        return error;
    }
    if (std::string error = moveFile(programs / kGitGgName, newDir / kGitGgName, kGitGgName, true); !error.empty()) {
        return error;
    }
#ifdef _WIN32
    return moveFile(top / "LICENSE.txt", newDir / "LICENSE.txt", "LICENSE.txt", false);
#else
    return {};
#endif
}

// Runs `git-gg --version` with a time limit; "" when it prints "git gg ...".
std::string checkVersion(const fs::path& gitGg)
{
    gg::RunRequest request;
    request.args = {pathUtf8(gitGg), "--version"};
    request.gitEnvironment = false;
    std::mutex mutex;
    std::condition_variable wake;
    bool done = false;
    const gg::CancelToken cancel = request.cancel;
    std::thread watchdog([&] {
        std::unique_lock lock(mutex);
        if (!wake.wait_for(lock, kVersionTimeout, [&] { return done; })) {
            cancel.cancel();
        }
    });
    const gg::RunResult result = gg::run(request);
    {
        std::lock_guard lock(mutex);
        done = true;
    }
    wake.notify_all();
    watchdog.join();
    if (result.startFailed) {
        return "The new git-gg does not start.";
    }
    if (result.cancelled) {
        return "The new git-gg did not answer in time.";
    }
    if (!result.ok() || !startsWith(result.out, "git gg ")) {
        return "The new git-gg gives a wrong version answer.";
    }
    return {};
}

} // namespace

std::string stageUpdate(const fs::path& installDir, const fs::path& zipFile, const Asset& asset)
{
    std::error_code ec;
    const std::uintmax_t size = fs::file_size(zipFile, ec);
    if (ec) {
        return "Cannot read the downloaded update: " + ec.message();
    }
    if (size != asset.size) {
        return "The downloaded update has the wrong size.";
    }
    std::string hashError;
    const std::string digest = sha256File(zipFile, hashError);
    if (digest.empty()) {
        return "Cannot read the downloaded update: " + hashError;
    }
    if (digest != lowerCase(asset.sha256)) {
        return "The downloaded update has the wrong checksum.";
    }

    const fs::path stage = stageDir(installDir);
    const auto fail = [&](const std::string& error) {
        std::error_code ignored;
        fs::remove_all(stage, ignored);
        return error;
    };
    fs::remove_all(stage, ec);
    if (ec) {
        return fail("Cannot remove the old update folder: " + ec.message());
    }
    const fs::path unpackDir = stage / "unpack";
    const fs::path newDir = stage / "new";
    if (std::string error = unpackZip(zipFile, size, unpackDir); !error.empty()) {
        return fail(error);
    }
    if (std::string error = collectFiles(unpackDir, newDir); !error.empty()) {
        return fail(error);
    }
#ifndef _WIN32
    for (const char* name : {kGguiName, kGitGgName}) {
        fs::permissions(newDir / name,
            fs::perms::owner_all | fs::perms::group_read | fs::perms::group_exec | fs::perms::others_read |
                fs::perms::others_exec,
            fs::perm_options::replace, ec);
        if (ec) {
            return fail(std::string("Cannot make ") + name + " executable: " + ec.message());
        }
    }
#endif
    if (std::string error = checkVersion(newDir / kGitGgName); !error.empty()) {
        return fail(error);
    }
    fs::remove_all(unpackDir, ec);
    if (ec) {
        return fail("Cannot remove the unpacked files: " + ec.message());
    }
    return {};
}

} // namespace ggui::update
