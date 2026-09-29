// First-class conflict markers: parser, writer and N-way term algebra
// (REBUILD_PLAN §4.10, M1; grammar in docs/spec/conflict-markers.md).
//
// Everything here is a pure function of file bytes. Attributes (gg-conflicts=false, filters)
// are applied by callers.
#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace gg::markers {

struct Section {
    std::string label;
    std::string content;    // raw bytes of the section's lines (with their line endings)
    bool noEol = false;     // "[no newline]" flag: the final '\n' is not part of the content
    // Content as the side really is (noEol applied).
    std::string value() const;
};

struct Region {
    size_t begin = 0;       // byte offset of the opening marker line
    size_t end = 0;         // byte offset just past the closing marker line
    int markerLength = 7;
    bool extended = false;  // N-sided form
    std::vector<Section> sides;
    std::vector<Section> bases; // sides.size() - 1
};

struct Parsed {
    std::vector<Region> regions;
    bool conflicted() const { return !regions.empty(); }
    int maxSides() const;
};

// Parses well-formed regions (§4 of the spec). Malformed or partial markers are text.
Parsed parse(std::string_view text);
bool isConflicted(std::string_view text);
// True when the bytes look binary (NUL within the first 8000 bytes, Git's heuristic).
bool looksBinary(std::string_view text);

// Broken-region diagnostic (not part of the grammar: a warning, §8 of the spec). `before` is
// the committed (HEAD) text, `after` an edit of it. An edit that removes a structural line of a
// region (the separator, a base, ...) while keeping its opening/closing marker leaves that
// marker as ordinary text instead of resolving the file: `after` is no longer conflicted, but
// it is not a clean resolution either. Returns the 1-based line numbers in `after` of such
// leftover opening (`<`) or closing (`>`) marker lines: lines whose marker length equals the
// length of some region `before` had, and which are not part of a well-formed region of
// `after`. To ignore marker-like text that was already there outside `before`'s own regions,
// a length only counts when `after` has *more* such stray lines than `before` did (simple
// count comparison; it does not try to match which particular stray line is "new"). Empty when
// `before` has no regions.
std::vector<size_t> brokenMarkers(std::string_view before, std::string_view after);

// ---- Term algebra ------------------------------------------------------------------------------

// M = a0 - r1 + a1 - … - rn + an (adds.size() == removes.size() + 1).
struct Merge {
    std::vector<std::string> adds;
    std::vector<std::string> removes;
    static Merge plain(std::string content) { return Merge{{std::move(content)}, {}}; }
    bool isResolved() const { return removes.empty() && adds.size() == 1; }
};

// A file's content as a merge value (§7.1): a plain file is one term.
Merge toMerge(std::string_view text);
// merge(base, ours, theirs) = ours + theirs - base (§7.2).
Merge combine(const Merge& base, const Merge& ours, const Merge& theirs);
// Cancels equal add/remove pairs and resolves when all adds agree (§7.3).
void simplify(Merge& m);
// True when a and b are the same conflict value: toMerge(a) − toMerge(b) simplifies to nothing
// under the strict algebra (no Git same-change rule; §7.3).
bool sameValue(std::string_view a, std::string_view b);

struct WriteOptions {
    int markerSize = 7;          // conflict-marker-size attribute (minimum 7)
    std::vector<std::string> sideLabels; // optional labels per side (index = side)
    // Git's rule: a hunk where every side made the same change is resolved to it. Off, such a
    // hunk stays a region (the exact term algebra: a − r + a is not a).
    bool sameChangeResolves = true;
};

// Materialises a merge value as file bytes (§7.4): plain when resolved, otherwise the
// unresolved hunks as regions (diff3 for 2 sides, extended form for more). Never nests.
std::string materialize(Merge m, const WriteOptions& options = {});

// Convenience: three-way merge of plain or conflicted files.
std::string mergeFiles(std::string_view base, std::string_view ours, std::string_view theirs,
    const WriteOptions& options = {});

// Writes one region; exposed for "take side" / "commit with conflicts".
std::string writeRegion(const std::vector<std::string>& sides, const std::vector<std::string>& bases,
    const WriteOptions& options = {});

// Replaces every region with side `side` (0-based) of that region; `region` limits to one
// region (-1 = all). Returns the new file bytes.
std::string takeSide(std::string_view text, int side, int region = -1);
// Replaces every region with its first base section (the merge base as the file had it).
std::string takeBase(std::string_view text);

} // namespace gg::markers
