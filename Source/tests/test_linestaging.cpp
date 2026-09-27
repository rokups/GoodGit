// Hunk and line staging, unstaging and discarding in the Diff panel (§4.5; P2-09).
#include "panels/ChangesPanel.hpp"
#include "panels/DiffPanel.hpp"
#include "shell/App.hpp"
#include "shell/Session.hpp"
#include "tests/Harness.hpp"

#include <algorithm>

namespace ggtest {

namespace {

std::string fileRef(Scenario& s, const char* group, const std::string& path)
{
    return s.child("//Changes", "##files") + "/" + group + "/" + path + "/###file_" + path;
}

std::string body(Scenario& s) { return s.child("//Diff", "##diff_body"); }

const ggui::core::DiffFile* diffFile(Scenario& s)
{
    const auto& d = s.session()->diff().diff();
    return d && !d->files.empty() ? &d->files[0] : nullptr;
}

// Waits until the Diff panel shows `path` from `group` with a fresh diff.
bool showFile(Scenario& s, const char* group, const std::string& path)
{
    if (!s.waitUntil([&] { return s.itemExists(fileRef(s, group, path).c_str()); }))
        return false;
    s.ctx->ItemClick(fileRef(s, group, path).c_str());
    s.showPanel("Diff");
    return s.waitUntil([&] {
        const auto* f = diffFile(s);
        const std::string g = group;
        const ggui::FileGroup expected = g == "Staged" ? ggui::FileGroup::Staged
            : g == "Untracked"                          ? ggui::FileGroup::Untracked
                                                        : ggui::FileGroup::Unstaged;
        return f && f->path() == path && s.session()->diff().file() && s.session()->diff().file()->group == expected;
    });
}

// The diff body is virtualised: scroll row `row` into view before interacting with it.
void scrollToRow(Scenario& s, int row)
{
    s.session()->diff().revealRow(row);
    s.ctx->Yield(3);
}

std::vector<std::string> linesOf(const std::string& text)
{
    std::vector<std::string> out;
    size_t start = 0;
    while (start < text.size()) {
        const auto nl = text.find('\n', start);
        const size_t end = nl == std::string::npos ? text.size() : nl + 1;
        out.push_back(text.substr(start, end - start));
        start = end;
    }
    return out;
}

std::string lineText(const ggui::core::DiffLine& l)
{
    return l.text + (l.noNewline ? "" : l.crlf ? "\r\n" : "\n");
}

// Independent model: the content that results from applying the selected changes of `file`
// forward to `oldText` (stage) or backward to `newText` (unstage / discard).
std::string applyModel(const ggui::core::DiffFile& file, const std::set<std::pair<int, int>>& selected,
    const std::string& base, bool reverse)
{
    const auto baseLines = linesOf(base);
    std::string out;
    size_t pos = 0; // next base line (0-based)
    for (size_t h = 0; h < file.hunks.size(); ++h) {
        const auto& hunk = file.hunks[h];
        int start = reverse ? hunk.newStart : hunk.oldStart;
        const int count = reverse ? hunk.newLines : hunk.oldLines;
        if (count == 0)
            ++start; // insertion after line `start`
        while (pos + 1 < static_cast<size_t>(start) && pos < baseLines.size())
            out += baseLines[pos++];
        for (size_t l = 0; l < hunk.lines.size(); ++l) {
            const auto& line = hunk.lines[l];
            const bool sel = selected.count({static_cast<int>(h), static_cast<int>(l)}) != 0;
            const char mine = reverse ? '+' : '-';   // lines that exist in the base
            const char other = reverse ? '-' : '+';  // lines that exist only on the other side
            if (line.origin == ' ') {
                out += baseLines[pos++];
            } else if (line.origin == mine) {
                if (!sel)
                    out += baseLines[pos];
                ++pos;
            } else if (line.origin == other && sel) {
                out += lineText(line);
            }
        }
    }
    while (pos < baseLines.size())
        out += baseLines[pos++];
    return out;
}

std::string randomWord(std::mt19937_64& rng)
{
    static const char* words[] = {"alpha", "beta", "gamma", "delta", "kappa", "omega", "sigma", "tau", "rho", "phi"};
    return words[rng() % 10] + std::to_string(rng() % 100);
}

} // namespace

GG_TEST("linestaging", "stage, discard and unstage hunks", "DIFF-STAGE-HUNK", "DIFF-DISCARD-HUNK", "DIFF-UNSTAGE-HUNK",
    "DIFF-HUNK-BUTTONS")
{
    const fs::path repo = s.fixture(Recipe::Empty, "hunks");
    std::string text;
    for (int i = 1; i <= 30; ++i)
        text += "line " + std::to_string(i) + "\n";
    s.commitFile(repo, "f.txt", text, "Base");
    std::string changed = text;
    changed.replace(changed.find("line 3\n"), 7, "LINE 3\n");
    changed.replace(changed.find("line 27\n"), 8, "LINE 27\n");
    s.write(repo, "f.txt", changed);
    GG_REQUIRE(s.openRepository(repo));
    GG_REQUIRE(showFile(s, "Unstaged", "f.txt"));
    GG_REQUIRE(diffFile(s)->hunks.size() == 2);
    // Stage the second hunk only.
    ctx->ItemClick((body(s) + "/###stage_hunk_1").c_str());
    GG_CHECK(s.waitUntil([&] { return s.gitOut(repo, {"show", ":f.txt"}).find("LINE 27") != std::string::npos; }));
    s.settle();
    GG_CHECK(s.gitOut(repo, {"show", ":f.txt"}).find("LINE 3") == std::string::npos);
    // Discard the remaining (first) hunk from the working tree.
    GG_REQUIRE(showFile(s, "Unstaged", "f.txt"));
    GG_REQUIRE(diffFile(s)->hunks.size() == 1);
    ctx->ItemClick((body(s) + "/###discard_hunk_0").c_str());
    GG_CHECK(s.waitUntil([&] { return s.read(repo, "f.txt").find("LINE 3") == std::string::npos; }));
    s.settle();
    GG_CHECK(s.read(repo, "f.txt").find("LINE 27") != std::string::npos);
    // Unstage the staged hunk.
    GG_REQUIRE(showFile(s, "Staged", "f.txt"));
    ctx->ItemClick((body(s) + "/###unstage_hunk_0").c_str());
    GG_CHECK(s.waitUntil([&] { return s.gitOut(repo, {"diff", "--cached", "--name-only"}).empty(); }));
    s.settle();
}

GG_TEST("linestaging", "hunks from the context menu in the side-by-side view", "DIFF-HUNK-MENU")
{
    const fs::path repo = s.fixture(Recipe::Empty, "hunks");
    std::string text;
    for (int i = 1; i <= 30; ++i)
        text += "line " + std::to_string(i) + "\n";
    s.commitFile(repo, "f.txt", text, "Base");
    std::string changed = text;
    changed.replace(changed.find("line 3\n"), 7, "LINE 3\n");
    changed.replace(changed.find("line 27\n"), 8, "LINE 27\n");
    s.write(repo, "f.txt", changed);
    GG_REQUIRE(s.openRepository(repo));
    GG_REQUIRE(showFile(s, "Unstaged", "f.txt"));
    s.comboSelect("//Diff/##diff_view", "Side by side");
    ctx->Yield(3);
    const std::string left = s.child(body(s).c_str(), "##sbs_left");
    // Row 0 is the first hunk's header (not shown here); rows 1-2 are context, row 3 removes line 3.
    s.contextMenu((left + "/###line_3").c_str(), "Stage hunk(s)");
    GG_CHECK(s.waitUntil([&] { return s.gitOut(repo, {"show", ":f.txt"}).find("LINE 3") != std::string::npos; }));
    s.settle();
    GG_CHECK(s.gitOut(repo, {"show", ":f.txt"}).find("LINE 27") == std::string::npos);
    GG_REQUIRE(showFile(s, "Staged", "f.txt"));
    s.contextMenu((s.child(body(s).c_str(), "##sbs_left") + "/###line_3").c_str(), "Unstage hunk(s)");
    GG_CHECK(s.waitUntil([&] { return s.gitOut(repo, {"diff", "--cached", "--name-only"}).empty(); }));
    s.settle();
    GG_REQUIRE(showFile(s, "Unstaged", "f.txt"));
    s.contextMenu((s.child(body(s).c_str(), "##sbs_left") + "/###line_3").c_str(), "Discard hunk(s)");
    GG_CHECK(s.waitUntil([&] { return s.read(repo, "f.txt").find("LINE 3") == std::string::npos; }));
    s.settle();
    GG_CHECK(s.read(repo, "f.txt").find("LINE 27") != std::string::npos);
    s.comboSelect("//Diff/##diff_view", "Unified");
}

GG_TEST("linestaging", "CRLF lines, missing final newline, new files", "DIFF-STAGE-LINES", "DIFF-DISCARD-LINES")
{
    const fs::path repo = s.fixture(Recipe::TextEdgeCases);
    s.write(repo, "new.txt", "keep one\ndrop me\nkeep two\n");
    GG_REQUIRE(s.openRepository(repo));
    // CRLF: stage the whole change of crlf.txt; the index keeps the CRLF bytes.
    GG_REQUIRE(showFile(s, "Unstaged", "crlf.txt"));
    ctx->ItemClick((body(s) + "/###stage_hunk_0").c_str());
    GG_CHECK(s.waitUntil([&] { return s.git(repo, {"show", ":crlf.txt"}).out == "one\r\nTWO\r\nthree\r\n"; }));
    s.settle();
    // No final newline on both sides.
    GG_REQUIRE(showFile(s, "Unstaged", "noeol.txt"));
    ctx->ItemClick((body(s) + "/###stage_hunk_0").c_str());
    GG_CHECK(s.waitUntil([&] { return s.git(repo, {"show", ":noeol.txt"}).out == "no final newline, changed"; }));
    s.settle();
    // Discard one line of an untracked file (rows: 0 hunk, 1..3 lines).
    GG_REQUIRE(showFile(s, "Untracked", "new.txt"));
    ctx->ItemClick((body(s) + "/###line_2").c_str());
    s.contextMenu((body(s) + "/###line_2").c_str(), "Discard line(s)");
    GG_CHECK(s.waitUntil([&] { return s.read(repo, "new.txt") == "keep one\nkeep two\n"; }));
    s.settle();
    // Stage one line of it (a new file with only that line in the index).
    GG_REQUIRE(showFile(s, "Untracked", "new.txt"));
    ctx->ItemClick((body(s) + "/###line_1").c_str());
    s.contextMenu((body(s) + "/###line_1").c_str(), "Stage line(s)");
    GG_CHECK(s.waitUntil([&] { return s.gitMayFail(repo, {"show", ":new.txt"}).out == "keep one\n"; }));
    s.settle();
}

GG_TEST("linestaging", "randomized line staging matches the content model", "DIFF-STAGING-RANDOM", "DIFF-STAGE-LINES",
    "DIFF-UNSTAGE-LINES")
{
    auto& rng = s.rng();
    const fs::path repo = s.fixture(Recipe::Empty, "random");
    std::vector<std::string> lines;
    const int n = 12 + static_cast<int>(rng() % 20);
    const bool crlf = rng() % 3 == 0;
    const std::string eol = crlf ? "\r\n" : "\n";
    for (int i = 0; i < n; ++i)
        lines.push_back(randomWord(rng) + eol);
    std::string original;
    for (const auto& l : lines)
        original += l;
    s.commitFile(repo, "r.txt", original, "Random base");
    // Random edits: replace, insert, delete.
    for (int e = 0; e < 6; ++e) {
        const size_t at = rng() % lines.size();
        switch (rng() % 3) {
        case 0: lines[at] = randomWord(rng) + eol; break;
        case 1: lines.insert(lines.begin() + static_cast<std::ptrdiff_t>(at), randomWord(rng) + eol); break;
        default:
            if (lines.size() > 3)
                lines.erase(lines.begin() + static_cast<std::ptrdiff_t>(at));
        }
    }
    std::string modified;
    for (const auto& l : lines)
        modified += l;
    s.write(repo, "r.txt", modified);
    ctx->LogInfo("seed %llu: %d lines, crlf=%d", static_cast<unsigned long long>(s.seed()), n, crlf ? 1 : 0);
    GG_REQUIRE(s.openRepository(repo));
    for (int round = 0; round < 4; ++round) {
        const bool unstage = round == 3;
        if (!showFile(s, unstage ? "Staged" : "Unstaged", "r.txt"))
            break;
        const ggui::core::DiffFile file = *diffFile(s);
        const std::string base = s.git(repo, {"show", unstage ? ":r.txt" : ":r.txt"}).out;
        // Rows as the Diff panel lays them out: per hunk a header row, then its lines.
        std::vector<std::pair<int, int>> rows; // (hunk, line) with line = -1 for the header
        for (size_t h = 0; h < file.hunks.size(); ++h) {
            rows.emplace_back(static_cast<int>(h), -1);
            for (size_t l = 0; l < file.hunks[h].lines.size(); ++l)
                rows.emplace_back(static_cast<int>(h), static_cast<int>(l));
        }
        // Find visual row indexes (gap rows come before hunks when context is expandable).
        std::vector<int> rowIndex;
        {
            int visual = 0;
            int prevNewEnd = 0;
            for (size_t h = 0; h < file.hunks.size(); ++h) {
                const auto& hk = file.hunks[h];
                const int newStart = hk.newLines == 0 ? hk.newStart + 1 : hk.newStart;
                if (file.newText && newStart - 1 >= prevNewEnd + 1)
                    ++visual; // gap row
                rowIndex.push_back(visual++);
                for (size_t l = 0; l < hk.lines.size(); ++l)
                    rowIndex.push_back(visual++);
                prevNewEnd = hk.newStart + hk.newLines - (hk.newLines == 0 ? 0 : 1);
            }
        }
        size_t a = rng() % rows.size();
        size_t b = std::min(rows.size() - 1, a + rng() % 5);
        std::set<std::pair<int, int>> selected;
        bool changes = false;
        for (size_t i = a; i <= b; ++i) {
            const auto [h, l] = rows[i];
            const auto& hk = file.hunks[static_cast<size_t>(h)];
            if (l < 0) {
                for (size_t k = 0; k < hk.lines.size(); ++k)
                    if (hk.lines[k].origin != ' ') {
                        selected.emplace(h, static_cast<int>(k));
                        changes = true;
                    }
            } else {
                selected.emplace(h, l);
                changes = changes || hk.lines[static_cast<size_t>(l)].origin != ' ';
            }
        }
        if (!changes)
            continue;
        const std::string first = "###" + std::string(rows[a].second < 0 ? "hunk_" + std::to_string(rows[a].first)
                                                                        : "line_" + std::to_string(rowIndex[a]));
        const std::string last = "###" + std::string(rows[b].second < 0 ? "hunk_" + std::to_string(rows[b].first)
                                                                       : "line_" + std::to_string(rowIndex[b]));
        scrollToRow(s, rowIndex[a]);
        ctx->ItemClick((body(s) + "/" + first).c_str());
        scrollToRow(s, rowIndex[b]);
        ctx->KeyDown(ImGuiMod_Shift);
        ctx->ItemClick((body(s) + "/" + last).c_str());
        ctx->KeyUp(ImGuiMod_Shift);
        scrollToRow(s, rowIndex[a]);
        std::string expected;
        if (unstage) {
            expected = applyModel(file, selected, base, true);
            s.contextMenu((body(s) + "/" + first).c_str(), "Unstage line(s)");
        } else {
            expected = applyModel(file, selected, base, false);
            s.contextMenu((body(s) + "/" + first).c_str(), "Stage line(s)");
        }
        const bool ok = s.waitUntil([&] { return s.git(repo, {"show", ":r.txt"}).out == expected; });
        if (!ok)
            ctx->LogError("round %d (seed %llu): index differs from the model", round,
                static_cast<unsigned long long>(s.seed()));
        GG_CHECK(ok);
        s.settle();
    }
    // The working tree never changes while staging.
    GG_CHECK(s.read(repo, "r.txt") == modified);
}

} // namespace ggtest
