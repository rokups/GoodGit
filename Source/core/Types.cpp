#include "core/Types.hpp"

#include <algorithm>
#include <cstdio>
#include <ctime>

namespace ggui::core {

bool Oid::isNull() const
{
    if (size == 0)
        return true;
    return std::all_of(bytes.begin(), bytes.begin() + size, [](std::uint8_t b) { return b == 0; });
}

std::string Oid::hex() const
{
    static const char* digits = "0123456789abcdef";
    std::string out;
    out.reserve(size * 2u);
    for (size_t i = 0; i < size; ++i) {
        out.push_back(digits[bytes[i] >> 4]);
        out.push_back(digits[bytes[i] & 15]);
    }
    return out;
}

Oid Oid::fromHex(std::string_view hex)
{
    Oid oid;
    if (hex.size() != 40 && hex.size() != 64)
        return oid;
    auto val = [](char c) -> int {
        if (c >= '0' && c <= '9')
            return c - '0';
        if (c >= 'a' && c <= 'f')
            return c - 'a' + 10;
        if (c >= 'A' && c <= 'F')
            return c - 'A' + 10;
        return -1;
    };
    for (size_t i = 0; i < hex.size() / 2; ++i) {
        const int hi = val(hex[2 * i]);
        const int lo = val(hex[2 * i + 1]);
        if (hi < 0 || lo < 0)
            return Oid{};
        oid.bytes[i] = static_cast<std::uint8_t>(hi * 16 + lo);
    }
    oid.size = static_cast<std::uint8_t>(hex.size() / 2);
    return oid;
}

Oid Oid::fromBytes(const unsigned char* raw, size_t n)
{
    Oid oid;
    oid.size = static_cast<std::uint8_t>(std::min<size_t>(n, 32));
    std::copy(raw, raw + oid.size, oid.bytes.begin());
    return oid;
}

const char* repoStateBadge(RepoState s)
{
    switch (s) {
    case RepoState::None: return "";
    case RepoState::Merging: return "MERGING";
    case RepoState::RebasingInteractive:
    case RepoState::Rebasing: return "REBASING";
    case RepoState::CherryPicking: return "CHERRY-PICKING";
    case RepoState::Reverting: return "REVERTING";
    case RepoState::Bisecting: return "BISECTING";
    }
    return "";
}

const BranchInfo* Snapshot::currentBranch() const
{
    if (headDetached)
        return nullptr;
    return findBranch(headBranch);
}

const BranchInfo* Snapshot::findBranch(const std::string& n) const
{
    for (const auto& b : branches)
        if (b.name == n)
            return &b;
    return nullptr;
}

std::string Snapshot::refsFingerprint() const
{
    std::string fp = head.hex() + (headDetached ? "D" : "A") + headBranch + ";";
    for (const auto& b : branches)
        fp += b.name + "=" + b.target.hex() + ";";
    for (const auto& r : remoteBranches)
        fp += r.name + "=" + r.target.hex() + ";";
    for (const auto& t : tags)
        fp += t.name + "=" + t.target.hex() + ";";
    for (const auto& s : stashes)
        fp += "s" + s.commit.hex() + ";";
    for (const auto& w : worktrees)
        fp += "w" + w.name + "=" + w.head.hex() + ";";
    return fp;
}

std::string formatTime(std::int64_t unixSeconds, bool withSeconds)
{
    const std::time_t t = static_cast<std::time_t>(unixSeconds);
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    char buf[64];
    std::strftime(buf, sizeof(buf), withSeconds ? "%Y-%m-%d %H:%M:%S" : "%Y-%m-%d %H:%M", &tm);
    return buf;
}

} // namespace ggui::core
