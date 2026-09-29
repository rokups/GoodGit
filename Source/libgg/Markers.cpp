#include "libgg/Markers.hpp"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <unordered_map>

namespace gg::markers {

namespace {

constexpr int kMinMarker = 7;
// Room for the user to paste marker-like text while editing (as Jujutsu does).
constexpr int kMarkerLengthMargin = 4;
constexpr const char* kNoEolFlag = "[no newline]";

struct Line {
    size_t offset;
    size_t length; // including the line ending
    std::string_view body; // without "\n" / "\r\n"
};

std::vector<Line> splitLines(std::string_view text)
{
    std::vector<Line> lines;
    size_t start = 0;
    while (start < text.size()) {
        size_t nl = text.find('\n', start);
        const size_t end = nl == std::string_view::npos ? text.size() : nl + 1;
        size_t bodyEnd = nl == std::string_view::npos ? text.size() : nl;
        if (bodyEnd > start && text[bodyEnd - 1] == '\r' && nl != std::string_view::npos)
            --bodyEnd;
        lines.push_back(Line{start, end - start, text.substr(start, bodyEnd - start)});
        start = end;
    }
    return lines;
}

bool isMarkerChar(char c) { return c == '<' || c == '|' || c == '=' || c == '>' || c == '+' || c == '-'; }

// Length of the marker run at the start of `body` when it forms a marker line (run followed by
// end of line, or a space and a label; '=' never has a label). 0 otherwise.
int markerRun(std::string_view body, char* kind)
{
    if (body.empty() || !isMarkerChar(body[0]))
        return 0;
    const char c = body[0];
    size_t n = 1;
    while (n < body.size() && body[n] == c)
        ++n;
    if (n < static_cast<size_t>(kMinMarker))
        return 0;
    if (n < body.size()) {
        if (c == '=' || body[n] != ' ')
            return 0;
    }
    *kind = c;
    return static_cast<int>(n);
}

// Leading run of one marker character (for choosing the writer's marker length).
int leadingRun(std::string_view body)
{
    if (body.empty() || !isMarkerChar(body[0]))
        return 0;
    size_t n = 1;
    while (n < body.size() && body[n] == body[0])
        ++n;
    return static_cast<int>(n);
}

std::string labelOf(std::string_view body, int L)
{
    if (static_cast<int>(body.size()) <= L)
        return {};
    return std::string(body.substr(static_cast<size_t>(L) + 1));
}

// Splits a "[no newline]" flag off a label.
std::string stripFlag(std::string label, bool* flag)
{
    *flag = false;
    const std::string token = std::string(" ") + kNoEolFlag;
    if (label == kNoEolFlag) {
        *flag = true;
        return {};
    }
    if (label.size() >= token.size() && label.compare(label.size() - token.size(), token.size(), token) == 0) {
        *flag = true;
        label.resize(label.size() - token.size());
    }
    return label;
}

int parseSideCount(const std::string& label)
{
    // "gg N-sided conflict[ free text]"
    if (label.rfind("gg ", 0) != 0)
        return 0;
    size_t i = 3;
    int n = 0;
    while (i < label.size() && label[i] >= '0' && label[i] <= '9') {
        n = n * 10 + (label[i] - '0');
        ++i;
        if (n > 100000)
            return 0;
    }
    if (i == 3)
        return 0;
    const std::string rest = label.substr(i);
    if (rest.rfind("-sided conflict", 0) != 0)
        return 0;
    const std::string tail = rest.substr(std::strlen("-sided conflict"));
    if (!tail.empty() && tail[0] != ' ')
        return 0;
    return n;
}

std::string joinContent(std::string_view text, const std::vector<Line>& lines, size_t from, size_t to)
{
    if (from >= to)
        return {};
    return std::string(text.substr(lines[from].offset, lines[to - 1].offset + lines[to - 1].length - lines[from].offset));
}

enum class Attempt { Region, Restart, Malformed };

// Tries to parse a region whose opening marker is line `i` with length `L`.
Attempt tryRegion(std::string_view text, const std::vector<Line>& lines, size_t i, int L, Region& out, size_t& next)
{
    const std::string openLabel = labelOf(lines[i].body, L);
    auto structural = [&](size_t k, char* kind) {
        const int n = markerRun(lines[k].body, kind);
        return n == L;
    };
    // Form: decided by the first structural marker of length L.
    size_t j = i + 1;
    char kind = 0;
    for (; j < lines.size(); ++j)
        if (structural(j, &kind))
            break;
    if (j >= lines.size())
        return Attempt::Malformed;
    if (kind == '<') {
        next = j;
        return Attempt::Restart;
    }
    out = Region{};
    out.markerLength = L;
    out.begin = lines[i].offset;
    if (kind == '|') {
        // Two-sided diff3: A | base = B >
        Section a, base, b;
        bool flag = false;
        a.label = stripFlag(openLabel, &flag);
        a.noEol = flag;
        a.content = joinContent(text, lines, i + 1, j);
        base.label = stripFlag(labelOf(lines[j].body, L), &flag);
        base.noEol = flag;
        size_t k = j + 1;
        for (; k < lines.size(); ++k) {
            if (!structural(k, &kind) || kind == '+' || kind == '-')
                continue;
            break;
        }
        if (k >= lines.size())
            return Attempt::Malformed;
        if (kind == '<') {
            next = k;
            return Attempt::Restart;
        }
        if (kind != '=')
            return Attempt::Malformed;
        base.content = joinContent(text, lines, j + 1, k);
        size_t m = k + 1;
        for (; m < lines.size(); ++m) {
            if (!structural(m, &kind) || kind == '+' || kind == '-')
                continue;
            break;
        }
        if (m >= lines.size())
            return Attempt::Malformed;
        if (kind == '<') {
            next = m;
            return Attempt::Restart;
        }
        if (kind != '>')
            return Attempt::Malformed;
        b.label = stripFlag(labelOf(lines[m].body, L), &flag);
        b.noEol = flag;
        b.content = joinContent(text, lines, k + 1, m);
        out.sides = {a, b};
        out.bases = {base};
        out.end = lines[m].offset + lines[m].length;
        next = m + 1;
        return Attempt::Region;
    }
    if (kind == '+') {
        const int declared = parseSideCount(openLabel);
        if (declared < 2)
            return Attempt::Malformed;
        out.extended = true;
        size_t start = j; // current section marker
        bool inSide = true;
        for (size_t k = j + 1;; ++k) {
            if (k >= lines.size())
                return Attempt::Malformed;
            if (!structural(k, &kind) || kind == '|' || kind == '=')
                continue;
            if (kind == '<') {
                next = k;
                return Attempt::Restart;
            }
            Section s;
            bool flag = false;
            s.label = stripFlag(labelOf(lines[start].body, L), &flag);
            s.noEol = flag;
            s.content = joinContent(text, lines, start + 1, k);
            if (inSide)
                out.sides.push_back(std::move(s));
            else
                out.bases.push_back(std::move(s));
            if (kind == '>') {
                if (!inSide || static_cast<int>(out.sides.size()) != declared)
                    return Attempt::Malformed;
                out.end = lines[k].offset + lines[k].length;
                next = k + 1;
                return Attempt::Region;
            }
            // Sections strictly alternate side, base, side, …
            if ((inSide && kind != '-') || (!inSide && kind != '+'))
                return Attempt::Malformed;
            inSide = !inSide;
            start = k;
        }
    }
    return Attempt::Malformed; // '=' / '>' / '-' first: Git's two-way style or garbage
}

std::vector<std::string_view> viewLines(std::string_view text)
{
    std::vector<std::string_view> out;
    size_t start = 0;
    while (start < text.size()) {
        const size_t nl = text.find('\n', start);
        const size_t end = nl == std::string_view::npos ? text.size() : nl + 1;
        out.push_back(text.substr(start, end - start));
        start = end;
    }
    return out;
}

// For each line of `ref`, the index of the matching line in `other` (LCS), or -1.
std::vector<int> matchLines(const std::vector<std::string_view>& ref, const std::vector<std::string_view>& other)
{
    std::vector<int> map(ref.size(), -1);
    size_t pre = 0;
    while (pre < ref.size() && pre < other.size() && ref[pre] == other[pre]) {
        map[pre] = static_cast<int>(pre);
        ++pre;
    }
    size_t suf = 0;
    while (suf < ref.size() - pre && suf < other.size() - pre
        && ref[ref.size() - 1 - suf] == other[other.size() - 1 - suf]) {
        map[ref.size() - 1 - suf] = static_cast<int>(other.size() - 1 - suf);
        ++suf;
    }
    const size_t n = ref.size() - pre - suf;
    const size_t m = other.size() - pre - suf;
    if (n == 0 || m == 0 || n * m > 4'000'000)
        return map; // coarser hunks for huge differences; terms still cancel correctly
    std::vector<std::uint32_t> dp((n + 1) * (m + 1), 0);
    auto at = [&](size_t a, size_t b) -> std::uint32_t& { return dp[a * (m + 1) + b]; };
    for (size_t a = n; a-- > 0;)
        for (size_t b = m; b-- > 0;)
            at(a, b) = ref[pre + a] == other[pre + b] ? at(a + 1, b + 1) + 1 : std::max(at(a + 1, b), at(a, b + 1));
    size_t a = 0, b = 0;
    while (a < n && b < m) {
        if (ref[pre + a] == other[pre + b]) {
            map[pre + a] = static_cast<int>(pre + b);
            ++a;
            ++b;
        } else if (at(a + 1, b) >= at(a, b + 1)) {
            ++a;
        } else {
            ++b;
        }
    }
    return map;
}

std::string concat(const std::vector<std::string_view>& lines, size_t from, size_t to)
{
    std::string s;
    for (size_t i = from; i < to; ++i)
        s.append(lines[i]);
    return s;
}

} // namespace

std::string Section::value() const
{
    if (noEol && !content.empty() && content.back() == '\n')
        return content.substr(0, content.size() - 1);
    return content;
}

int Parsed::maxSides() const
{
    int n = 0;
    for (const auto& r : regions)
        n = std::max(n, static_cast<int>(r.sides.size()));
    return n;
}

Parsed parse(std::string_view text)
{
    Parsed parsed;
    if (text.find("<<<<<<<") == std::string_view::npos)
        return parsed; // fast path: no opening marker at all
    const auto lines = splitLines(text);
    size_t i = 0;
    while (i < lines.size()) {
        char kind = 0;
        const int L = markerRun(lines[i].body, &kind);
        if (L == 0 || kind != '<') {
            ++i;
            continue;
        }
        Region region;
        size_t next = i + 1;
        switch (tryRegion(text, lines, i, L, region, next)) {
        case Attempt::Region:
            parsed.regions.push_back(std::move(region));
            i = next;
            break;
        case Attempt::Restart:  // abandoned at another opening of the same length
        case Attempt::Malformed:
            // The opening is text; scan on from the next line, so an opening of another length
            // between here and where the candidate stopped still starts its own region.
            ++i;
            break;
        }
    }
    return parsed;
}

bool isConflicted(std::string_view text) { return !looksBinary(text) && parse(text).conflicted(); }

bool looksBinary(std::string_view text)
{
    return text.substr(0, std::min<size_t>(text.size(), 8000)).find('\0') != std::string_view::npos;
}

static Merge toMergeOnce(std::string_view text)
{
    const Parsed p = parse(text);
    if (!p.conflicted())
        return Merge::plain(std::string(text));
    const size_t n = static_cast<size_t>(p.maxSides());
    Merge m;
    m.adds.assign(n, {});
    m.removes.assign(n - 1, {});
    size_t pos = 0;
    for (const auto& r : p.regions) {
        const std::string_view outside = text.substr(pos, r.begin - pos);
        std::vector<std::string> sides, bases;
        for (const auto& s : r.sides)
            sides.push_back(s.value());
        for (const auto& b : r.bases)
            bases.push_back(b.value());
        while (sides.size() < n) { // padding +b −b cancels
            bases.push_back(bases.front());
            sides.push_back(bases.front());
        }
        for (size_t k = 0; k < n; ++k) {
            m.adds[k].append(outside);
            m.adds[k] += sides[k];
        }
        for (size_t k = 0; k + 1 < n; ++k) {
            m.removes[k].append(outside);
            m.removes[k] += bases[k];
        }
        pos = r.end;
    }
    const std::string_view rest = text.substr(pos);
    for (auto& a : m.adds)
        a.append(rest);
    for (auto& r : m.removes)
        r.append(rest);
    return m;
}

Merge toMerge(std::string_view text)
{
    // A term is outside text joined with one section of each region, so marker-like lines
    // outside and inside regions can join into a region of its own: flatten that term too
    // (+t = +a0 − r1 + a1 …, −t = −a0 + r1 − a1 …) until no term has regions. Each round strips
    // marker lines, so terms only get shorter and this ends.
    Merge m = toMergeOnce(text);
    for (bool again = true; again;) {
        again = false;
        for (size_t i = 0; i < m.adds.size() && !again; ++i)
            if (isConflicted(m.adds[i])) {
                Merge t = toMergeOnce(m.adds[i]);
                m.adds.erase(m.adds.begin() + static_cast<std::ptrdiff_t>(i));
                m.adds.insert(m.adds.begin() + static_cast<std::ptrdiff_t>(i), t.adds.begin(), t.adds.end());
                m.removes.insert(m.removes.begin() + static_cast<std::ptrdiff_t>(std::min(i, m.removes.size())),
                    t.removes.begin(), t.removes.end());
                again = true;
            }
        for (size_t i = 0; i < m.removes.size() && !again; ++i)
            if (isConflicted(m.removes[i])) {
                Merge t = toMergeOnce(m.removes[i]);
                m.removes.erase(m.removes.begin() + static_cast<std::ptrdiff_t>(i));
                m.removes.insert(m.removes.begin() + static_cast<std::ptrdiff_t>(i), t.adds.begin(), t.adds.end());
                m.adds.insert(m.adds.begin() + static_cast<std::ptrdiff_t>(i + 1), t.removes.begin(), t.removes.end());
                again = true;
            }
    }
    return m;
}

Merge combine(const Merge& base, const Merge& ours, const Merge& theirs)
{
    Merge m;
    m.adds = ours.adds;
    m.adds.insert(m.adds.end(), theirs.adds.begin(), theirs.adds.end());
    m.adds.insert(m.adds.end(), base.removes.begin(), base.removes.end());
    m.removes = ours.removes;
    m.removes.insert(m.removes.end(), theirs.removes.begin(), theirs.removes.end());
    m.removes.insert(m.removes.end(), base.adds.begin(), base.adds.end());
    return m;
}

namespace {

// §7.3 as byte equality: cancel equal add/remove pairs; resolved when all adds agree (Git's
// rule for the same change on both sides; `strict` leaves it out: a − r + a is not a).
void simplifyPairs(Merge& m, bool strict = false)
{
    for (size_t r = 0; r < m.removes.size();) {
        auto it = std::find(m.adds.begin(), m.adds.end(), m.removes[r]);
        if (it != m.adds.end()) {
            m.adds.erase(it);
            m.removes.erase(m.removes.begin() + static_cast<std::ptrdiff_t>(r));
        } else {
            ++r;
        }
    }
    if (!strict && !m.adds.empty()
        && std::all_of(m.adds.begin(), m.adds.end(), [&](const std::string& a) { return a == m.adds[0]; })) {
        m.adds.resize(1);
        m.removes.clear();
    }
}

bool writeLines(const Merge& m, const WriteOptions& options, std::string& out, bool strict = false);

// a − r + b as plain text when every hunk of it resolves by cancellation alone (in each hunk a or
// b is r): exactly the same value, as one file.
std::optional<std::string> cleanMerge(const std::string& r, const std::string& a, const std::string& b)
{
    Merge three{{a, b}, {r}};
    simplifyPairs(three, true);
    if (three.isResolved())
        return three.adds[0];
    std::string out;
    if (!writeLines(three, WriteOptions{}, out, true))
        return std::nullopt;
    return out;
}

} // namespace

// strict: without Git's same-change rule ("all adds agree"), the exact term algebra.
static void simplify(Merge& m, bool strict)
{
    simplifyPairs(m, strict);
    // Terms that merge cleanly collapse: a − r + b is one file when every hunk of that three-way
    // merge resolves. Terms that cancel hunk by hunk but differ as whole files (the same change
    // made in other surroundings) then cancel too; without this a conflict rebased away and back
    // keeps such terms and grows each time.
    for (bool collapsed = true; collapsed && !m.removes.empty() && m.adds.size() >= 2;) {
        collapsed = false;
        for (size_t k = 0; k < m.removes.size() && !collapsed; ++k)
            for (size_t i = 0; i < m.adds.size() && !collapsed; ++i)
                for (size_t j = i + 1; j < m.adds.size() && !collapsed; ++j) {
                    auto merged = cleanMerge(m.removes[k], m.adds[i], m.adds[j]);
                    if (!merged)
                        continue;
                    m.adds.erase(m.adds.begin() + static_cast<std::ptrdiff_t>(j));
                    m.adds[i] = std::move(*merged);
                    m.removes.erase(m.removes.begin() + static_cast<std::ptrdiff_t>(k));
                    collapsed = true;
                }
        if (collapsed)
            simplifyPairs(m, strict);
    }
}

void simplify(Merge& m)
{
    simplify(m, false);
}

std::string writeRegion(const std::vector<std::string>& sides, const std::vector<std::string>& bases,
    const WriteOptions& options)
{
    // Marker length: longer than any leading run of a marker character in the content.
    int run = 0;
    size_t crlf = 0, lf = 0;
    auto scan = [&](const std::string& s) {
        for (auto l : viewLines(s)) {
            std::string_view body = l;
            if (!body.empty() && body.back() == '\n') {
                body.remove_suffix(1);
                if (!body.empty() && body.back() == '\r') {
                    body.remove_suffix(1);
                    ++crlf;
                } else {
                    ++lf;
                }
            }
            run = std::max(run, leadingRun(body));
        }
    };
    for (const auto& s : sides)
        scan(s);
    for (const auto& b : bases)
        scan(b);
    const int L = std::max({kMinMarker, options.markerSize, run + kMarkerLengthMargin});
    const std::string eol = crlf > lf ? "\r\n" : "\n";
    auto marker = [&](char c, const std::string& label, bool noEol) {
        std::string line(static_cast<size_t>(L), c);
        std::string text = label;
        if (noEol)
            text += text.empty() ? kNoEolFlag : std::string(" ") + kNoEolFlag;
        if (!text.empty())
            line += " " + text;
        return line + eol;
    };
    auto body = [](const std::string& s) { return (!s.empty() && s.back() != '\n') ? s + "\n" : s; };
    auto noEol = [](const std::string& s) { return !s.empty() && s.back() != '\n'; };
    auto sideLabel = [&](size_t k) {
        return k < options.sideLabels.size() ? options.sideLabels[k] : "side " + std::to_string(k + 1);
    };
    std::string out;
    if (sides.size() == 2 && bases.size() == 1) {
        out += marker('<', sideLabel(0), noEol(sides[0]));
        out += body(sides[0]);
        out += marker('|', "base", noEol(bases[0]));
        out += body(bases[0]);
        out += std::string(static_cast<size_t>(L), '=') + eol;
        out += body(sides[1]);
        out += marker('>', sideLabel(1), noEol(sides[1]));
        return out;
    }
    out += marker('<', "gg " + std::to_string(sides.size()) + "-sided conflict", false);
    for (size_t k = 0; k < sides.size(); ++k) {
        out += marker('+', sideLabel(k), noEol(sides[k]));
        out += body(sides[k]);
        if (k < bases.size()) {
            out += marker('-', "base " + std::to_string(k + 1), noEol(bases[k]));
            out += body(bases[k]);
        }
    }
    out += marker('>', "end of conflict", false);
    return out;
}

namespace {

// §7.4 for an unresolved merge value: anchors copied, each hunk plain when it resolves, else a
// region with every term (the same sides in every region). True when every hunk resolved.
bool writeLines(const Merge& m, const WriteOptions& options, std::string& out, bool strict)
{
    // Terms in writing order: a0, r1, a1, …; the reference is r1.
    std::vector<const std::string*> terms;
    for (size_t k = 0; k < m.adds.size(); ++k) {
        terms.push_back(&m.adds[k]);
        if (k < m.removes.size())
            terms.push_back(&m.removes[k]);
    }
    const auto ref = viewLines(m.removes[0]);
    std::vector<std::vector<std::string_view>> lines;
    std::vector<std::vector<int>> maps;
    for (const auto* t : terms) {
        lines.push_back(viewLines(*t));
        maps.push_back(matchLines(ref, lines.back()));
    }
    // Anchors: reference lines unchanged in every term.
    std::vector<size_t> anchors;
    for (size_t i = 0; i < ref.size(); ++i) {
        bool all = true;
        for (const auto& map : maps)
            all = all && map[i] >= 0;
        if (all)
            anchors.push_back(i);
    }
    bool clean = true;
    std::vector<size_t> cursor(terms.size(), 0);
    auto emitGap = [&](const std::vector<size_t>& until) {
        Merge hunk;
        for (size_t t = 0; t < terms.size(); ++t) {
            std::string part = concat(lines[t], cursor[t], until[t]);
            if (t % 2 == 0)
                hunk.adds.push_back(std::move(part));
            else
                hunk.removes.push_back(std::move(part));
        }
        // Resolved when the hunk alone simplifies to one side. Otherwise the region keeps every
        // term of the file, in the file's order: each region then has the same sides, and
        // reading the file back (§7.1) rebuilds each term, not a mix of terms across regions.
        Merge simplified = hunk;
        simplifyPairs(simplified, strict || !options.sameChangeResolves);
        if (simplified.isResolved()) {
            out += simplified.adds[0];
        } else {
            clean = false;
            out += writeRegion(hunk.adds, hunk.removes, options);
        }
    };
    for (size_t anchor : anchors) {
        std::vector<size_t> until(terms.size());
        for (size_t t = 0; t < terms.size(); ++t)
            until[t] = static_cast<size_t>(maps[t][anchor]);
        emitGap(until);
        out.append(ref[anchor]);
        for (size_t t = 0; t < terms.size(); ++t)
            cursor[t] = until[t] + 1;
    }
    std::vector<size_t> ends(terms.size());
    for (size_t t = 0; t < terms.size(); ++t)
        ends[t] = lines[t].size();
    emitGap(ends);
    return clean;
}

} // namespace

std::string materialize(Merge m, const WriteOptions& options)
{
    simplify(m, !options.sameChangeResolves);
    if (m.isResolved())
        return m.adds[0];
    std::string out;
    writeLines(m, options, out);
    return out;
}

std::string mergeFiles(std::string_view base, std::string_view ours, std::string_view theirs, const WriteOptions& options)
{
    return materialize(combine(toMerge(base), toMerge(ours), toMerge(theirs)), options);
}

std::string takeSide(std::string_view text, int side, int region)
{
    const Parsed p = parse(text);
    std::string out;
    size_t pos = 0;
    for (size_t i = 0; i < p.regions.size(); ++i) {
        const Region& r = p.regions[i];
        out.append(text.substr(pos, r.begin - pos));
        const bool target = region < 0 || static_cast<int>(i) == region;
        if (target && side >= 0 && side < static_cast<int>(r.sides.size()))
            out += r.sides[static_cast<size_t>(side)].value();
        else
            out.append(text.substr(r.begin, r.end - r.begin));
        pos = r.end;
    }
    out.append(text.substr(pos));
    return out;
}

std::string takeBase(std::string_view text)
{
    const Parsed p = parse(text);
    std::string out;
    size_t pos = 0;
    for (const Region& r : p.regions) {
        out.append(text.substr(pos, r.begin - pos));
        if (!r.bases.empty())
            out += r.bases.front().value();
        pos = r.end;
    }
    out.append(text.substr(pos));
    return out;
}

} // namespace gg::markers
