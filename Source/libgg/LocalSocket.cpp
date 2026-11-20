#include "libgg/LocalSocket.hpp"

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
using socklen_t = int;
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

#include <chrono>
#include <cstring>

#ifdef SOCK_CLOEXEC
constexpr int kCloexec = SOCK_CLOEXEC;
#else
constexpr int kCloexec = 0; // Windows: noInherit() does it
#endif

namespace gg::net {

namespace {

bool waitReadable(Socket s, int timeoutMs)
{
    if (timeoutMs < 0)
        return true;
#ifdef _WIN32
    WSAPOLLFD p{static_cast<SOCKET>(s), POLLRDNORM, 0};
    return WSAPoll(&p, 1, timeoutMs) > 0;
#else
    pollfd p{static_cast<int>(s), POLLIN, 0};
    return poll(&p, 1, timeoutMs) > 0;
#endif
}

#ifdef _WIN32
// A child process (git) must not keep an accepted socket open.
Socket noInherit(Socket s)
{
    if (s != kInvalid)
        SetHandleInformation(reinterpret_cast<HANDLE>(s), HANDLE_FLAG_INHERIT, 0);
    return s;
}
#endif

// Created not inheritable from the start: no window for a child process started at the same time.
Socket newSocket()
{
#ifdef _WIN32
    return static_cast<Socket>(::WSASocketW(AF_INET, SOCK_STREAM, 0, nullptr, 0, WSA_FLAG_NO_HANDLE_INHERIT));
#else
    return static_cast<Socket>(::socket(AF_INET, SOCK_STREAM | kCloexec, 0));
#endif
}

} // namespace

bool startup()
{
#ifdef _WIN32
    static bool ok = [] {
        WSADATA data;
        return WSAStartup(MAKEWORD(2, 2), &data) == 0;
    }();
    return ok;
#else
    return true;
#endif
}

Socket listenLoopback(int& port)
{
    startup();
    const auto s = newSocket();
    if (s == kInvalid)
        return kInvalid;
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = 0;
    if (::bind(static_cast<int>(s), reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0
        || ::listen(static_cast<int>(s), 8) != 0) {
        closeSocket(s);
        return kInvalid;
    }
    socklen_t len = sizeof(addr);
    ::getsockname(static_cast<int>(s), reinterpret_cast<sockaddr*>(&addr), &len);
    port = ntohs(addr.sin_port);
    return s;
}

Socket acceptWithTimeout(Socket listener, int timeoutMs)
{
    if (!waitReadable(listener, timeoutMs))
        return kInvalid;
#ifdef _WIN32
    return noInherit(static_cast<Socket>(::accept(static_cast<int>(listener), nullptr, nullptr)));
#else
    return static_cast<Socket>(::accept4(static_cast<int>(listener), nullptr, nullptr, SOCK_CLOEXEC));
#endif
}

Socket connectLoopback(int port)
{
    startup();
    const auto s = newSocket();
    if (s == kInvalid)
        return kInvalid;
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = htons(static_cast<std::uint16_t>(port));
    if (::connect(static_cast<int>(s), reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
        closeSocket(s);
        return kInvalid;
    }
    return s;
}

bool sendAll(Socket s, const std::string& data)
{
    size_t off = 0;
    while (off < data.size()) {
        #ifdef MSG_NOSIGNAL
        const int flags = MSG_NOSIGNAL; // a closed peer is an error, not SIGPIPE
#else
        const int flags = 0;
#endif
        const auto n = ::send(static_cast<int>(s), data.data() + off, static_cast<int>(data.size() - off), flags);
        if (n <= 0)
            return false;
        off += static_cast<size_t>(n);
    }
    return true;
}

bool recvLine(Socket s, std::string& line, int timeoutMs)
{
    line.clear();
    char c = 0;
    const auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
    for (;;) {
        int left = -1; // no deadline
        if (timeoutMs >= 0) {
            left = static_cast<int>(std::chrono::duration_cast<std::chrono::milliseconds>(end - std::chrono::steady_clock::now()).count());
            if (left < 0)
                return false;
        }
        if (!waitReadable(s, left))
            return false;
        const auto n = ::recv(static_cast<int>(s), &c, 1, 0);
        if (n <= 0)
            return !line.empty();
        if (c == '\n')
            return true;
        if (line.size() >= kMaxLine)
            return false;
        line.push_back(c);
    }
}

bool recvExact(Socket s, std::string& data, size_t size)
{
    data.assign(size, '\0');
    size_t off = 0;
    while (off < size) {
        const auto n = ::recv(static_cast<int>(s), data.data() + off, static_cast<int>(size - off), 0);
        if (n <= 0)
            return false;
        off += static_cast<size_t>(n);
    }
    return true;
}

bool peerClosed(Socket s)
{
    if (!waitReadable(s, 0))
        return false;
    char c = 0;
    return ::recv(static_cast<int>(s), &c, 1, MSG_PEEK) <= 0;
}

void closeSocket(Socket s)
{
    if (s == kInvalid)
        return;
#ifdef _WIN32
    ::closesocket(static_cast<SOCKET>(s));
#else
    ::close(static_cast<int>(s));
#endif
}

} // namespace gg::net
