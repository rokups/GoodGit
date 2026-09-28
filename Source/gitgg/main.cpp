// git-gg: the `git gg` subcommand (REBUILD_PLAN §6). Deliberately minimal and fast to start:
// hooks run it on every ref update, so there is no repository scan beyond what a command needs.
//
//   git gg new [-m MSG] [--detach] [PARENT...]
//   git gg undo | redo | op log
//   git gg conflicts [REV]           exit 1 when REV has first-class conflicts
//   git gg hooks install|uninstall|status
//   git gg hook <name> [ARGS...]     entry point for the managed hooks
//   git gg ui [PATH]
//   git gg sequence-editor FILE      sequence.editor for plain git rebase -i (ggui's todo editor), and
//                                    internal GIT_SEQUENCE_EDITOR / GIT_EDITOR of ggui's git rebase -i
//   git gg help [COMMAND]
//
// Exit codes follow git: 0 success, 1 "found something" / refused, 128 fatal, 129 usage.

#include "Askpass.hpp"
#include "SequenceEditor.hpp"

#include <libgg/Conflicts.hpp>
#include <libgg/Git2.hpp>
#include <libgg/GitRunner.hpp>
#include <libgg/Hooks.hpp>
#include <libgg/Journal.hpp>
#include <libgg/NewCommit.hpp>
#include <libgg/Operation.hpp>
#include <libgg/Rewrite.hpp>
#include <libgg/Undo.hpp>

#include <CLI/CLI.hpp>

#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <iostream>
#include <set>
#include <map>

namespace {

namespace fs = std::filesystem;

constexpr int kFatal = 128;
constexpr int kUsage = 129;

const std::map<std::string, std::string>& helpTexts()
{
    static const std::map<std::string, std::string> texts{
        {"new", "usage: git gg new [-m <msg>] [--detach] [<parent>...]\n"
                "   or: git gg new [-m <msg>] (--before | --after) <commit>\n\n"
                "Creates an empty commit on the parents (default HEAD). When HEAD is attached and the first\n"
                "parent is HEAD, the branch advances; otherwise HEAD detaches at the new commit.\n"
                "Several parents create an empty merge commit.\n\n"
                "    -m, --message <msg>   commit message\n"
                "    --detach              leave branches alone\n"
                "    --before <commit>     insert the new commit before <commit> (descendants rebased)\n"
                "    --after <commit>      insert it after <commit> (descendants rebased)\n"},
        {"undo", "usage: git gg undo\n\nUndoes the last operation recorded in the undo journal (ggui, git gg, or plain\n"
                 "git when the managed hooks are installed). Refs and the index are restored; the working\n"
                 "tree only when nothing would be lost.\n"},
        {"redo", "usage: git gg redo\n\nRedoes the last undone operation (an undo of the undo).\n"},
        {"op", "usage: git gg op log\n\nLists the operations in the undo journal, newest first.\n"},
        {"conflicts", "usage: git gg conflicts [<rev>]\n\nLists files with first-class conflicts in <rev> (default HEAD).\n"
                      "Exit status 1 when there are any.\n"},
        {"hooks", "usage: git gg hooks (install | uninstall | status)\n\nManages the chained ggui hooks of this repository.\n"},
        {"hook", "usage: git gg hook <hook-name> [<args>...]\n\nEntry point called by the managed hooks; not for interactive use.\n"},
        {"ui", "usage: git gg ui [<path>]\n\nStarts ggui on the repository.\n"},
        {"sequence-editor", "usage: git gg sequence-editor <file>\n\n"
                            "As sequence.editor (git config sequence.editor \"git gg sequence-editor\", or ggui's Settings):\n"
                            "plain git rebase -i shows its todo list in ggui's todo editor and goes on with the list saved\n"
                            "there. A ggui that has the repository open shows it; otherwise a new ggui is started (GG_GGUI\n"
                            "names the program, default: the ggui next to git-gg). Cancel stops the rebase (empty list; for\n"
                            "git rebase --edit-todo the list stays). Without a display git's own editor is used.\n\n"
                            "Internal: also GIT_SEQUENCE_EDITOR / GIT_EDITOR of ggui's native interactive rebase\n"
                            "(GG_SEQUENCE_DIR set by ggui): the todo it prepared goes to git-rebase-todo, a message typed in\n"
                            "its todo editor to the commit git is working on.\n"},
    };
    return texts;
}

// "git gg help" / "git gg --help": the commands with one line each.
const char* kOverview =
    "usage: git gg <command> [<args>]\n\n"
    "Undo, first-class conflicts and helpers for ggui.\n\n"
    "    new         create an empty commit (on HEAD, detached, or a merge)\n"
    "    undo        undo the last operation from the undo journal\n"
    "    redo        redo the last undone operation\n"
    "    op log      list the operations in the undo journal\n"
    "    conflicts   list first-class conflicts in a commit (exit 1 when any)\n"
    "    hooks       install, uninstall or show the managed hooks\n"
    "    ui          start ggui on the repository\n"
    "    sequence-editor  ggui's todo editor as sequence.editor for git rebase -i\n"
    "    help        show help for a command: git gg help <command>\n";

int fatal(const std::string& message)
{
    std::cerr << "fatal: " << message << "\n";
    return kFatal;
}

gg::git2::Repository openHere()
{
    gg::git2::initLibrary();
    git_repository* raw = nullptr;
    if (git_repository_open_ext(&raw, ".", GIT_REPOSITORY_OPEN_FROM_ENV, nullptr) != 0)
        throw std::runtime_error("not a git repository (or any of the parent directories)");
    return gg::git2::Repository(raw);
}

std::string timeText(std::int64_t ms)
{
    const std::time_t t = static_cast<std::time_t>(ms / 1000);
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tm);
    return buf;
}

int cmdNew(const std::vector<std::string>& parents, const std::string& message, bool detach)
{
    auto repo = openHere();
    gg::OperationRecorder recorder(repo.get(), "git-gg", "new", false);
    recorder.begin();
    gg::NewCommitOptions options;
    options.parents = parents;
    options.message = message;
    options.detach = detach;
    const auto result = gg::newCommit(repo.get(), options);
    recorder.finish(result.ok);
    if (!result.ok)
        return fatal(result.error);
    std::cout << result.commit << "\n";
    return 0;
}

// new --before/--after REV: an empty commit inserted there, the descendants rebased onto it.
int cmdInsert(const std::string& rev, bool before, const std::string& message)
{
    auto repo = openHere();
    const auto oid = gg::git2::resolve(repo.get(), rev);
    if (!oid)
        return fatal("bad revision '" + rev + "'");
    const bool bare = git_repository_is_bare(repo.get()) == 1;
    gg::OperationRecorder recorder(repo.get(), "git-gg", std::string("new ") + (before ? "--before " : "--after ") + rev, !bare);
    recorder.begin();
    const fs::path dir = bare ? fs::path(git_repository_path(repo.get())) : fs::path(git_repository_workdir(repo.get()));
    gg::rewrite::Plan plan = gg::rewrite::insertPlan(repo.get(), gg::git2::toHex(*oid), before, message);
    gg::rewrite::Rewriter rewriter(dir);
    gg::rewrite::Result result = rewriter.compute(plan);
    std::string error = result.error;
    const bool ok = result.ok && rewriter.apply(plan, result, error);
    recorder.finish(ok, result.headAfter != result.headBefore);
    if (!ok)
        return fatal(error);
    std::cout << result.steps["inserted"] << "\n";
    return 0;
}

int cmdUndo(bool redo)
{
    auto repo = openHere();
    const auto result = gg::undo(repo.get(), redo, "git-gg");
    if (result.nothing) {
        std::cerr << (redo ? "Nothing to redo\n" : "Nothing to undo\n");
        return 1;
    }
    if (!result.ok)
        return fatal(result.error + (result.wouldLoseData ? "\nhint: stash your changes first (git stash)" : ""));
    std::cout << (redo ? "Redone: " : "Undone: ") << result.label.substr(5) << "\n";
    return 0;
}

int cmdOpLog()
{
    auto repo = openHere();
    gg::journal::Journal journal{fs::path(git_repository_commondir(repo.get()))};
    std::string error;
    size_t skipped = 0;
    const auto ops = journal.read(&error, &skipped);
    if (!error.empty())
        return fatal(error);
    for (auto it = ops.rbegin(); it != ops.rend(); ++it) {
        std::cout << it->id << "  " << timeText(it->time) << "  [" << it->src << "] " << it->label;
        if (!it->ok)
            std::cout << " (failed)";
        std::cout << "\n";
        for (const auto& r : it->refs)
            std::cout << "    " << r.ref << ": " << r.oldValue.substr(0, 12) << " -> " << r.newValue.substr(0, 12) << "\n";
    }
    if (skipped)
        std::cerr << "warning: skipped " << skipped << " unreadable journal line(s)\n";
    return 0;
}

int cmdConflicts(const std::string& rev)
{
    auto repo = openHere();
    const auto oid = gg::git2::resolve(repo.get(), rev.empty() ? "HEAD" : rev);
    if (!oid)
        return fatal("bad revision '" + rev + "'");
    gg::conflicts::Cache cache{fs::path(git_repository_commondir(repo.get()))};
    const auto files = gg::conflicts::commitConflicts(repo.get(), *oid, cache);
    for (const auto& f : files)
        std::cout << f.path << " (" << f.sides << " sides)\n";
    return files.empty() ? 0 : 1;
}

int cmdHooks(const std::string& action)
{
    const fs::path here = fs::current_path();
    std::string error;
    if (action == "install") {
        if (!gg::hooks::install(here, error))
            return fatal(error);
        const auto s = gg::hooks::status(here);
        std::cout << "Installed the ggui hooks (" << (s.mode == gg::hooks::Mode::Config ? "config-defined" : "wrapper scripts")
                  << ").\n";
        return 0;
    }
    if (action == "uninstall") {
        if (!gg::hooks::uninstall(here, error))
            return fatal(error);
        std::cout << "Removed the ggui hooks.\n";
        return 0;
    }
    const auto s = gg::hooks::status(here);
    std::cout << (s.installed ? "installed" : s.partial ? "partially installed" : "not installed");
    if (s.installed || s.partial)
        std::cout << " (" << (s.mode == gg::hooks::Mode::Config ? "config-defined" : "wrapper scripts in " + s.hooksDir.string())
                  << ")";
    std::cout << "\n";
    if (!s.gitGgFound)
        std::cout << "warning: git-gg is not on PATH; the hooks do nothing\n";
    return s.installed ? 0 : 1;
}

int cmdUi(const std::string& path)
{
    gg::RunRequest r;
    const fs::path self = gg::findInPath("git-gg");
    const fs::path ggui = self.empty() ? fs::path("ggui") : self.parent_path() / "ggui";
    r.args = {ggui.string(), fs::weakly_canonical(path.empty() ? fs::current_path() : fs::absolute(path)).string()};
    r.gitEnvironment = false;
    r.cLocale = false;
    const auto res = gg::run(r);
    if (res.startFailed)
        return fatal("cannot start ggui: " + res.err);
    return res.exitCode;
}

} // namespace

int main(int argc, char** argv)
{
    // Askpass mode: git runs $GIT_ASKPASS with the prompt as the only argument. A command name
    // ("undo", "help", ...) run from a terminal inside ggui is never a prompt.
    static const std::set<std::string> kCommands{"new", "undo", "redo", "op", "conflicts", "hooks", "hook", "ui",
        "sequence-editor", "help"};
    if (std::getenv("GG_ASKPASS_ENDPOINT") && argc == 2 && !kCommands.count(argv[1]) && argv[1][0] != '-')
        return gitgg::runAskpass(argv[1]);

    CLI::App app{"git gg: undo, first-class conflicts and helpers for ggui"};
    app.name("git gg");
    app.require_subcommand(1);
    app.set_help_flag("-h,--help", "Show help");

    auto* newCmd = app.add_subcommand("new", "Create an empty commit");
    std::string message;
    bool detach = false;
    std::vector<std::string> parents;
    newCmd->add_option("-m,--message", message, "Commit message");
    newCmd->add_flag("--detach", detach, "Leave branches alone");
    newCmd->add_option("parents", parents, "Parent revisions (default HEAD)");
    std::string insertBefore, insertAfter;
    newCmd->add_option("--before", insertBefore, "Insert before this commit (its descendants are rebased)");
    newCmd->add_option("--after", insertAfter, "Insert after this commit (its descendants are rebased)");

    app.add_subcommand("undo", "Undo the last operation");
    app.add_subcommand("redo", "Redo the last undone operation");
    auto* op = app.add_subcommand("op", "Operation log");
    op->require_subcommand(1);
    op->add_subcommand("log", "List journal entries");

    auto* conflictsCmd = app.add_subcommand("conflicts", "List first-class conflicts");
    std::string rev;
    conflictsCmd->add_option("rev", rev, "Revision (default HEAD)");

    auto* hooksCmd = app.add_subcommand("hooks", "Manage the ggui hooks");
    std::string hooksAction;
    hooksCmd->add_option("action", hooksAction, "install, uninstall or status")
        ->required()
        ->check(CLI::IsMember({"install", "uninstall", "status"}));

    auto* hookCmd = app.add_subcommand("hook", "Hook entry point (internal)");
    std::string hookName;
    std::vector<std::string> hookArgs;
    hookCmd->add_option("name", hookName)->required();
    hookCmd->add_option("args", hookArgs);
    hookCmd->allow_extras();

    auto* uiCmd = app.add_subcommand("ui", "Start ggui");
    std::string uiPath;
    uiCmd->add_option("path", uiPath);

    auto* seqCmd = app.add_subcommand("sequence-editor", "ggui's todo editor as sequence.editor for git rebase -i");
    std::string seqFile;
    seqCmd->add_option("file", seqFile)->required();

    auto* helpCmd = app.add_subcommand("help", "Show help for a command");
    std::string helpTopic;
    helpCmd->add_option("command", helpTopic);

    try {
        app.parse(argc, argv);
    } catch (const CLI::CallForHelp&) {
        std::cout << kOverview;
        return 0;
    } catch (const CLI::ParseError& e) {
        std::cerr << "error: " << e.what() << "\n\n" << kOverview;
        return kUsage;
    }

    try {
        if (*helpCmd) {
            if (helpTopic.empty()) {
                std::cout << kOverview;
                return 0;
            }
            auto it = helpTexts().find(helpTopic);
            if (it == helpTexts().end()) {
                std::cerr << "git gg: '" << helpTopic << "' is not a git gg command\n";
                return 1;
            }
            std::cout << it->second;
            return 0;
        }
        if (*newCmd) {
            if (!insertBefore.empty() || !insertAfter.empty()) {
                if (!parents.empty() || detach || (!insertBefore.empty() && !insertAfter.empty())) {
                    std::cerr << "error: --before/--after take one commit and no parents or --detach\n";
                    return kUsage;
                }
                return insertBefore.empty() ? cmdInsert(insertAfter, false, message) : cmdInsert(insertBefore, true, message);
            }
            return cmdNew(parents, message, detach);
        }
        if (app.got_subcommand("undo"))
            return cmdUndo(false);
        if (app.got_subcommand("redo"))
            return cmdUndo(true);
        if (*op)
            return cmdOpLog();
        if (*conflictsCmd)
            return cmdConflicts(rev);
        if (*hooksCmd)
            return cmdHooks(hooksAction);
        if (*hookCmd) {
            for (const auto& extra : hookCmd->remaining())
                hookArgs.push_back(extra);
            return gg::hooks::runHook(hookName, hookArgs, std::cin, std::cout, std::cerr);
        }
        if (*uiCmd)
            return cmdUi(uiPath);
        if (*seqCmd)
            return gitgg::runSequenceEditor(seqFile);
    } catch (const std::exception& e) {
        return fatal(e.what());
    }
    return kUsage;
}
