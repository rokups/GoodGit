// Self-update download backends: the argument lists, the refusal of non-https URLs, the backend
// choice by the environment. No test uses the network.
#include "platform/Fetch.hpp"
#include "tests/Harness.hpp"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>
#include <vector>

#ifdef _WIN32
#include <stdlib.h>
#endif

namespace ggtest {

namespace {

namespace fs = std::filesystem;
namespace up = ggui::update;

int countOf(const std::vector<std::string>& args, const std::string& item)
{
    return static_cast<int>(std::count(args.begin(), args.end(), item));
}

// True when `first` is directly followed by `second`.
bool hasPair(const std::vector<std::string>& args, const std::string& first, const std::string& second)
{
    for (std::size_t i = 0; i + 1 < args.size(); ++i) {
        if (args[i] == first && args[i + 1] == second) {
            return true;
        }
    }
    return false;
}

// Sets an environment variable for the scope (nullptr removes it) and restores the old value.
struct EnvGuard {
    std::string name;
    bool had = false;
    std::string old;

    EnvGuard(const char* variable, const char* value)
        : name(variable)
    {
        if (const char* current = std::getenv(variable)) {
            had = true;
            old = current;
        }
        set(value);
    }

    ~EnvGuard() { set(had ? old.c_str() : nullptr); }

    void set(const char* value) const
    {
#ifdef _WIN32
        _putenv_s(name.c_str(), value != nullptr ? value : "");
#else
        if (value != nullptr) {
            setenv(name.c_str(), value, 1);
        } else {
            unsetenv(name.c_str());
        }
#endif
    }
};

const char* const kUrl = "https://github.com/rokups/GoodGit/releases/download/x/a%20b.zip";

} // namespace

GG_TEST("selfupdate", "fetch curlArgs has the https-only flags and single-element values")
{
    const fs::path out = fs::path("dir with space") / "pkg.zip";
    const auto args = up::curlArgs(kUrl, out, 12345);
    GG_CHECK(!args.empty() && args.front() == "curl");
    GG_CHECK(countOf(args, "--fail") == 1);
    GG_CHECK(countOf(args, "--location") == 1);
    GG_CHECK(hasPair(args, "--proto", "=https"));
    GG_CHECK(hasPair(args, "--proto-redir", "=https"));
    GG_CHECK(hasPair(args, "--max-redirs", "5"));
    GG_CHECK(hasPair(args, "--connect-timeout", "15"));
    GG_CHECK(hasPair(args, "--max-time", "600"));
    GG_CHECK(hasPair(args, "--max-filesize", "12345"));
    GG_CHECK(hasPair(args, "--user-agent", "ggui/" + std::string(GGUI_VERSION)));
    GG_CHECK(hasPair(args, "-o", out.string()));
    GG_CHECK(countOf(args, kUrl) == 1);
    GG_CHECK(args.back() == kUrl);
    // Without a limit there is no --max-filesize.
    GG_CHECK(countOf(up::curlArgs(kUrl, out, 0), "--max-filesize") == 0);
}

GG_TEST("selfupdate", "fetch wgetArgs has the https-only flags and single-element values")
{
    const fs::path out = fs::path("dir with space") / "pkg.zip";
    const auto args = up::wgetArgs(kUrl, out, 12345);
    GG_CHECK(!args.empty() && args.front() == "wget");
    GG_CHECK(countOf(args, "--https-only") == 1);
    GG_CHECK(countOf(args, "--max-redirect=5") == 1);
    GG_CHECK(countOf(args, "--timeout=30") == 1);
    GG_CHECK(countOf(args, "--user-agent=ggui/" + std::string(GGUI_VERSION)) == 1);
    GG_CHECK(countOf(args, "-q") == 1);
    GG_CHECK(hasPair(args, "-O", out.string()));
    GG_CHECK(countOf(args, kUrl) == 1);
    GG_CHECK(args.back() == kUrl);
}

GG_TEST("selfupdate", "fetch refuses a URL that is not https for every backend")
{
    const fs::path out = fs::temp_directory_path() / "gg-fetch-refuse.bin";
    std::error_code ec;
    for (const auto backend : {up::FetchBackend::None, up::FetchBackend::WinHttp, up::FetchBackend::Curl,
             up::FetchBackend::Wget, up::FetchBackend::LibCurl}) {
        const up::FetchFn fetch = up::makeFetch(backend);
        GG_CHECK(static_cast<bool>(fetch));
        for (const char* url : {"http://example.com/a.zip", "ftp://example.com/a", "file:///etc/passwd", "example.com/a",
                 "HTTP://example.com/a", "https:///a.zip", "https://", "https://example.com/a b", ""}) {
            fs::remove(out, ec);
            const std::string error = fetch(url, out, 1000, gg::CancelToken::none());
            GG_CHECK(!error.empty());
            GG_CHECK(!fs::exists(out, ec));
        }
    }
}

GG_TEST("selfupdate", "fetch deletes a stale output file before the download")
{
    const fs::path out = fs::temp_directory_path() / "gg-fetch-partial.bin";
    std::ofstream(out, std::ios::binary) << "stale";
    const std::string error = up::makeFetch(up::FetchBackend::None)("https://example.invalid/a.zip", out, 1000,
        gg::CancelToken::none());
    std::error_code ec;
    GG_CHECK(!error.empty());
    GG_CHECK(!fs::exists(out, ec));
}

GG_TEST("selfupdate", "fetch GGUI_FETCH_BACKEND=none gives None")
{
    {
        const EnvGuard env("GGUI_FETCH_BACKEND", "none");
        GG_CHECK(up::detectFetch() == up::FetchBackend::None);
    }
    {
        // A forced backend that does not exist gives None, never another backend.
        const EnvGuard env("GGUI_FETCH_BACKEND", "libcurl");
        const auto backend = up::detectFetch();
        GG_CHECK(backend == up::FetchBackend::None || backend == up::FetchBackend::LibCurl);
    }
}

GG_TEST("selfupdate", "fetch fetchName")
{
    GG_CHECK_STR_EQ(up::fetchName(up::FetchBackend::None), "none");
    GG_CHECK_STR_EQ(up::fetchName(up::FetchBackend::WinHttp), "WinHTTP");
    GG_CHECK_STR_EQ(up::fetchName(up::FetchBackend::Curl), "curl");
    GG_CHECK_STR_EQ(up::fetchName(up::FetchBackend::Wget), "wget");
    GG_CHECK_STR_EQ(up::fetchName(up::FetchBackend::LibCurl), "libcurl");
}

} // namespace ggtest
