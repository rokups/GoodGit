// Builds the patch for staging, unstaging or discarding selected hunks / lines (P2-09).
//
// The patch is always written in the diff's own direction (old → new):
//  * forward (stage: `git apply --cached`): unselected '-' lines become context, unselected
//    '+' lines are dropped;
//  * reverse (unstage / discard: `git apply [--cached] -R`): unselected '+' lines become
//    context, unselected '-' lines are dropped.
// Hunk headers are recomputed; CRLF and "No newline at end of file" are kept.
#pragma once

#include <core/Types.hpp>

#include <set>
#include <string>
#include <utility>

namespace ggui {

using LineSet = std::set<std::pair<int, int>>; // (hunk index, line index in hunk)

// All changed lines of hunk `h`.
LineSet hunkLines(const core::DiffFile& file, int h);
// Empty string when nothing selected is a change.
std::string buildPatch(const core::DiffFile& file, LineSet selected, bool reverse);

} // namespace ggui
