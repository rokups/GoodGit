// Shared helpers of the self-update tests: temp folders, ZIP building and package contents.
#pragma once

#include "libgg/Launch.hpp"
#include "platform/SelfUpdate.hpp"
#include "util/Sha256.hpp"

#include <miniz.h>

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <system_error>
#include <vector>

#ifndef _WIN32
#include <unistd.h>
#endif

namespace ggtest::suhelp {

namespace fs = std::filesystem;
namespace up = ggui::update;

#ifdef _WIN32
inline const char* const kGgui = "ggui.exe";
inline const char* const kGitGg = "git-gg.exe";
inline const std::string kBinDir; // the program files are in the top folder
#else
inline const char* const kGgui = "ggui";
inline const char* const kGitGg = "git-gg";
inline const std::string kBinDir = "bin/";
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

inline std::string readFile(const fs::path& path)
{
    std::ifstream file(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(file), {});
}

inline bool pathExists(const fs::path& path)
{
    std::error_code ec;
    return fs::exists(path, ec);
}

inline bool contains(const std::string& text, const std::string& part)
{
    return text.find(part) != std::string::npos;
}

// The name miniz's writer accepts for `name`: it refuses a leading "/" and a drive letter.
inline std::string writerName(std::string name, bool hasData = false)
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
inline bool writeZip(const fs::path& path, const std::vector<Entry>& entries, int level = MZ_DEFAULT_LEVEL)
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
inline up::Asset assetFor(const fs::path& zipFile)
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
inline std::string realGitGg()
{
    const fs::path self = gg::selfExecutable();
    if (self.empty()) {
        return {};
    }
    return readFile(self.parent_path() / kGitGg);
}

// The files of a package: ggui, git-gg and a file that the update does not use.
inline std::vector<Entry> packageEntries(const std::string& top, const std::string& gitGg)
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

} // namespace ggtest::suhelp
