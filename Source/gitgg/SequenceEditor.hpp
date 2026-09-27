// git-gg as GIT_SEQUENCE_EDITOR / GIT_EDITOR for ggui's native interactive rebase (P3-19).
#pragma once

#include <string>

namespace gitgg {

int runSequenceEditor(const std::string& file);

} // namespace gitgg
