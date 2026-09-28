#include "SequenceEditor.hpp"

#include <libgg/NativeRebase.hpp>

#include <iostream>

namespace gitgg {

int runSequenceEditor(const std::string& file)
{
    std::string error;
    const int status = gg::native::sequenceEditor(file, error);
    if (status != 0)
        std::cerr << "git gg sequence-editor: " << error << "\n";
    return status;
}

} // namespace gitgg
