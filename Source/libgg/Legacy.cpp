#include "libgg/Legacy.hpp"

#include "libgg/Git2.hpp"
#include "libgg/GitRunner.hpp"
#include "libgg/HooksLegacy.hpp"

#include <exception>
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

LegacyMigration migrateLegacy(git_repository* repo)
{
    LegacyMigration result;
    // The hooks first: a stale install would otherwise see the ref deletion.
    try {
        if (hooks::installed(repo)) {
            std::string error;
            if (hooks::uninstall(repo, error))
                result.hooksRemoved = true;
            else
                result.error = "could not uninstall the managed git hooks: " + error;
        }
    } catch (const std::exception& e) {
        result.error = std::string("could not uninstall the managed git hooks: ") + e.what();
    }
    std::string error;
    if (!removeLegacyGgRefs(repo, &error, &result.refsDeleted) && !error.empty() && result.error.empty())
        result.error = "could not remove leftover refs/gg/* refs: " + error;
    return result;
}

} // namespace gg
