#include "platform/Fetch.hpp"

#include "libgg/GitRunner.hpp"
#include "util/BuildInfo.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <memory>
#include <string_view>
#include <system_error>
#include <utility>

#ifdef _WIN32
#include <windows.h>

#include <winhttp.h>
#else
#include <dlfcn.h>
#endif

namespace ggui::update {

namespace fs = std::filesystem;

namespace {

constexpr int kMaxRedirects = 5;
constexpr int kConnectTimeoutSeconds = 15;
constexpr int kTotalTimeoutSeconds = 600;
constexpr int kWgetTimeoutSeconds = 30;

std::string pathUtf8(const fs::path& path)
{
    const auto text = path.u8string();
    return std::string(text.begin(), text.end());
}

std::string userAgent()
{
    return "ggui/" + buildInfo().version;
}

char lowerAscii(char c)
{
    return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
}

// "" when `url` is an https URL with a host and without a space or control character.
std::string checkUrl(const std::string& url)
{
    constexpr std::string_view prefix = "https://";
    if (url.size() <= prefix.size() ||
        !std::equal(prefix.begin(), prefix.end(), url.begin(), [](char a, char b) { return a == lowerAscii(b); })) {
        return "Only https:// URLs are allowed.";
    }
    for (const char c : url) {
        if (static_cast<unsigned char>(c) <= ' ' || static_cast<unsigned char>(c) >= 0x7f) {
            return "The URL has a character that is not allowed.";
        }
    }
    if (url[prefix.size()] == '/' || url[prefix.size()] == '?' || url[prefix.size()] == '#') {
        return "The URL has no host.";
    }
    return {};
}

void removeQuietly(const fs::path& path)
{
    std::error_code ec;
    fs::remove(path, ec);
}

// What the programs and libraries below share: write the bytes to a file, count them, stop at the
// limit or on cancel. `reason` is empty until the sink stops the transfer.
class ByteSink {
public:
    ByteSink(const fs::path& out, std::uint64_t maxBytes, const gg::CancelToken& cancel)
        : m_file(out, std::ios::binary | std::ios::trunc)
        , m_max(maxBytes)
        , m_cancel(cancel)
    {
        if (!m_file) {
            reason = "Cannot write the download file.";
        }
    }

    bool failed() const { return !reason.empty(); }

    // True to go on.
    bool write(const char* data, std::uint64_t size)
    {
        if (failed()) {
            return false;
        }
        if (m_cancel.cancelled()) {
            reason = "Download cancelled.";
            return false;
        }
        if (m_max > 0 && (m_written > m_max || size > m_max - m_written)) {
            reason = "The download is larger than the size limit.";
            return false;
        }
        m_file.write(data, static_cast<std::streamsize>(size));
        if (!m_file) {
            reason = "Cannot write the download file.";
            return false;
        }
        m_written += size;
        return true;
    }

    bool cancelled()
    {
        if (!failed() && m_cancel.cancelled()) {
            reason = "Download cancelled.";
        }
        return failed();
    }

    // Flushes the file; false (and a reason) when that fails.
    bool finish()
    {
        m_file.close();
        if (!failed() && m_file.fail()) {
            reason = "Cannot write the download file.";
        }
        return !failed();
    }

    std::string reason;

private:
    std::ofstream m_file;
    std::uint64_t m_max;
    gg::CancelToken m_cancel;
    std::uint64_t m_written = 0;
};

// ---- curl and wget programs -----------------------------------------------------------------

// Runs `args` (no shell) until the end or cancel. "" on exit code 0.
std::string runProgram(const std::vector<std::string>& args, const gg::CancelToken& cancel)
{
    gg::RunRequest request;
    request.args = args;
    request.cancel = cancel;
    request.gitEnvironment = false;
    const gg::RunResult result = gg::run(request);
    if (result.cancelled) {
        return "Download cancelled.";
    }
    if (result.startFailed) {
        return "Cannot start " + args.front() + ".";
    }
    if (result.exitCode != 0) {
        const std::string message = result.message();
        return message.empty() ? args.front() + " failed." : message;
    }
    return {};
}

bool programWorks(const std::string& program)
{
    gg::RunRequest request;
    request.args = {program, "--version"};
    request.gitEnvironment = false;
    return gg::run(request).ok();
}

// ---- libcurl (Linux, loaded with dlopen) ----------------------------------------------------

#ifndef _WIN32

// Option and result values, copied from curl/curl.h (checked against libcurl 8.x). Option number
// = base + id: long 0, object pointer 10000, function pointer 20000, curl_off_t 30000. These are
// stable ABI values.
namespace curlc {
constexpr int kOk = 0;
constexpr int kUnknownOption = 48;       // CURLE_UNKNOWN_OPTION
constexpr int kWriteError = 23;          // CURLE_WRITE_ERROR
constexpr int kAbortedByCallback = 42;   // CURLE_ABORTED_BY_CALLBACK
constexpr int kGlobalDefault = 3;        // CURL_GLOBAL_DEFAULT
constexpr int kWriteData = 10001;        // CURLOPT_WRITEDATA
constexpr int kUrl = 10002;              // CURLOPT_URL
constexpr int kTimeout = 13;             // CURLOPT_TIMEOUT
constexpr int kUserAgent = 10018;        // CURLOPT_USERAGENT
constexpr int kWriteFunction = 20011;    // CURLOPT_WRITEFUNCTION
constexpr int kNoProgress = 43;          // CURLOPT_NOPROGRESS
constexpr int kFailOnError = 45;         // CURLOPT_FAILONERROR
constexpr int kFollowLocation = 52;      // CURLOPT_FOLLOWLOCATION
constexpr int kProgressFunction = 20056; // CURLOPT_PROGRESSFUNCTION (old, doubles)
constexpr int kProgressData = 10057;     // CURLOPT_PROGRESSDATA = CURLOPT_XFERINFODATA
constexpr int kMaxRedirs = 68;           // CURLOPT_MAXREDIRS
constexpr int kNoSignal = 99;            // CURLOPT_NOSIGNAL
constexpr int kConnectTimeout = 78;      // CURLOPT_CONNECTTIMEOUT
constexpr int kProtocols = 181;          // CURLOPT_PROTOCOLS (bit mask, curl < 7.85)
constexpr int kRedirProtocols = 182;     // CURLOPT_REDIR_PROTOCOLS (bit mask, curl < 7.85)
constexpr int kXferInfoFunction = 20219; // CURLOPT_XFERINFOFUNCTION (curl >= 7.32)
constexpr int kProtocolsStr = 10318;     // CURLOPT_PROTOCOLS_STR (curl >= 7.85)
constexpr int kRedirProtocolsStr = 10319; // CURLOPT_REDIR_PROTOCOLS_STR (curl >= 7.85)
constexpr int kMaxFileSizeLarge = 30117; // CURLOPT_MAXFILESIZE_LARGE
constexpr int kInfoResponseCode = 0x200000 + 2; // CURLINFO_RESPONSE_CODE
constexpr long kProtoHttps = 2;          // CURLPROTO_HTTPS
} // namespace curlc

struct CurlApi {
    int (*globalInit)(long) = nullptr;
    void* (*easyInit)() = nullptr;
    int (*setopt)(void*, int, ...) = nullptr;
    int (*perform)(void*) = nullptr;
    int (*getinfo)(void*, int, ...) = nullptr;
    void (*cleanup)(void*) = nullptr;
    const char* (*strerror)(int) = nullptr;
    bool ok = false;
};

CurlApi loadCurlApi()
{
    CurlApi api;
    // The library stays loaded until the process ends.
    void* lib = dlopen("libcurl.so.4", RTLD_NOW | RTLD_LOCAL);
    if (lib == nullptr) {
        return api;
    }
    api.globalInit = reinterpret_cast<decltype(api.globalInit)>(dlsym(lib, "curl_global_init"));
    api.easyInit = reinterpret_cast<decltype(api.easyInit)>(dlsym(lib, "curl_easy_init"));
    api.setopt = reinterpret_cast<decltype(api.setopt)>(dlsym(lib, "curl_easy_setopt"));
    api.perform = reinterpret_cast<decltype(api.perform)>(dlsym(lib, "curl_easy_perform"));
    api.getinfo = reinterpret_cast<decltype(api.getinfo)>(dlsym(lib, "curl_easy_getinfo"));
    api.cleanup = reinterpret_cast<decltype(api.cleanup)>(dlsym(lib, "curl_easy_cleanup"));
    api.strerror = reinterpret_cast<decltype(api.strerror)>(dlsym(lib, "curl_easy_strerror"));
    api.ok = api.globalInit && api.easyInit && api.setopt && api.perform && api.getinfo && api.cleanup &&
        api.strerror;
    if (api.ok) {
        api.ok = api.globalInit(curlc::kGlobalDefault) == curlc::kOk;
    }
    return api;
}

const CurlApi& curlApi()
{
    static const CurlApi api = loadCurlApi(); // thread-safe one-time load
    return api;
}

std::size_t curlWrite(char* data, std::size_t size, std::size_t count, void* user)
{
    auto* sink = static_cast<ByteSink*>(user);
    const std::size_t total = size * count;
    return sink->write(data, total) ? total : 0;
}

int curlXferInfo(void* user, std::int64_t, std::int64_t, std::int64_t, std::int64_t)
{
    return static_cast<ByteSink*>(user)->cancelled() ? 1 : 0;
}

int curlProgress(void* user, double, double, double, double)
{
    return static_cast<ByteSink*>(user)->cancelled() ? 1 : 0;
}

struct CurlHandle {
    const CurlApi& api;
    void* handle;
    ~CurlHandle() { api.cleanup(handle); }
};

std::string libCurlFetch(const std::string& url, const fs::path& out, std::uint64_t maxBytes, const gg::CancelToken& cancel)
{
    const CurlApi& api = curlApi();
    if (!api.ok) {
        return "libcurl is not available.";
    }
    void* easy = api.easyInit();
    if (easy == nullptr) {
        return "libcurl cannot start.";
    }
    const CurlHandle guard{api, easy};
    ByteSink sink(out, maxBytes, cancel);
    if (sink.failed()) {
        return sink.reason;
    }
    const std::string agent = userAgent();

    // Every option must be accepted: a refused option must not leave a weaker setting.
    int rc = api.setopt(easy, curlc::kUrl, url.c_str());
    // Newer libcurl takes a string list; libcurl before 7.85 answers "unknown option" and takes a mask.
    if (rc == curlc::kOk) {
        rc = api.setopt(easy, curlc::kProtocolsStr, "https");
        if (rc == curlc::kUnknownOption) {
            rc = api.setopt(easy, curlc::kProtocols, curlc::kProtoHttps);
        }
    }
    if (rc == curlc::kOk) {
        rc = api.setopt(easy, curlc::kRedirProtocolsStr, "https");
        if (rc == curlc::kUnknownOption) {
            rc = api.setopt(easy, curlc::kRedirProtocols, curlc::kProtoHttps);
        }
    }
    if (rc == curlc::kOk) {
        rc = api.setopt(easy, curlc::kFollowLocation, 1L);
    }
    if (rc == curlc::kOk) {
        rc = api.setopt(easy, curlc::kMaxRedirs, static_cast<long>(kMaxRedirects));
    }
    if (rc == curlc::kOk) {
        rc = api.setopt(easy, curlc::kUserAgent, agent.c_str());
    }
    if (rc == curlc::kOk) {
        rc = api.setopt(easy, curlc::kConnectTimeout, static_cast<long>(kConnectTimeoutSeconds));
    }
    if (rc == curlc::kOk) {
        rc = api.setopt(easy, curlc::kTimeout, static_cast<long>(kTotalTimeoutSeconds));
    }
    if (rc == curlc::kOk) {
        rc = api.setopt(easy, curlc::kFailOnError, 1L);
    }
    if (rc == curlc::kOk) {
        rc = api.setopt(easy, curlc::kNoSignal, 1L); // no SIGALRM in a worker thread
    }
    if (rc == curlc::kOk && maxBytes > 0) {
        // Stops early when the server states a size; the write callback counts the real bytes.
        rc = api.setopt(easy, curlc::kMaxFileSizeLarge, static_cast<std::int64_t>(maxBytes));
    }
    if (rc == curlc::kOk) {
        rc = api.setopt(easy, curlc::kWriteFunction, &curlWrite);
    }
    if (rc == curlc::kOk) {
        rc = api.setopt(easy, curlc::kWriteData, static_cast<void*>(&sink));
    }
    if (rc == curlc::kOk) {
        rc = api.setopt(easy, curlc::kNoProgress, 0L);
    }
    if (rc == curlc::kOk) {
        rc = api.setopt(easy, curlc::kProgressData, static_cast<void*>(&sink));
    }
    if (rc == curlc::kOk) {
        // curl before 7.32 has only the older progress function (it takes doubles).
        rc = api.setopt(easy, curlc::kXferInfoFunction, &curlXferInfo);
        if (rc == curlc::kUnknownOption) {
            rc = api.setopt(easy, curlc::kProgressFunction, &curlProgress);
        }
    }
    if (rc != curlc::kOk) {
        return std::string("libcurl refused an option: ") + api.strerror(rc);
    }

    rc = api.perform(easy);
    if (sink.failed()) {
        return sink.reason;
    }
    if (rc != curlc::kOk) {
        return std::string("Download failed: ") + api.strerror(rc);
    }
    long status = 0;
    if (api.getinfo(easy, curlc::kInfoResponseCode, &status) != curlc::kOk || status != 200) {
        return "Download failed: HTTP status " + std::to_string(status) + ".";
    }
    if (!sink.finish()) {
        return sink.reason;
    }
    return {};
}

#endif // !_WIN32

// ---- WinHTTP (Windows) ----------------------------------------------------------------------

#ifdef _WIN32

// UTF-8 to UTF-16, strict. False when `s` is not valid UTF-8.
bool widenUtf8(const std::string& s, std::wstring& out)
{
    out.clear();
    if (s.empty()) {
        return true;
    }
    const int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s.c_str(), static_cast<int>(s.size()), nullptr, 0);
    if (n <= 0) {
        return false;
    }
    out.assign(static_cast<std::size_t>(n), L'\0');
    return MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s.c_str(), static_cast<int>(s.size()), out.data(), n) == n;
}

struct WinHttpHandle {
    HINTERNET handle = nullptr;
    WinHttpHandle() = default;
    explicit WinHttpHandle(HINTERNET h)
        : handle(h)
    {
    }
    WinHttpHandle(const WinHttpHandle&) = delete;
    WinHttpHandle& operator=(const WinHttpHandle&) = delete;
    ~WinHttpHandle()
    {
        if (handle != nullptr) {
            WinHttpCloseHandle(handle);
        }
    }
    explicit operator bool() const { return handle != nullptr; }
};

std::string winHttpFetch(const std::string& url, const fs::path& out, std::uint64_t maxBytes, const gg::CancelToken& cancel)
{
    std::wstring wideUrl;
    if (!widenUtf8(url, wideUrl)) {
        return "The URL is not valid UTF-8.";
    }
    std::wstring wideAgent;
    if (!widenUtf8(userAgent(), wideAgent)) {
        return "The User-Agent is not valid.";
    }

    // A length of -1 makes WinHttpCrackUrl point into `wideUrl`.
    URL_COMPONENTS parts{};
    parts.dwStructSize = sizeof(parts);
    parts.dwSchemeLength = static_cast<DWORD>(-1);
    parts.dwHostNameLength = static_cast<DWORD>(-1);
    parts.dwUrlPathLength = static_cast<DWORD>(-1);
    parts.dwExtraInfoLength = static_cast<DWORD>(-1);
    if (!WinHttpCrackUrl(wideUrl.c_str(), static_cast<DWORD>(wideUrl.size()), 0, &parts)) {
        return "The URL is not valid.";
    }
    if (parts.nScheme != INTERNET_SCHEME_HTTPS || parts.dwHostNameLength == 0) {
        return "Only https:// URLs are allowed.";
    }
    const std::wstring host(parts.lpszHostName, parts.dwHostNameLength);
    std::wstring target(parts.lpszUrlPath, parts.dwUrlPathLength);
    target.append(parts.lpszExtraInfo, parts.dwExtraInfoLength);
    if (target.empty()) {
        target = L"/";
    }

    // WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY (4) needs Windows 8.1; older systems use the default proxy.
    constexpr DWORD kAutomaticProxy = 4;
    WinHttpHandle session(WinHttpOpen(wideAgent.c_str(), kAutomaticProxy, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0));
    if (!session) {
        session.handle = WinHttpOpen(
            wideAgent.c_str(), WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    }
    if (!session) {
        return "WinHTTP cannot start.";
    }
    const int connectMs = kConnectTimeoutSeconds * 1000;
    const int ioMs = kWgetTimeoutSeconds * 1000;
    if (!WinHttpSetTimeouts(session.handle, connectMs, connectMs, ioMs, ioMs)) {
        return "WinHTTP refused the timeouts.";
    }
    const WinHttpHandle connection(WinHttpConnect(session.handle, host.c_str(), parts.nPort, 0));
    if (!connection) {
        return "Cannot connect to the server.";
    }
    const WinHttpHandle request(WinHttpOpenRequest(connection.handle, L"GET", target.c_str(), nullptr, WINHTTP_NO_REFERER,
        WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE));
    if (!request) {
        return "Cannot open the request.";
    }
    // Every option must be accepted: a refused option must not leave a weaker setting.
    DWORD policy = WINHTTP_OPTION_REDIRECT_POLICY_DISALLOW_HTTPS_TO_HTTP;
    DWORD maxRedirects = static_cast<DWORD>(kMaxRedirects);
    if (!WinHttpSetOption(request.handle, WINHTTP_OPTION_REDIRECT_POLICY, &policy, sizeof(policy)) ||
        !WinHttpSetOption(request.handle, WINHTTP_OPTION_MAX_HTTP_AUTOMATIC_REDIRECTS, &maxRedirects, sizeof(maxRedirects))) {
        return "WinHTTP refused the redirect limits.";
    }

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(kTotalTimeoutSeconds);
    if (cancel.cancelled()) {
        return "Download cancelled.";
    }
    if (!WinHttpSendRequest(request.handle, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) ||
        !WinHttpReceiveResponse(request.handle, nullptr)) {
        return cancel.cancelled() ? "Download cancelled." : "Download failed: no answer from the server.";
    }
    DWORD status = 0;
    DWORD statusSize = sizeof(status);
    if (!WinHttpQueryHeaders(request.handle, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
            WINHTTP_HEADER_NAME_BY_INDEX, &status, &statusSize, WINHTTP_NO_HEADER_INDEX) ||
        status != 200) {
        return "Download failed: HTTP status " + std::to_string(status) + ".";
    }

    ByteSink sink(out, maxBytes, cancel);
    if (sink.failed()) {
        return sink.reason;
    }
    std::vector<char> buffer(64 * 1024);
    for (;;) {
        if (sink.cancelled()) {
            return sink.reason;
        }
        if (std::chrono::steady_clock::now() > deadline) {
            return "Download failed: the time limit is over.";
        }
        DWORD read = 0;
        if (!WinHttpReadData(request.handle, buffer.data(), static_cast<DWORD>(buffer.size()), &read)) {
            return "Download failed: read error.";
        }
        if (read == 0) {
            break;
        }
        if (!sink.write(buffer.data(), read)) {
            return sink.reason;
        }
    }
    if (!sink.finish()) {
        return sink.reason;
    }
    return {};
}

#endif // _WIN32

std::string unavailable(const char* name)
{
    return std::string(name) + " is not available on this system.";
}

// Wraps one backend with the checks they share: the URL, a clean output file before and after,
// and the size limit of the written file.
FetchFn guarded(FetchFn backend)
{
    return [backend = std::move(backend)](
               const std::string& url, const fs::path& out, std::uint64_t maxBytes, const gg::CancelToken& cancel) {
        std::string error = checkUrl(url);
        if (!error.empty()) {
            return error;
        }
        if (out.empty()) {
            return std::string("No output file.");
        }
        removeQuietly(out);
        error = backend(url, out, maxBytes, cancel);
        if (error.empty()) {
            std::error_code ec;
            const std::uintmax_t size = fs::file_size(out, ec);
            if (ec) {
                error = "The download file is missing.";
            } else if (maxBytes > 0 && size > maxBytes) {
                error = "The download is larger than the size limit.";
            }
        }
        if (!error.empty()) {
            removeQuietly(out);
        }
        return error;
    };
}

} // namespace

std::vector<std::string> curlArgs(const std::string& url, const fs::path& out, std::uint64_t maxBytes)
{
    std::vector<std::string> args = {"curl", "--disable", "--globoff", "--fail", "--silent", "--show-error", "--location", "--proto", "=https",
        "--proto-redir", "=https", "--max-redirs", std::to_string(kMaxRedirects), "--connect-timeout",
        std::to_string(kConnectTimeoutSeconds), "--max-time", std::to_string(kTotalTimeoutSeconds), "--user-agent",
        userAgent()};
    if (maxBytes > 0) {
        args.push_back("--max-filesize");
        args.push_back(std::to_string(maxBytes));
    }
    args.push_back("-o");
    args.push_back(pathUtf8(out));
    args.push_back(url);
    return args;
}

std::vector<std::string> wgetArgs(const std::string& url, const fs::path& out, std::uint64_t)
{
    return {"wget", "--https-only", "--tries=1", "--max-redirect=" + std::to_string(kMaxRedirects),
        "--timeout=" + std::to_string(kWgetTimeoutSeconds), "--user-agent=" + userAgent(), "-q", "-O", pathUtf8(out), url};
}

FetchBackend detectFetch()
{
    const char* forced = std::getenv("GGUI_FETCH_BACKEND");
    const std::string choice = forced != nullptr ? forced : "";
    if (choice == "none") {
        return FetchBackend::None;
    }
    const bool any = choice != "curl" && choice != "wget" && choice != "libcurl";
#ifdef _WIN32
    if (any) {
        return FetchBackend::WinHttp;
    }
#endif
    if ((any || choice == "curl") && programWorks("curl")) {
        return FetchBackend::Curl;
    }
    if ((any || choice == "wget") && programWorks("wget")) {
        return FetchBackend::Wget;
    }
#ifndef _WIN32
    if ((any || choice == "libcurl") && curlApi().ok) {
        return FetchBackend::LibCurl;
    }
#endif
    return FetchBackend::None;
}

FetchFn makeFetch(FetchBackend backend)
{
    switch (backend) {
    case FetchBackend::Curl:
        return guarded([](const std::string& url, const fs::path& out, std::uint64_t maxBytes, const gg::CancelToken& cancel) {
            return runProgram(curlArgs(url, out, maxBytes), cancel);
        });
    case FetchBackend::Wget:
        // wget has no byte limit: `guarded` checks the size of the file after the download.
        return guarded([](const std::string& url, const fs::path& out, std::uint64_t maxBytes, const gg::CancelToken& cancel) {
            return runProgram(wgetArgs(url, out, maxBytes), cancel);
        });
    case FetchBackend::LibCurl:
#ifndef _WIN32
        return guarded(libCurlFetch);
#else
        break;
#endif
    case FetchBackend::WinHttp:
#ifdef _WIN32
        return guarded(winHttpFetch);
#else
        break;
#endif
    case FetchBackend::None:
        break;
    }
    const std::string name = fetchName(backend);
    return guarded([name](const std::string&, const fs::path&, std::uint64_t, const gg::CancelToken&) {
        return unavailable(name.c_str());
    });
}

const char* fetchName(FetchBackend backend)
{
    switch (backend) {
    case FetchBackend::WinHttp:
        return "WinHTTP";
    case FetchBackend::Curl:
        return "curl";
    case FetchBackend::Wget:
        return "wget";
    case FetchBackend::LibCurl:
        return "libcurl";
    case FetchBackend::None:
        break;
    }
    return "none";
}

} // namespace ggui::update
