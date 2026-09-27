#include "SequenceEditor.hpp"

#include <iostream>

namespace gitgg {

int runSequenceEditor(const std::string& file)
{
    // Implemented with the native interactive rebase engine (P3-19).
    std::cerr << "git gg sequence-editor: not available yet (" << file << ")\n";
    return 1;
}

} // namespace gitgg
