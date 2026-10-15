// Self-update: check, unpack and test a downloaded update ZIP (stageUpdate in platform/SelfUpdate.hpp).
#include "libgg/Launch.hpp"
#include "platform/SelfUpdate.hpp"
#include "tests/Harness.hpp"
#include "util/Sha256.hpp"

#include <miniz.h>

#include <cctype>
#include <chrono>
#include <filesystem>
#include <cstdint>
#include <fstream>
#include <functional>
#include <iterator>
#include <string>
#include <system_error>
#include <vector>

#ifndef _WIN32
#include <unistd.h>
#endif

namespace ggtest {

namespace {

namespace fs = std::filesystem;
namespace up = ggui::update;

#ifdef _WIN32
const char* const kGgui = "ggui.exe";
const char* const kGitGg = "git-gg.exe";
const std::string kBinDir; // the program files are in the top folder
#else
const char* const kGgui = "ggui";
const char* const kGitGg = "git-gg";
const std::string kBinDir = "bin/";
#endif

// A fresh empty folder, removed at the end of the test.
struct TempRoot {
    fs::path path;

    TempRoot()
    {
        static int counter = 0;
        std::error_code ec;
        path = fs::temp_directory_path(ec) / ("gg-selfupdate-stage-" + std::to_string(counter++) + "-" +
#ifdef _WIN32
                                                 std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
#else
                                                 std::to_string(getpid()));
#endif
        fs::remove_all(path, ec);
        fs::create_directories(path, ec);
    }

    ~TempRoot()
    {
        std::error_code ec;
        fs::remove_all(path, ec);
    }

    fs::path installDir() const { return path / "inst"; }
    fs::path zipFile() const { return path / "update.zip"; }
};

struct Entry {
    std::string name;
    std::string data;
};

std::string readFile(const fs::path& path)
{
    std::ifstream file(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(file), {});
}

bool pathExists(const fs::path& path)
{
    std::error_code ec;
    return fs::exists(path, ec);
}

bool contains(const std::string& text, const std::string& part)
{
    return text.find(part) != std::string::npos;
}

// The name miniz's writer accepts for `name`: it refuses a leading "/" and a drive letter.
std::string writerName(std::string name, bool hasData = false)
{
    if (hasData && !name.empty() && name.back() == '/') {
        name.back() = '#'; // a folder entry with data: patched back afterwards
    }
    for (char& c : name) {
        if (c == '\\' || c == ':') {
            c = '#';
        }
    }
    if (!name.empty() && name.front() == '/') {
        name.front() = '#';
    }
    return name;
}

// Writes a ZIP with `entries` (a name that ends in "/" is a folder). The writer refuses some bad
// names, so these are written with a stand-in of the same length and patched in the file afterwards.
// False on failure.
bool writeZip(const fs::path& path, const std::vector<Entry>& entries, int level = MZ_DEFAULT_LEVEL)
{
    mz_zip_archive zip;
    mz_zip_zero_struct(&zip);
    if (!mz_zip_writer_init_heap(&zip, 0, 0)) {
        return false;
    }
    bool ok = true;
    for (const Entry& entry : entries) {
        const std::string name = writerName(entry.name, !entry.data.empty());
        ok = ok && mz_zip_writer_add_mem(&zip, name.c_str(), entry.data.data(), entry.data.size(),
                                         static_cast<mz_uint>(level)) != 0;
    }
    void* buffer = nullptr;
    size_t size = 0;
    ok = ok && mz_zip_writer_finalize_heap_archive(&zip, &buffer, &size) != 0;
    if (ok) {
        std::string bytes(static_cast<const char*>(buffer), size);
        for (const Entry& entry : entries) {
            const std::string name = writerName(entry.name, !entry.data.empty());
            for (size_t at = bytes.find(name); name != entry.name && at != std::string::npos;
                 at = bytes.find(name, at + name.size())) {
                bytes.replace(at, name.size(), entry.name);
            }
        }
        std::ofstream file(path, std::ios::binary | std::ios::trunc);
        file.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
        file.flush();
        ok = static_cast<bool>(file);
    }
    mz_zip_writer_end(&zip);
    return ok;
}

// The asset that describes `zipFile` (size and digest).
up::Asset assetFor(const fs::path& zipFile)
{
    up::Asset asset;
    asset.name = "ggui-1-test.zip";
    std::error_code ec;
    asset.size = fs::file_size(zipFile, ec);
    std::string error;
    asset.sha256 = ggui::sha256File(zipFile, error);
    return asset;
}

// The git-gg built next to the test binary; empty when absent.
std::string realGitGg()
{
    const fs::path self = gg::selfExecutable();
    if (self.empty()) {
        return {};
    }
    return readFile(self.parent_path() / kGitGg);
}

std::uint32_t readLe(const std::string& bytes, size_t at, int count)
{
    std::uint32_t value = 0;
    for (int i = count - 1; i >= 0; --i) {
        value = (value << 8) | static_cast<unsigned char>(bytes[at + static_cast<size_t>(i)]);
    }
    return value;
}

void writeLe(std::string& bytes, size_t at, std::uint32_t value, int count)
{
    for (int i = 0; i < count; ++i) {
        bytes[at + static_cast<size_t>(i)] = static_cast<char>((value >> (8 * i)) & 0xff);
    }
}

// Offset of the central directory record of the entry `name`, or npos.
size_t centralRecord(const std::string& bytes, const std::string& name)
{
    const std::string signature("PK\x01\x02", 4);
    for (size_t at = bytes.find(signature); at != std::string::npos; at = bytes.find(signature, at + 1)) {
        if (at + 46 > bytes.size()) {
            break;
        }
        const size_t length = readLe(bytes, at + 28, 2);
        if (length == name.size() && bytes.compare(at + 46, length, name) == 0) {
            return at;
        }
    }
    return std::string::npos;
}

// Changes the ZIP file `path` in place: `change(bytes, central, local)` gets the file bytes and the
// offsets of the central record and the local header of the entry `name`.
using Patch = std::function<void(std::string& bytes, size_t central, size_t local)>;
bool patchZip(const fs::path& path, const std::string& name, const Patch& change)
{
    std::string bytes = readFile(path);
    const size_t central = centralRecord(bytes, name);
    if (central == std::string::npos) {
        return false;
    }
    const size_t local = readLe(bytes, central + 42, 4);
    change(bytes, central, local);
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    file.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    file.flush();
    return static_cast<bool>(file);
}

// Writes a ZIP with `entries` (stored without compression when `level` is 0), changes the entry
// `name` with `change` and stages the result. False when the ZIP could not be built.
bool stagePatched(const TempRoot& root, const std::vector<Entry>& entries, int level, const std::string& name,
                  const Patch& change, std::string& error)
{
    if (!writeZip(root.zipFile(), entries, level) || !patchZip(root.zipFile(), name, change)) {
        return false;
    }
    error = up::stageUpdate(root.installDir(), root.zipFile(), assetFor(root.zipFile()));
    return true;
}

// The files of a package: ggui, git-gg and a file that the update does not use.
std::vector<Entry> packageEntries(const std::string& top, const std::string& gitGg)
{
    std::vector<Entry> entries;
    entries.push_back({top + "/", ""});
    if (!kBinDir.empty()) {
        entries.push_back({top + "/" + kBinDir, ""});
    }
    entries.push_back({top + "/" + kBinDir + kGgui, "ggui bytes"});
    entries.push_back({top + "/" + kBinDir + kGitGg, gitGg});
#ifdef _WIN32
    entries.push_back({top + "/LICENSE.txt", "license"});
#else
    entries.push_back({top + "/share/doc/ggui/LICENSE", "license"});
#endif
    return entries;
}

// Builds a ZIP from `entries` in `root` and stages it. The error text goes to `error`; false when
// the ZIP could not be written.
bool stage(const TempRoot& root, const std::vector<Entry>& entries, std::string& error)
{
    if (!writeZip(root.zipFile(), entries)) {
        return false;
    }
    error = up::stageUpdate(root.installDir(), root.zipFile(), assetFor(root.zipFile()));
    return true;
}

} // namespace

GG_TEST("selfupdate", "stage unpacks a good update into new/")
{
    const std::string gitGg = realGitGg();
    if (gitGg.empty()) {
        markCurrentTestSkipped("git-gg is not next to the test binary");
        return;
    }
    TempRoot root;
    std::string error = "unset";
    GG_REQUIRE(stage(root, packageEntries("ggui-1-test", gitGg), error));
    GG_CHECK_STR_EQ(error, "");
    const fs::path newDir = up::stageDir(root.installDir()) / "new";
    GG_CHECK_STR_EQ(readFile(newDir / kGgui), "ggui bytes");
    GG_CHECK(readFile(newDir / kGitGg) == gitGg);
    GG_CHECK(!pathExists(up::stageDir(root.installDir()) / "unpack"));
#ifdef _WIN32
    GG_CHECK_STR_EQ(readFile(newDir / "LICENSE.txt"), "license");
#else
    for (const char* name : {kGgui, kGitGg}) {
        std::error_code ec;
        const fs::perms perms = fs::status(newDir / name, ec).permissions();
        GG_CHECK((perms & fs::perms::owner_exec) != fs::perms::none);
        GG_CHECK((perms & fs::perms::group_exec) != fs::perms::none);
        GG_CHECK((perms & fs::perms::others_exec) != fs::perms::none);
    }
#endif
    // Nothing else is installed from the package.
    GG_CHECK(!pathExists(newDir / "share"));
}

GG_TEST("selfupdate", "stage replaces an old stage and accepts an upper case digest")
{
    const std::string gitGg = realGitGg();
    if (gitGg.empty()) {
        markCurrentTestSkipped("git-gg is not next to the test binary");
        return;
    }
    TempRoot root;
    const fs::path stale = up::stageDir(root.installDir()) / "new" / "stale.txt";
    std::error_code ec;
    fs::create_directories(stale.parent_path(), ec);
    std::ofstream(stale) << "old";
    GG_CHECK(pathExists(stale));
    GG_REQUIRE(writeZip(root.zipFile(), packageEntries("ggui-1-test", gitGg)));
    up::Asset asset = assetFor(root.zipFile());
    for (char& c : asset.sha256) {
        c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    }
    GG_CHECK_STR_EQ(up::stageUpdate(root.installDir(), root.zipFile(), asset), "");
    GG_CHECK(!pathExists(stale));
    GG_CHECK(pathExists(up::stageDir(root.installDir()) / "new" / kGgui));
}

GG_TEST("selfupdate", "stage refuses a wrong size or digest and keeps nothing")
{
    const std::string gitGg = realGitGg();
    if (gitGg.empty()) {
        markCurrentTestSkipped("git-gg is not next to the test binary");
        return;
    }
    TempRoot root;
    GG_REQUIRE(writeZip(root.zipFile(), packageEntries("ggui-1-test", gitGg)));
    up::Asset asset = assetFor(root.zipFile());

    up::Asset wrongSize = asset;
    wrongSize.size += 1;
    const std::string sizeError = up::stageUpdate(root.installDir(), root.zipFile(), wrongSize);
    GG_CHECK(contains(sizeError, "wrong size"));

    up::Asset wrongDigest = asset;
    wrongDigest.sha256[0] = wrongDigest.sha256[0] == 'a' ? 'b' : 'a';
    const std::string digestError = up::stageUpdate(root.installDir(), root.zipFile(), wrongDigest);
    GG_CHECK(contains(digestError, "wrong checksum"));

    up::Asset noDigest = asset;
    noDigest.sha256.clear();
    GG_CHECK(!up::stageUpdate(root.installDir(), root.zipFile(), noDigest).empty());
    GG_CHECK(!pathExists(up::stageDir(root.installDir())));
}

GG_TEST("selfupdate", "stage refuses unsafe entry names")
{
    const std::string gitGg = realGitGg();
    if (gitGg.empty()) {
        markCurrentTestSkipped("git-gg is not next to the test binary");
        return;
    }
    const char* const names[] = {"../evil", "ggui-1-test/../../evil", "/evil", "C:/evil", "ggui-1-test\\evil",
                                 "ggui-1-test/./x", "ggui-1-test//x"};
    for (const char* name : names) {
        TempRoot root;
        std::vector<Entry> entries = packageEntries("ggui-1-test", gitGg);
        entries.push_back({name, "evil"});
        std::string error = "unset";
        GG_REQUIRE(stage(root, entries, error));
        GG_CHECK(contains(error, "unsafe file name"));
        GG_CHECK(!pathExists(root.path / "evil"));
        GG_CHECK(!pathExists(root.installDir() / "evil"));
        GG_CHECK(!pathExists(up::stageDir(root.installDir())));
    }
}

GG_TEST("selfupdate", "stage refuses a duplicate name, too many entries and a file at the top")
{
    const std::string gitGg = realGitGg();
    if (gitGg.empty()) {
        markCurrentTestSkipped("git-gg is not next to the test binary");
        return;
    }
    std::string error;
    {
        TempRoot root;
        std::vector<Entry> entries = packageEntries("ggui-1-test", gitGg);
        // The same name in another case is a duplicate (Windows and macOS file systems).
        entries.push_back({"ggui-1-test/" + kBinDir + "GGUI-DUP", "x"});
        entries.push_back({"ggui-1-test/" + kBinDir + "ggui-dup", "y"});
        GG_REQUIRE(stage(root, entries, error));
        GG_CHECK(contains(error, "duplicate"));
        GG_CHECK(!pathExists(up::stageDir(root.installDir())));
    }
    {
        TempRoot root;
        std::vector<Entry> entries = packageEntries("ggui-1-test", gitGg);
        for (int i = 0; i < 1000; ++i) {
            entries.push_back({"ggui-1-test/extra/" + std::to_string(i), "x"});
        }
        GG_REQUIRE(stage(root, entries, error));
        GG_CHECK(contains(error, "too many files"));
        GG_CHECK(!pathExists(up::stageDir(root.installDir())));
    }
    {
        TempRoot root;
        std::vector<Entry> entries = packageEntries("ggui-1-test", gitGg);
        entries.push_back({"loose.txt", "x"});
        GG_REQUIRE(stage(root, entries, error));
        GG_CHECK(contains(error, "one top folder"));
        GG_CHECK(!pathExists(up::stageDir(root.installDir())));
    }
}

GG_TEST("selfupdate", "stage refuses two top folders")
{
    const std::string gitGg = realGitGg();
    if (gitGg.empty()) {
        markCurrentTestSkipped("git-gg is not next to the test binary");
        return;
    }
    TempRoot root;
    std::vector<Entry> entries = packageEntries("ggui-1-test", gitGg);
    entries.push_back({"other/file.txt", "x"});
    std::string error = "unset";
    GG_REQUIRE(stage(root, entries, error));
    GG_CHECK(contains(error, "one top folder"));
    GG_CHECK(!pathExists(up::stageDir(root.installDir())));
}

GG_TEST("selfupdate", "stage refuses a package without git-gg or ggui")
{
    const std::string gitGg = realGitGg();
    if (gitGg.empty()) {
        markCurrentTestSkipped("git-gg is not next to the test binary");
        return;
    }
    const std::string missing[] = {kGitGg, kGgui};
    for (const std::string& name : missing) {
        TempRoot root;
        std::vector<Entry> entries;
        for (Entry& entry : packageEntries("ggui-1-test", gitGg)) {
            if (entry.name != "ggui-1-test/" + kBinDir + name) {
                entries.push_back(std::move(entry));
            }
        }
        std::string error = "unset";
        GG_REQUIRE(stage(root, entries, error));
        GG_CHECK(contains(error, "The update has no " + name));
        GG_CHECK(!pathExists(up::stageDir(root.installDir())));
    }
}

GG_TEST("selfupdate", "stage refuses data that is not a ZIP file")
{
    TempRoot root;
    std::error_code ec;
    fs::create_directories(root.path, ec);
    std::ofstream(root.zipFile(), std::ios::binary) << std::string(5000, 'x');
    const std::string error = up::stageUpdate(root.installDir(), root.zipFile(), assetFor(root.zipFile()));
    GG_CHECK(contains(error, "not a valid ZIP"));
    GG_CHECK(!pathExists(up::stageDir(root.installDir())));
}

GG_TEST("selfupdate", "stage refuses a git-gg that gives a wrong version answer")
{
#ifdef _WIN32
    markCurrentTestSkipped("the fake git-gg is a shell script");
#else
    TempRoot root;
    std::string error = "unset";
    GG_REQUIRE(stage(root, packageEntries("ggui-1-test", "#!/bin/sh\necho junk\n"), error));
    GG_CHECK(contains(error, "wrong version answer"));
    GG_CHECK(!pathExists(up::stageDir(root.installDir())));

    // A git-gg that cannot start (not a program).
    TempRoot root2;
    GG_REQUIRE(stage(root2, packageEntries("ggui-1-test", "not a program"), error));
    GG_CHECK(!error.empty());
    GG_CHECK(!pathExists(up::stageDir(root2.installDir())));
#endif
}

// Entries of a package plus one extra file; a name is the full name in the ZIP.
const std::string kTop = "ggui-1-test/";

GG_TEST("selfupdate", "stage refuses a file that does not match its checksum")
{
    const std::string gitGg = realGitGg();
    if (gitGg.empty()) {
        markCurrentTestSkipped("git-gg is not next to the test binary");
        return;
    }
    TempRoot root;
    std::string error = "unset";
    // Stored without compression: the first "ggui bytes" in the file is the data of ggui.
    GG_REQUIRE(stagePatched(root, packageEntries("ggui-1-test", gitGg), 0, kTop + kBinDir + kGgui,
                            [](std::string& bytes, size_t, size_t) { bytes[bytes.find("ggui bytes")] = 'h'; }, error));
    GG_CHECK(contains(error, "damaged"));
    GG_CHECK(!pathExists(up::stageDir(root.installDir())));
}

GG_TEST("selfupdate", "stage refuses an entry that holds more data than its header says")
{
    const std::string gitGg = realGitGg();
    if (gitGg.empty()) {
        markCurrentTestSkipped("git-gg is not next to the test binary");
        return;
    }
    TempRoot root;
    std::vector<Entry> entries = packageEntries("ggui-1-test", gitGg);
    entries.push_back({kTop + "extra.bin", std::string(100000, 'a')});
    std::string error = "unset";
    GG_REQUIRE(stagePatched(root, entries, MZ_DEFAULT_LEVEL, kTop + "extra.bin",
                            [](std::string& bytes, size_t central, size_t local) {
                                writeLe(bytes, central + 24, 10, 4);
                                writeLe(bytes, local + 22, 10, 4);
                            },
                            error));
    GG_CHECK(contains(error, "damaged") || contains(error, "more data"));
    GG_CHECK(!pathExists(up::stageDir(root.installDir())));
}

GG_TEST("selfupdate", "stage refuses header sizes over the limit")
{
    const std::string gitGg = realGitGg();
    if (gitGg.empty()) {
        markCurrentTestSkipped("git-gg is not next to the test binary");
        return;
    }
    TempRoot root;
    std::vector<Entry> entries = packageEntries("ggui-1-test", gitGg);
    entries.push_back({kTop + "extra.bin", std::string(1000, 'a')});
    std::string error = "unset";
    GG_REQUIRE(stagePatched(root, entries, MZ_DEFAULT_LEVEL, kTop + "extra.bin",
                            [](std::string& bytes, size_t central, size_t local) {
                                writeLe(bytes, central + 24, 0x50000000u, 4); // 1.25 GiB
                                writeLe(bytes, local + 22, 0x50000000u, 4);
                            },
                            error));
    GG_CHECK(contains(error, "too big"));
    GG_CHECK(!pathExists(up::stageDir(root.installDir())));
}

GG_TEST("selfupdate", "stage refuses a link and a special file")
{
    const std::string gitGg = realGitGg();
    if (gitGg.empty()) {
        markCurrentTestSkipped("git-gg is not next to the test binary");
        return;
    }
    // Symbolic link, FIFO, character device, socket.
    for (const std::uint32_t mode : {0120777u, 0010644u, 0020644u, 0140755u}) {
        TempRoot root;
        std::string error = "unset";
        GG_REQUIRE(stagePatched(root, packageEntries("ggui-1-test", gitGg), MZ_DEFAULT_LEVEL,
                                kTop + kBinDir + kGgui,
                                [mode](std::string& bytes, size_t central, size_t) {
                                    bytes[central + 5] = 3; // made by Unix
                                    writeLe(bytes, central + 38, mode << 16, 4);
                                },
                                error));
        GG_CHECK(contains(error, "link or a special file"));
        GG_CHECK(!pathExists(up::stageDir(root.installDir())));
    }
}

GG_TEST("selfupdate", "stage refuses a folder entry with data")
{
    const std::string gitGg = realGitGg();
    if (gitGg.empty()) {
        markCurrentTestSkipped("git-gg is not next to the test binary");
        return;
    }
    TempRoot root;
    std::vector<Entry> entries = packageEntries("ggui-1-test", gitGg);
    entries.push_back({kTop + "dir/", "x"});
    std::string error = "unset";
    GG_REQUIRE(stage(root, entries, error));
    GG_CHECK(contains(error, "folder with data"));
    GG_CHECK(!pathExists(up::stageDir(root.installDir())));
}

GG_TEST("selfupdate", "stage refuses odd names")
{
    const std::string gitGg = realGitGg();
    if (gitGg.empty()) {
        markCurrentTestSkipped("git-gg is not next to the test binary");
        return;
    }
    // Non-ASCII (UTF-8), a control character, a name that ends in "." and one that ends in a space.
    const std::string names[] = {kTop + "\xC3\xA4.txt", kTop + "a\x01" "b", kTop + "x.", kTop + "x "};
    for (const std::string& name : names) {
        TempRoot root;
        std::vector<Entry> entries = packageEntries("ggui-1-test", gitGg);
        entries.push_back({name, "x"});
        std::string error = "unset";
        GG_REQUIRE(stage(root, entries, error));
        GG_CHECK(contains(error, "unsafe file name"));
        GG_CHECK(!pathExists(up::stageDir(root.installDir())));
    }
}

GG_TEST("selfupdate", "stage refuses a file and a folder with the same name")
{
    const std::string gitGg = realGitGg();
    if (gitGg.empty()) {
        markCurrentTestSkipped("git-gg is not next to the test binary");
        return;
    }
    const std::vector<Entry> clashes[] = {
        {{kTop + "clash", "a"}, {kTop + "clash/inner", "b"}},  // the file first
        {{kTop + "clash/inner", "b"}, {kTop + "clash", "a"}},  // the folder first
        {{kTop + "clash", "a"}, {kTop + "clash/", ""}},        // an explicit folder entry
    };
    for (const std::vector<Entry>& clash : clashes) {
        TempRoot root;
        std::vector<Entry> entries = packageEntries("ggui-1-test", gitGg);
        entries.insert(entries.end(), clash.begin(), clash.end());
        std::string error = "unset";
        GG_REQUIRE(stage(root, entries, error));
        GG_CHECK(!error.empty());
        GG_CHECK(!pathExists(up::stageDir(root.installDir())));
    }
}

} // namespace ggtest
