#include "libgg/Hooks.hpp"

#include "libgg/Conflicts.hpp"
#include "libgg/Git2.hpp"
#include "libgg/GitRunner.hpp"
#include "libgg/Journal.hpp"
#include "libgg/NativeRebase.hpp"
#include "libgg/Operation.hpp"
#include "libgg/Process.hpp"

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <istream>
#include <mutex>
#include <ostream>
#include <sstream>

namespace gg::hooks {

namespace fs = std::filesystem;
using namespace gg::git2;

namespace {

constexpr const char* kWrapperMarker = "# ggui managed hook";
std::string g_forcedMode;

const char* kRunner = R"(#!/bin/sh
# ggui managed hooks runner (installed by "git gg hooks install", removed by uninstall).
# Does nothing when git-gg is not installed, so plain git never breaks because of ggui.
name="$1"
shift
if command -v git-gg >/dev/null 2>&1; then
    exec git-gg hook "$name" "$@"
fi
if [ "$name" = "pre-push" ]; then
    echo "ggui hooks: git-gg not found on PATH; skipping the first-class conflict check" >&2
fi
exit 0
)";

std::string shellQuote(const std::string& s)
{
    std::string out = "'";
    for (char c : s) {
        if (c == '\'')
            out += "'\\''";
        else
            out.push_back(c);
    }
    return out + "'";
}

std::string readFile(const fs::path& p)
{
    std::ifstream f(p, std::ios::binary);
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

void writeExecutable(const fs::path& p, const std::string& text)
{
    fs::create_directories(p.parent_path());
    {
        std::ofstream f(p, std::ios::binary | std::ios::trunc);
        f << text;
    }
    std::error_code ec;
    fs::permissions(p, fs::perms::owner_all | fs::perms::group_read | fs::perms::group_exec | fs::perms::others_read
            | fs::perms::others_exec,
        ec);
}

struct Paths {
    fs::path commonDir;
    fs::path hooksDir;
    fs::path runner;
    bool ok = false;
    std::string error;
};

Paths paths(const fs::path& repoDir)
{
    Paths p;
    const RunResult common = git(repoDir, {"rev-parse", "--path-format=absolute", "--git-common-dir"});
    const RunResult hooksDir = git(repoDir, {"rev-parse", "--path-format=absolute", "--git-path", "hooks"});
    if (!common.ok() || !hooksDir.ok()) {
        p.error = common.ok() ? hooksDir.message() : common.message();
        return p;
    }
    p.commonDir = fs::path(trim(common.out));
    p.hooksDir = fs::path(trim(hooksDir.out));
    p.runner = p.commonDir / "gg" / "hooks" / "run";
    p.ok = true;
    return p;
}

std::string wrapperScript(const fs::path& runner, const std::string& name)
{
    return std::string("#!/bin/sh\n") + kWrapperMarker
        + ": runs the ggui hooks runner, then the previous hook (if any).\n"
          "# \"git gg hooks uninstall\" restores the previous hook.\n"
          "input=$(cat; printf x)\n"
          "input=${input%x}\n"
          "status=0\n"
          "printf '%s' \"$input\" | "
        + shellQuote(runner.generic_string()) + " " + name
        + " \"$@\" || status=$?\n"
          "prev=\"$0.gg-previous\"\n"
          "if [ -x \"$prev\" ]; then\n"
          "    printf '%s' \"$input\" | \"$prev\" \"$@\" || status=$?\n"
          "fi\n"
          "exit $status\n";
}

bool isWrapper(const fs::path& p) { return readFile(p).find(kWrapperMarker) != std::string::npos; }

Mode activeMode() { return configHooksSupported() ? Mode::Config : Mode::Wrapper; }

std::string configCommand(const fs::path& runner, const std::string& name)
{
    return shellQuote(runner.generic_string()) + " " + name;
}

} // namespace

const std::vector<std::string>& managedHooks()
{
    static const std::vector<std::string> names{"reference-transaction", "post-checkout", "post-merge",
        "post-rewrite", "post-commit", "pre-push"};
    return names;
}

void forceMode(const std::string& mode) { g_forcedMode = mode; }

bool configHooksSupported()
{
    std::string forced = g_forcedMode;
    if (forced.empty())
        if (const char* e = std::getenv("GG_HOOKS_MODE"))
            forced = e;
    if (forced == "wrapper")
        return false;
    if (forced == "config")
        return true;
    static std::once_flag once;
    static bool supported = false;
    std::call_once(once, [] {
        std::error_code ec;
        const fs::path dir = fs::temp_directory_path(ec) / ("gg-hook-probe-" + journal::Journal::newOperationId());
        fs::create_directories(dir, ec);
        if (git(dir, {"init", "-q", "."}).ok() && git(dir, {"config", "hook.ggprobe.command", "echo gg-probe"}).ok()
            && git(dir, {"config", "hook.ggprobe.event", "pre-commit"}).ok()) {
            const RunResult r = git(dir, {"hook", "run", "--ignore-missing", "pre-commit"});
            supported = (r.out + r.err).find("gg-probe") != std::string::npos;
        }
        fs::remove_all(dir, ec);
    });
    return supported;
}

Status status(const fs::path& repoDir)
{
    Status s;
    s.gitGgFound = !findInPath("git-gg").empty();
    const Paths p = paths(repoDir);
    if (!p.ok)
        return s;
    s.hooksDir = p.hooksDir;
    s.mode = activeMode();
    Repository repo = openRepository(repoDir);
    Config cfg = repositoryConfig(repo.get());
    for (const auto& name : managedHooks()) {
        const bool inConfig = configString(cfg.get(), ("hook.ggui-" + name + ".command").c_str()).has_value();
        const bool wrapper = fs::exists(p.hooksDir / name) && isWrapper(p.hooksDir / name);
        if (inConfig)
            s.mode = Mode::Config;
        else if (wrapper)
            s.mode = Mode::Wrapper;
        if (inConfig || wrapper)
            s.present.push_back(name);
    }
    s.installed = s.present.size() == managedHooks().size() && fs::exists(p.runner);
    s.partial = !s.present.empty() && !s.installed;
    return s;
}

bool install(const fs::path& repoDir, std::string& error)
{
    const Paths p = paths(repoDir);
    if (!p.ok) {
        error = p.error;
        return false;
    }
    if (hooks::status(repoDir).installed)
        return true;
    writeExecutable(p.runner, kRunner);
    if (activeMode() == Mode::Config) {
        for (const auto& name : managedHooks()) {
            const RunResult a = git(repoDir, {"config", "--local", "hook.ggui-" + name + ".command", configCommand(p.runner, name)});
            const RunResult b = git(repoDir, {"config", "--local", "hook.ggui-" + name + ".event", name});
            if (!a.ok() || !b.ok()) {
                error = a.ok() ? b.message() : a.message();
                return false;
            }
        }
        return true;
    }
    std::error_code ec;
    fs::create_directories(p.hooksDir, ec);
    for (const auto& name : managedHooks()) {
        const fs::path hook = p.hooksDir / name;
        if (fs::exists(hook) && !isWrapper(hook)) {
            fs::rename(hook, p.hooksDir / (name + ".gg-previous"), ec);
            if (ec) {
                error = "cannot move " + hook.string() + ": " + ec.message();
                return false;
            }
        }
        writeExecutable(hook, wrapperScript(p.runner, name));
    }
    return true;
}

bool uninstall(const fs::path& repoDir, std::string& error)
{
    const Paths p = paths(repoDir);
    if (!p.ok) {
        error = p.error;
        return false;
    }
    Repository repo = openRepository(repoDir);
    Config cfg = repositoryConfig(repo.get());
    std::error_code ec;
    for (const auto& name : managedHooks()) {
        if (configString(cfg.get(), ("hook.ggui-" + name + ".command").c_str()) ||
            configString(cfg.get(), ("hook.ggui-" + name + ".event").c_str()))
            git(repoDir, {"config", "--local", "--remove-section", "hook.ggui-" + name});
        const fs::path hook = p.hooksDir / name;
        if (fs::exists(hook) && isWrapper(hook)) {
            fs::remove(hook, ec);
            const fs::path previous = p.hooksDir / (name + ".gg-previous");
            if (fs::exists(previous))
                fs::rename(previous, hook, ec);
        }
    }
    fs::remove(p.runner, ec);
    fs::remove(p.runner.parent_path(), ec); // only when empty
    return true;
}

std::vector<ConflictedCommit> conflictedOutgoing(const fs::path& repoDir, const std::string& localOid,
    const std::string& remote, const std::string& remoteOid)
{
    std::vector<ConflictedCommit> out;
    Repository repo = openRepository(repoDir);
    auto local = fromHex(localOid);
    if (!local)
        return out;
    git_revwalk* raw = nullptr;
    check(git_revwalk_new(&raw, repo.get()), "git_revwalk_new");
    Revwalk walk(raw);
    check(git_revwalk_push(walk.get(), &*local), "git_revwalk_push");
    if (auto r = fromHex(remoteOid); r && !isZero(*r))
        if (git_revwalk_hide(walk.get(), &*r) != 0)
            git_error_clear();
    if (!remote.empty())
        if (git_revwalk_hide_glob(walk.get(), ("refs/remotes/" + remote + "/*").c_str()) != 0)
            git_error_clear();
    conflicts::Cache cache{fs::path(git_repository_commondir(repo.get()))};
    git_oid oid;
    while (git_revwalk_next(&oid, walk.get()) == 0) {
        const auto files = conflicts::commitConflicts(repo.get(), oid, cache);
        if (files.empty())
            continue;
        ConflictedCommit c;
        c.id = toHex(oid);
        Commit commit = lookupCommit(repo.get(), oid);
        const char* summary = git_commit_summary(commit.get());
        c.subject = summary ? summary : "";
        for (const auto& f : files)
            c.files.push_back(f.path);
        out.push_back(std::move(c));
    }
    git_error_clear();
    return out;
}

int runHook(const std::string& name, const std::vector<std::string>& args, std::istream& in, std::ostream& out,
    std::ostream& err)
{
    (void)out;
    const bool journalHook = name != "pre-push";
    if (journalHook && std::getenv("GG_NO_JOURNAL"))
        return 0; // fast exit when the journal is disabled
    git2::initLibrary();
    git_repository* rawRepo = nullptr;
    if (git_repository_open_ext(&rawRepo, ".", GIT_REPOSITORY_OPEN_FROM_ENV, nullptr) != 0) {
        git_error_clear();
        return 0; // never break git
    }
    Repository repo(rawRepo);
    const fs::path commonDir = git_repository_commondir(repo.get());
    const std::string wt = worktreeKey(repo.get());
    journal::Journal journal{commonDir};

    const char* ggOperation = std::getenv("GG_OPERATION");
    const bool joined = ggOperation && *ggOperation;
    auto operationId = [&](bool create) -> std::string {
        if (joined)
            return ggOperation; // loop guard: join the ggui / git-gg operation
        // A native rebase is one operation from start to finish, over several git commands
        // (git rebase -i, git rebase --continue, ...): join the one that saw it start.
        if (create)
            native::closeFinishedGroup(repo.get(), journal);
        if (std::string group = native::groupOperation(repo.get()); !group.empty())
            return group;
        const ProcessInfo parent = parentProcess();
        const std::string id = "git-" + std::to_string(parent.pid) + "-" + std::to_string(parent.start);
        if (create && !journal.hasOpenOperation(id)) {
            journal::Operation op;
            op.id = id;
            op.src = "git";
            op.cmd = parent.commandLine;
            op.label = parent.commandLine.empty() ? std::string("git") : parent.commandLine;
            op.wt = wt;
            journal.begin(op);
        }
        if (create && !native::rebaseIdentity(repo.get()).empty())
            native::rememberGroup(repo.get(), journal, id);
        return id;
    };

    if (name == "reference-transaction") {
        const std::string state = args.empty() ? std::string() : args[0];
        std::vector<journal::RefChange> changes;
        std::vector<std::string> names; // as git wrote them (HEAD, not the per-worktree key)
        std::string line;
        while (std::getline(in, line)) {
            std::istringstream fields(line);
            journal::RefChange c;
            if (!(fields >> c.oldValue >> c.newValue >> c.ref))
                continue;
            const std::string raw = c.ref;
            if (c.ref == "HEAD")
                c.ref = journal::headKey(wt);
            if (c.ref.rfind("refs/", 0) != 0 && c.ref != journal::headKey(wt))
                continue; // pseudo refs (ORIG_HEAD, FETCH_HEAD, …) are not tracked
            changes.push_back(std::move(c));
            names.push_back(raw);
        }
        if (changes.empty())
            return 0;
        // git reports a symbolic ref's old target as the null id. While the transaction is only
        // prepared the ref still has it: remember it for the committed call.
        const fs::path pending = commonDir / "gg" / ("symref-" + operationId(false));
        auto isNull = [](const std::string& v) { return v.find_first_not_of('0') == std::string::npos; };
        if (state == "prepared") {
            std::string text;
            for (size_t i = 0; i < changes.size(); ++i) {
                // A symbolic ref being re-pointed (git reports its old value as null), or HEAD
                // being detached from a branch (git reports the branch's commit; the branch
                // itself is not part of the transaction): its old value is the symbolic target.
                const bool repoint = changes[i].newValue.rfind("ref:", 0) == 0 && isNull(changes[i].oldValue);
                const bool detach = names[i] == "HEAD" && changes[i].newValue.rfind("ref:", 0) != 0;
                if (!repoint && !detach)
                    continue;
                git_reference* ref = nullptr;
                if (git_reference_lookup(&ref, repo.get(), names[i].c_str()) == 0) {
                    const char* target = git_reference_type(ref) == GIT_REFERENCE_SYMBOLIC
                        ? git_reference_symbolic_target(ref)
                        : nullptr;
                    const bool referentMoves = target
                        && std::find(names.begin(), names.end(), std::string(target)) != names.end();
                    if (target && !(detach && referentMoves))
                        text += changes[i].ref + " ref:" + target + "\n";
                    git_reference_free(ref);
                } else {
                    git_error_clear();
                }
            }
            if (!text.empty()) {
                std::error_code ec;
                fs::create_directories(pending.parent_path(), ec);
                std::ofstream(pending, std::ios::app) << text;
            }
            return 0;
        }
        if (state != "committed") {
            std::error_code ec;
            fs::remove(pending, ec); // aborted
            return 0;
        }
        if (fs::exists(pending)) {
            std::ifstream f(pending);
            std::string ref, value;
            while (f >> ref >> value)
                for (auto& c : changes)
                    if (c.ref == ref && (isNull(c.oldValue) || c.newValue.rfind("ref:", 0) != 0))
                        c.oldValue = value;
            f.close();
            std::error_code ec;
            fs::remove(pending, ec);
        }
        journal.appendRefs(operationId(true), changes);
        return 0;
    }
    if (name == "post-rewrite") {
        std::vector<std::pair<std::string, std::string>> map;
        std::string line;
        while (std::getline(in, line)) {
            std::istringstream fields(line);
            std::string a, b;
            if (fields >> a >> b)
                map.emplace_back(a, b);
        }
        const std::string id = operationId(true);
        if (!map.empty())
            journal.appendRewrites(id, map);
        // `git rebase` runs post-rewrite when it finishes: its operation ends here (ggui ends its
        // own when the git command it runs returns).
        if (!joined && !args.empty() && args[0] == "rebase" && native::groupOperation(repo.get()) == id) {
            // An operation ggui started has the index from before the rebase: add the final one.
            if (id.rfind("git-", 0) != 0 && git_repository_workdir(repo.get()))
                journal.appendIndex(id, journal::IndexChange{wt, "", indexTree(git_repository_workdir(repo.get())), true});
            native::finishGroup(repo.get(), journal);
        }
        return 0;
    }
    if (name == "post-checkout" || name == "post-merge" || name == "post-commit") {
        std::string ignored;
        while (std::getline(in, ignored)) {
        }
        return 0;
    }
    if (name == "pre-push") {
        const std::string remote = args.empty() ? std::string() : args[0];
        const fs::path repoDir = git_repository_workdir(repo.get()) ? fs::path(git_repository_workdir(repo.get()))
                                                                    : fs::path(git_repository_path(repo.get()));
        std::vector<ConflictedCommit> found;
        std::string line;
        while (std::getline(in, line)) {
            std::istringstream fields(line);
            std::string localRef, localOid, remoteRef, remoteOid;
            if (!(fields >> localRef >> localOid >> remoteRef >> remoteOid))
                continue;
            if (localOid.find_first_not_of('0') == std::string::npos)
                continue; // deleting a remote ref pushes no commits
            for (auto& c : conflictedOutgoing(repoDir, localOid, remote, remoteOid))
                found.push_back(std::move(c));
        }
        if (found.empty())
            return 0;
        err << "ggui: refusing to push commits with first-class conflicts:\n";
        for (const auto& c : found) {
            err << "  " << c.id.substr(0, 10) << " " << c.subject << "\n";
            for (const auto& f : c.files)
                err << "      " << f << "\n";
        }
        err << "Resolve the conflicts first (or bypass with git push --no-verify).\n";
        return 1;
    }
    err << "git gg hook: unknown hook '" << name << "'\n";
    return 0;
}

} // namespace gg::hooks
