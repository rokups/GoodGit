#include "util/PatchBuilder.hpp"

#include <cstdio>

namespace ggui {

namespace {

std::string modeText(std::uint32_t mode)
{
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%06o", mode);
    return buf;
}

std::string header(const core::DiffFile& f, bool partialReverse, bool partialForward)
{
    // (A diff sets both paths, the same unless renamed; a side that does not exist has mode 0.)
    const std::string& oldPath = f.oldPath;
    const std::string& newPath = f.newPath;
    // A partial reverse of a new file (or a partial forward of a deletion) is a plain modification.
    const bool added = f.oldMode == 0 && !partialReverse;
    const bool deleted = f.newMode == 0 && !partialForward;
    std::string h = "diff --git a/" + oldPath + " b/" + newPath + "\n";
    if (added)
        h += "new file mode " + modeText(f.newMode) + "\n";
    else if (deleted)
        h += "deleted file mode " + modeText(f.oldMode) + "\n";
    else if (f.oldMode != f.newMode && f.oldMode != 0 && f.newMode != 0)
        h += "old mode " + modeText(f.oldMode) + "\nnew mode " + modeText(f.newMode) + "\n";
    if (oldPath != newPath && !added && !deleted)
        h += "rename from " + oldPath + "\nrename to " + newPath + "\n";
    h += (added ? std::string("--- /dev/null") : "--- a/" + oldPath) + "\n";
    h += (deleted ? std::string("+++ /dev/null") : "+++ b/" + newPath) + "\n";
    return h;
}

} // namespace

LineSet hunkLines(const core::DiffFile& file, int h)
{
    LineSet s;
    const auto& lines = file.hunks[static_cast<size_t>(h)].lines;
    for (size_t l = 0; l < lines.size(); ++l)
        if (lines[l].origin == '+' || lines[l].origin == '-')
            s.emplace(h, static_cast<int>(l));
    return s;
}

std::string buildPatch(const core::DiffFile& file, LineSet selected, bool reverse)
{
    // A changed last line without newline moves together with its counterpart.
    for (size_t h = 0; h < file.hunks.size(); ++h) {
        const auto& lines = file.hunks[h].lines;
        bool eofSelected = false;
        for (size_t l = 0; l < lines.size(); ++l)
            if (lines[l].noNewline && lines[l].origin != ' ' && selected.count({static_cast<int>(h), static_cast<int>(l)}))
                eofSelected = true;
        if (eofSelected)
            for (size_t l = 0; l < lines.size(); ++l)
                if (lines[l].noNewline && lines[l].origin != ' ')
                    selected.emplace(static_cast<int>(h), static_cast<int>(l));
    }
    size_t changed = 0;
    for (size_t h = 0; h < file.hunks.size(); ++h)
        changed += hunkLines(file, static_cast<int>(h)).size();
    size_t chosen = 0;
    for (const auto& [h, l] : selected) {
        const auto& lines = file.hunks[static_cast<size_t>(h)].lines;
        if (lines[static_cast<size_t>(l)].origin != ' ') // (selections are lines of the file)
            ++chosen;
    }
    const bool partial = chosen < changed;
    std::string body;
    long delta = 0;
    bool any = false;
    for (size_t h = 0; h < file.hunks.size(); ++h) {
        const auto& hunk = file.hunks[h];
        std::string text;
        int oldCount = 0, newCount = 0;
        bool changes = false;
        for (size_t l = 0; l < hunk.lines.size(); ++l) {
            const auto& line = hunk.lines[l];
            const bool sel = selected.count({static_cast<int>(h), static_cast<int>(l)}) != 0;
            char origin = line.origin;
            if (origin == '-' && !sel)
                origin = reverse ? 0 : ' ';
            else if (origin == '+' && !sel)
                origin = reverse ? ' ' : 0;
            if (origin == 0)
                continue;
            if (origin != ' ')
                changes = true;
            if (origin != '+')
                ++oldCount;
            if (origin != '-')
                ++newCount;
            text.push_back(origin);
            text += line.text;
            text += line.crlf ? "\r\n" : "\n";
            if (line.noNewline)
                text += "\\ No newline at end of file\n";
        }
        if (!changes)
            continue;
        any = true;
        int oldStart = hunk.oldStart;
        int newStart = hunk.newStart;
        if (!reverse) {
            newStart = static_cast<int>(oldStart + delta + (oldCount == 0 ? 1 : 0) - (newCount == 0 ? 1 : 0));
            if (oldCount == 0 && newCount == 0)
                newStart = oldStart;
            delta += newCount - oldCount;
        } else {
            oldStart = static_cast<int>(newStart + delta + (newCount == 0 ? 1 : 0) - (oldCount == 0 ? 1 : 0));
            delta += oldCount - newCount;
        }
        char hdr[96];
        std::snprintf(hdr, sizeof(hdr), "@@ -%d,%d +%d,%d @@\n", oldStart, oldCount, newStart, newCount);
        body += hdr;
        body += text;
    }
    return any ? header(file, reverse && partial, !reverse && partial) + body : std::string();
}

} // namespace ggui
