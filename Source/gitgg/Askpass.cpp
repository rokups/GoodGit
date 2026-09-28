#include "Askpass.hpp"

#include <libgg/LocalSocket.hpp>

#include <cstdlib>
#include <iostream>

namespace gitgg {

int runAskpass(const std::string& prompt)
{
    // GG_ASKPASS_ENDPOINT = "<port>:<token>" (loopback only; set, or this is not askpass mode).
    const std::string value = std::getenv("GG_ASKPASS_ENDPOINT");
    const auto colon = value.find(':');
    if (colon == std::string::npos)
        return 1;
    const int port = std::atoi(value.substr(0, colon).c_str());
    const std::string token = value.substr(colon + 1);
    const auto s = gg::net::connectLoopback(port);
    if (s == gg::net::kInvalid)
        return 1;
    std::string prompt1 = prompt;
    for (auto& c : prompt1)
        if (c == '\n' || c == '\r')
            c = ' ';
    std::string status, answer;
    const bool ok = gg::net::sendAll(s, token + "\n" + prompt1 + "\n") && gg::net::recvLine(s, status)
        && status == "OK" && gg::net::recvLine(s, answer);
    gg::net::closeSocket(s);
    if (!ok)
        return 1;
    std::cout << answer << "\n";
    return 0;
}

} // namespace gitgg
