// Minimal loopback TCP helpers for the askpass bridge (REBUILD_PLAN §4.8, T1): ggui listens on
// 127.0.0.1, git-gg (run by git as GIT_ASKPASS / SSH_ASKPASS) connects with a per-session token.
#pragma once

#include <cstdint>
#include <string>

namespace gg::net {

using Socket = std::intptr_t;
constexpr Socket kInvalid = -1;

bool startup();
// Listens on 127.0.0.1 with an OS-assigned port.
Socket listenLoopback(int& port);
// Waits up to `timeoutMs` for a connection (kInvalid on timeout).
Socket acceptWithTimeout(Socket listener, int timeoutMs);
Socket connectLoopback(int port);
bool sendAll(Socket s, const std::string& data);
// Reads until '\n' (not included) or EOF.
bool recvLine(Socket s, std::string& line, int timeoutMs = -1);
void closeSocket(Socket s);

} // namespace gg::net
