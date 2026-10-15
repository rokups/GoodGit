// Self-update release model: parse a GitHub release, pick the asset for this build, and compare
// commit ids. Pure logic, no network or file access.
#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace ggui::update {

struct Asset {
    std::string name;
    std::string url;
    std::string sha256; // lowercase hex; empty when the release gives no sha256 digest
    std::uint64_t size = 0;
};

struct Release {
    std::string commit; // target_commitish
    std::vector<Asset> assets;
};

// Reads a GitHub "release" JSON object. Assets whose state is not "uploaded" are skipped.
// Returns false and sets `error` for malformed JSON. Never throws.
bool parseRelease(std::string_view json, Release& out, std::string& error);

// The asset for `packageSystem` (for example "linux-x86_64" or "windows-msvc"), or null when there
// is none or the asset is not trustworthy (foreign URL, bad digest, zero size).
const Asset* selectAsset(const Release& release, std::string_view packageSystem);

enum class Freshness { UpToDate, Newer, Unknown };

// Compares two 40-hex commit ids (case-insensitive). Anything else gives Unknown.
Freshness compare(std::string_view local, std::string_view release);

// Empty when ggui can update the install of `exe`, else a message for the user. The checks run in
// this order: file name (ggui or ggui.exe), installed by the package manager (Linux, /usr but not
// /usr/local; no disk access up to here), git-gg next to the exe, install folder writable.
std::string installProblem(const std::filesystem::path& exe);

// Where the update is staged: <installDir>/.ggui-update. It holds new/ (the unpacked files), the
// script and result.txt.
std::filesystem::path stageDir(const std::filesystem::path& installDir);

// The text of the update scripts (pure ASCII; the Windows text uses CRLF line ends). Contract: the
// script is in the stage folder and its parent folder is the install folder. Argument 1 is the pid
// of ggui, argument 2 "nostart" skips the restart (tests only). The script waits for the pid to
// exit, swaps ggui and git-gg for the files in new/ (it undoes the swap when a step fails), writes
// "ok" or "failed: <step>" to result.txt (a failed undo adds the line "failed: undo"), and then starts the installed ggui.
std::string windowsUpdateScript();
std::string unixUpdateScript();

// The script file of this platform in `stage`: update.cmd (Windows) or update.sh.
std::filesystem::path scriptPath(const std::filesystem::path& stage);

// Writes the script of this platform into `stage` (creates the folder; update.sh gets mode 0755).
// Returns "" or an error text.
std::string writeScript(const std::filesystem::path& stage);

// The trimmed text of stage/result.txt, after it deletes the stage folder and the *.old files of
// ggui and git-gg (an .old file stays when its live file is missing). A stage without result.txt
// is deleted and gives nullopt only when its script (else the stage folder) is older than 10
// minutes by mtime: a younger stage may belong to a running update and stays. No stage gives nullopt.
std::optional<std::string> takeResult(const std::filesystem::path& installDir);

// Checks, unpacks and tests a downloaded update ZIP (the format on every platform). Steps:
//  1. The size of `zipFile` equals asset.size and its SHA-256 equals asset.sha256 (hex, any case).
//  2. The old stage folder is removed and the ZIP is unpacked into <stage>/unpack. An entry is
//     refused when its name is not a plain relative path (".." or empty parts, a leading "/", a
//     backslash, ":", control or non-ASCII characters, a duplicate, a name that ends in "." or space)
//     or when its Unix mode is not a regular file or a folder. The ZIP has at most 1000 entries and
//     1 GiB of data in total; the bytes written are counted, not only the sizes in the headers.
//     A CRC error is an error. Nothing follows a link: links are not created.
//  3. The ZIP holds exactly one top folder. ggui and git-gg are moved into <stage>/new: from
//     <top>/bin/ on Linux, from <top>/ on Windows, where LICENSE.txt is moved too (when present).
//  4. Linux: both files get mode 0755.
//  5. `<stage>/new/git-gg --version` runs (it is killed after 20 s) and must print "git gg ...".
//  6. <stage>/unpack is deleted.
// Returns "" or an error text. After an error from step 2 on, the whole stage is deleted. A failed
// step 1 leaves the stage as it was. `zipFile` must not be inside the stage.
// Blocks (disk work, a child process): call it from a worker thread, never from the UI thread.
std::string stageUpdate(const std::filesystem::path& installDir, const std::filesystem::path& zipFile,
                        const Asset& asset);

} // namespace ggui::update
