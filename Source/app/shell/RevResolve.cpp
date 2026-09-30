#include "shell/RevResolve.hpp"

#include "panels/HistoryPanel.hpp"
#include "shell/Session.hpp"

#include <libgg/GitRunner.hpp>

#include <cctype>

namespace ggui {

namespace {

bool isHex(const std::string& s)
{
    if (s.empty())
        return false;
    for (char c : s)
        if (!std::isxdigit(static_cast<unsigned char>(c)))
            return false;
    return true;
}

std::string lower(std::string s)
{
    for (char& c : s)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

// The commit a bare name (no ~ or ^ suffix) stands for.
const core::HistoryRow* resolveBase(const core::Snapshot& snap, const std::vector<core::HistoryRow>& rows,
    const RowLookup& lookup, std::string name)
{
    if (name.empty())
        return nullptr;
    if (name == "HEAD" || name == "@")
        return snap.headUnborn || snap.head.isNull() ? nullptr : lookup(snap.head);
    const size_t fullLen = snap.head.isNull() ? 40 : snap.head.size * 2u;
    if (name.size() == fullLen && isHex(name)) {
        const core::Oid id = core::Oid::fromHex(name);
        if (!id.isNull())
            return lookup(id);
    }
    auto strip = [&](const char* prefix) {
        const std::string p = prefix;
        if (name.compare(0, p.size(), p) == 0)
            name.erase(0, p.size());
    };
    strip("refs/");
    const bool heads = name.rfind("heads/", 0) == 0, tags = name.rfind("tags/", 0) == 0, remotes = name.rfind("remotes/", 0) == 0;
    strip("heads/");
    strip("tags/");
    strip("remotes/");
    if (!tags && !remotes)
        for (const auto& b : snap.branches)
            if (b.name == name)
                return lookup(b.target);
    if (!heads && !tags)
        for (const auto& b : snap.remoteBranches)
            if (b.name == name)
                return lookup(b.target);
    if (!heads && !remotes)
        for (const auto& t : snap.tags)
            if (t.name == name)
                return lookup(t.target);
    // A unique id prefix.
    if (name.size() >= 4 && isHex(name)) {
        const std::string prefix = lower(name);
        const core::HistoryRow* found = nullptr;
        for (const auto& r : rows) {
            const bool match = prefix.size() <= r.shortId.size() ? r.shortId.compare(0, prefix.size(), prefix) == 0
                                                                 : r.id.hex().compare(0, prefix.size(), prefix) == 0;
            if (!match)
                continue;
            if (found && found->id != r.id)
                return nullptr; // ambiguous
            found = &r;
        }
        return found;
    }
    return nullptr;
}

// The commit a bare ref name (HEAD, branch, remote branch, tag) or a full id names, loaded or not.
core::Oid refTarget(const core::Snapshot& snap, std::string name)
{
    if (name.empty())
        return {};
    if (name == "HEAD" || name == "@")
        return snap.headUnborn ? core::Oid{} : snap.head;
    const size_t fullLen = snap.head.isNull() ? 40 : snap.head.size * 2u;
    if (name.size() == fullLen && isHex(name))
        return core::Oid::fromHex(name);
    if (name.compare(0, 5, "refs/") == 0)
        name.erase(0, 5);
    const bool heads = name.rfind("heads/", 0) == 0, tags = name.rfind("tags/", 0) == 0, remotes = name.rfind("remotes/", 0) == 0;
    if (heads)
        name.erase(0, 6);
    else if (tags)
        name.erase(0, 5);
    else if (remotes)
        name.erase(0, 8);
    if (!tags && !remotes)
        for (const auto& b : snap.branches)
            if (b.name == name)
                return b.target;
    if (!heads && !tags)
        for (const auto& b : snap.remoteBranches)
            if (b.name == name)
                return b.target;
    if (!heads && !remotes)
        for (const auto& t : snap.tags)
            if (t.name == name)
                return t.target;
    return {};
}

} // namespace

const core::HistoryRow* resolveRev(const core::Snapshot& snap, const std::vector<core::HistoryRow>& rows,
    const RowLookup& lookup, const std::string& text)
{
    size_t end = 0;
    while (end < text.size() && text[end] != '~' && text[end] != '^')
        ++end;
    const core::HistoryRow* row = resolveBase(snap, rows, lookup, text.substr(0, end));
    // Suffixes: ~N (N-th first parent, default 1), ^ / ^N (N-th parent, default 1; ^0 the commit).
    size_t i = end;
    while (row && i < text.size()) {
        const char op = text[i++];
        size_t n = 0;
        bool digits = false;
        while (i < text.size() && std::isdigit(static_cast<unsigned char>(text[i])) && n < 100000) {
            n = n * 10 + static_cast<size_t>(text[i++] - '0');
            digits = true;
        }
        if (op != '~' && op != '^')
            return nullptr; // e.g. ^{tree}
        if (op == '^') {
            if (!digits)
                n = 1;
            if (n == 0)
                continue;
            row = n <= row->parents.size() ? lookup(row->parents[n - 1]) : nullptr;
        } else {
            if (!digits)
                n = 1;
            for (; row && n > 0; --n)
                row = row->parents.empty() ? nullptr : lookup(row->parents.front());
        }
    }
    return i >= text.size() ? row : nullptr;
}

const core::HistoryRow* resolveRev(Session& session, const std::string& text)
{
    const auto snap = session.snapshot();
    if (!snap)
        return nullptr;
    return resolveRev(*snap, session.history().rows(),
        [&session](const core::Oid& id) { return session.history().row(id); }, text);
}

std::string commitLine(const core::HistoryRow& row, size_t maxChars)
{
    std::string subject = row.subject;
    if (maxChars && subject.size() > maxChars) {
        size_t cut = maxChars;
        while (cut > 0 && (static_cast<unsigned char>(subject[cut]) & 0xC0) == 0x80)
            --cut; // not inside a UTF-8 sequence
        subject = subject.substr(0, cut) + "\xE2\x80\xA6";
    }
    return row.shortId + " " + subject;
}

std::string commitLine(Session& session, const core::Oid& id, size_t maxChars)
{
    if (const core::HistoryRow* row = session.history().row(id))
        return commitLine(*row, maxChars);
    return session.shortId(id); // not loaded
}

Field commitField(Session& session, const std::string& id, const std::string& label, const std::string& prefill,
    const std::string& emptyLabel, const core::Oid& emptyTarget)
{
    Field f{Field::Commit, id, label};
    f.text = prefill;
    Session* s = &session;
    f.resolve = [s, emptyLabel, emptyTarget](const std::string& text) {
        CommitPreview p;
        const std::string trimmed = gg::trim(text);
        if (trimmed.empty()) {
            if (emptyTarget.isNull() || emptyLabel.empty()) {
                p.message = "Enter a branch, tag or commit";
                return p;
            }
            p.prefix = emptyLabel + ": ";
            if (const core::HistoryRow* row = s->history().row(emptyTarget)) {
                p.found = true;
                p.shortId = row->shortId;
                p.subject = row->subject;
            } else {
                p.message = emptyLabel;
            }
            return p;
        }
        if (const core::HistoryRow* row = resolveRev(*s, trimmed)) {
            p.found = true;
            p.shortId = row->shortId;
            p.subject = row->subject;
        } else if (const auto snap = s->snapshot(); snap && !refTarget(*snap, trimmed).isNull()) {
            // A ref or id that is not among the loaded rows (filtered or truncated History).
            p.shortId = s->shortId(refTarget(*snap, trimmed));
            p.message = p.shortId + " (not in loaded history)";
        } else {
            p.message = "Not found in loaded history";
            p.warning = true;
        }
        return p;
    };
    return f;
}

Field commitInfo(Session& session, const std::string& label, const core::Oid& id)
{
    Field f{Field::Info, "info_" + label};
    f.text = label + ": " + commitLine(session, id, 80);
    return f;
}

Field commitInfo(Session& session, const std::string& label, const std::string& rev)
{
    Field f{Field::Info, "info_" + label};
    const core::HistoryRow* row = resolveRev(session, rev);
    std::string what = rev;
    if (row)
        what = row->shortId != rev ? rev + "  " + commitLine(*row, 80) : commitLine(*row, 80);
    else if (const auto snap = session.snapshot(); snap && !refTarget(*snap, rev).isNull())
        what = rev + "  " + session.shortId(refTarget(*snap, rev)) + " (not in loaded history)";
    f.text = label + ": " + what;
    return f;
}

} // namespace ggui
