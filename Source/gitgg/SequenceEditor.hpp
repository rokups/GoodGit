// git-gg as GIT_SEQUENCE_EDITOR / GIT_EDITOR for ggui's native interactive rebase (P3-19): writes
// the todo and messages ggui prepared (GG_SEQUENCE_DIR; see libgg/NativeRebase.hpp).
#pragma once

#include <string>

namespace gitgg {

int runSequenceEditor(const std::string& file);

} // namespace gitgg
