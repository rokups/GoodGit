#include "libgg/Legacy.hpp"

#include "libgg/Git2.hpp"
#include "libgg/GitRunner.hpp"

#include <optional>
#include <vector>

namespace gg {

namespace {

int collectName(const char* name, void* payload)
{
    static_cast<std::vector<std::string>*>(payload)->emplace_back(name);
    return 0;
}

} // namespace

bool removeLegacyGgRefs(git_repository* repo, std::string* error, int* deleted)
{
    if (deleted)
        *deleted = 0;
    std::vector<std::string> names;
    if (git_reference_foreach_glob(repo, "refs/gg/*", collectName, &names) != 0) {
        git_error_clear();
        return false;
    }
    if (names.empty())
        return false;
    std::string input;
    for (const auto& name : names)
        input += "delete " + name + "\n";
    const char* workdir = git_repository_workdir(repo);
    RunRequest request;
    request.args = {"git", "update-ref", "--no-deref", "--stdin"};
    request.cwd = workdir ? workdir : git_repository_path(repo);
    request.input = std::move(input);
    request.env.emplace_back("GG_OPERATION", std::nullopt); // never joins a journaled operation
    request.env.emplace_back("GG_NO_JOURNAL", "1");          // nor makes managed hooks open one
    const RunResult result = run(request);
    if (!result.ok()) {
        if (error)
            *error = result.message();
        return false;
    }
    if (deleted)
        *deleted = static_cast<int>(names.size());
    return true;
}

} // namespace gg
