#include "libgg/HooksLegacy.hpp"

#include "libgg/Git2.hpp"
#include "libgg/GitRunner.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace gg::hooks {

namespace fs = std::filesystem;
using namespace gg::git2;

namespace {

constexpr const char* kWrapperMarker = "# ggui managed hook";

std::string readFile(const fs::path& p)
{
    std::ifstream f(p, std::ios::binary);
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

bool isWrapper(const fs::path& p)
{
    std::error_code ec;
    return fs::is_regular_file(p, ec) && readFile(p).find(kWrapperMarker) != std::string::npos;
}

// Set only by a local-config entry: an entry from a global or system config is not ours.
bool localConfigHas(git_config* cfg, const std::string& name)
{
    git_config_entry* entry = nullptr;
    if (git_config_get_entry(&entry, cfg, name.c_str()) != 0) {
        git_error_clear();
        return false;
    }
    const bool local = entry->level == GIT_CONFIG_LEVEL_LOCAL;
    git_config_entry_free(entry);
    return local;
}

fs::path workDir(git_repository* repo)
{
    const char* workdir = git_repository_workdir(repo);
    return workdir ? fs::path(workdir) : fs::path(git_repository_path(repo));
}

// Where git looks for hooks: core.hooksPath (relative to the working tree), else the common
// directory's hooks.
fs::path hooksDir(git_repository* repo, git_config* cfg)
{
    if (auto configured = configString(cfg, "core.hooksPath"); configured && !configured->empty()) {
        std::string value = *configured;
        if (value.rfind("~/", 0) == 0) {
            const char* home = std::getenv("HOME");
            if (!home)
                home = std::getenv("USERPROFILE");
            if (home)
                value = std::string(home) + value.substr(1);
        }
        fs::path dir(value);
        return dir.is_absolute() ? dir : workDir(repo) / dir;
    }
    return fs::path(git_repository_commondir(repo)) / "hooks";
}

fs::path runnerPath(git_repository* repo) { return fs::path(git_repository_commondir(repo)) / "gg" / "hooks" / "run"; }

} // namespace

const std::vector<std::string>& managedHooks()
{
    static const std::vector<std::string> names{"reference-transaction", "post-checkout", "post-merge",
        "post-rewrite", "post-commit", "pre-push", "pre-commit"};
    return names;
}

bool installed(git_repository* repo)
{
    Config cfg = repositoryConfig(repo);
    const fs::path dir = hooksDir(repo, cfg.get());
    std::error_code ec;
    for (const auto& name : managedHooks()) {
        const std::string section = "hook.ggui-" + name;
        if (localConfigHas(cfg.get(), section + ".command") || localConfigHas(cfg.get(), section + ".event"))
            return true;
        if (isWrapper(dir / name))
            return true;
    }
    return fs::exists(runnerPath(repo), ec);
}

bool uninstall(git_repository* repo, std::string& error)
{
    Config cfg = repositoryConfig(repo);
    const fs::path dir = hooksDir(repo, cfg.get());
    const fs::path runner = runnerPath(repo);
    std::error_code ec;
    // A file that cannot be removed or moved back fails the uninstall: the install is still there.
    auto failed = [&](const std::string& what, const fs::path& path) {
        if (ec) {
            if (!error.empty())
                error += "; ";
            error += "cannot " + what + " " + path.string() + ": " + ec.message();
            ec.clear();
        }
    };
    for (const auto& name : managedHooks()) {
        const std::string section = "hook.ggui-" + name;
        if (localConfigHas(cfg.get(), section + ".command") || localConfigHas(cfg.get(), section + ".event")) {
            const RunResult r = git(workDir(repo), {"config", "--local", "--remove-section", section});
            if (!r.ok()) {
                error = r.message();
                return false;
            }
        }
        const fs::path hook = dir / name;
        if (isWrapper(hook)) {
            fs::remove(hook, ec);
            failed("remove", hook);
            const fs::path previous = dir / (name + ".gg-previous");
            if (fs::exists(previous, ec) && !isWrapper(hook))
                fs::rename(previous, hook, ec);
            failed("restore", previous);
        }
    }
    fs::remove(runner, ec);
    failed("remove", runner);
    fs::remove(runner.parent_path(), ec); // only when empty
    ec.clear();
    return error.empty();
}

} // namespace gg::hooks
