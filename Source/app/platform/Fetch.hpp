// HTTPS download backends for self-update: WinHTTP (Windows), the curl or wget program, or
// libcurl loaded at run time (Linux). No new dependency: libcurl is found with dlopen and used
// through locally declared types, so no curl header or link library is needed.
//
// Rules for every backend: only https:// URLs (anything else is refused before any call), at most
// 5 redirects and only to https, connect and total timeouts (wget: connect and read only), a byte limit, the User-Agent
// "ggui/<version>", a partial output file is deleted on error or cancel, and cancel is supported.
#pragma once

#include "libgg/Cancel.hpp"

#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace ggui::update {

// Downloads `url` to the file `out`. `maxBytes` is the largest size accepted (0 = no limit); a
// longer download fails. Returns "" on success, else an error text; `out` is then deleted. The
// token is gg::CancelToken (libgg/Cancel.hpp): a cancelled token stops the download and kills a
// child process. Blocks until the transfer ends: never call it on the UI thread.
using FetchFn = std::function<std::string(
    const std::string& url, const std::filesystem::path& out, std::uint64_t maxBytes, const gg::CancelToken& cancel)>;

enum class FetchBackend { None, WinHttp, Curl, Wget, LibCurl };

// The first backend that works here. Windows: WinHttp. Linux: the curl program, then the wget
// program, then libcurl.so.4. The environment variable GGUI_FETCH_BACKEND (curl, wget, libcurl,
// none) forces one: it gives None when that backend does not work. Any other value is ignored.
// It runs `curl --version` and `wget --version` (no shell): never call it on the UI thread.
FetchBackend detectFetch();

// The download function of `backend`. None gives a function that always returns an error text.
FetchFn makeFetch(FetchBackend backend);

// A short name for messages: "none", "WinHTTP", "curl", "wget", "libcurl".
const char* fetchName(FetchBackend backend);

// The argument vector (argv[0] is "curl") of the curl backend. Each value is one element.
// `maxBytes` > 0 adds --max-filesize. Pure.
std::vector<std::string> curlArgs(const std::string& url, const std::filesystem::path& out, std::uint64_t maxBytes);

// The argument vector (argv[0] is "wget") of the wget backend. wget has no byte limit: the
// backend checks the size of `out` after the download. `maxBytes` is not used in the arguments. Pure.
std::vector<std::string> wgetArgs(const std::string& url, const std::filesystem::path& out, std::uint64_t maxBytes);

} // namespace ggui::update
