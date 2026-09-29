// Interactive rebase on the native engine (§4.13 execution 2, plain rebase -i, §4.10 native):
// `git rebase -i` fed by `git gg sequence-editor`, its stops (edit, break, failing exec, conflicts)
// with Amend and continue, the progress view and Edit remaining todo, a plain `git rebase -i`
// started as a test step, a rebase finished in a terminal, and one journal operation per rebase.
#include "panels/HistoryPanel.hpp"
#include "panels/RebasePanel.hpp"
#include "shell/App.hpp"
#include "shell/Dialogs.hpp"
#include "shell/Session.hpp"
#include "tests/Harness.hpp"
#include "util/Env.hpp"

#include <libgg/GitRunner.hpp>
#include <libgg/Todo.hpp>

#include <algorithm>
#include <fstream>
#include <sstream>

namespace ggtest {

namespace {

namespace todo = gg::todo;
using Rows = std::vector<std::string>;

// c1 … c5 on main, each adding its own file; "part1" at c3 (a stacked branch).
struct Repo {
    fs::path path;
    std::vector<std::string> c; // c[1] = c1 … c[5] = c5
};

Repo makeRepo(Scenario& s)
{
    Repo r;
    r.path = s.fixture(Recipe::Empty);
    r.c.emplace_back();
    const char* files[] = {"", "a", "b", "c", "d", "e"};
    for (int i = 1; i <= 5; ++i) {
        s.commitFile(r.path, std::string(files[i]) + ".txt", std::string(files[i]) + "\n",
            "c" + std::to_string(i) + " add " + files[i]);
        r.c.push_back(s.head(r.path));
        if (i == 3)
            s.git(r.path, {"branch", "part1"});
    }
    return r;
}

std::string historyRow(const std::string& hex) { return "//History/**/###row_" + hex; }
std::string irRow(const std::string& hex) { return "//Interactive rebase/**/###ir_" + hex; }
std::string irWidget(const char* id) { return std::string("//Interactive rebase/###") + id; }
const char* kContinue = "//##Toolbar/Continue##tb_continue";
const char* kAbort = "//##Toolbar/Abort##tb_abort";
const char* kAmendContinue = "//##Toolbar/Amend and continue##tb_amend_continue";
const char* kEditTodo = "//##Toolbar/Edit remaining todo##tb_edit_todo";
const char* kProgress = "//##Toolbar/Progress##tb_rebase_progress";
const char* kCommitConflicts = "//##Toolbar/Commit with conflicts##tb_commit_conflicts";

ggui::RebasePanel& editor(Scenario& s) { return s.session()->rebase(); }

bool editorReady(Scenario& s)
{
    const bool ok = s.waitUntil([&] { return editor(s).isOpen() && editor(s).context() != nullptr; });
    s.ctx->Yield(2);
    return ok;
}

bool rowReady(Scenario& s, const std::string& hex)
{
    return s.waitUntil([&] { return s.session()->history().row(ggui::core::Oid::fromHex(hex)) != nullptr; });
}

// Opens the editor from `hex` with the I key.
bool openFrom(Scenario& s, const std::string& hex)
{
    if (!rowReady(s, hex))
        return false;
    s.ctx->ItemClick(historyRow(hex).c_str());
    s.ctx->KeyPress(ImGuiKey_I);
    return editorReady(s);
}

// The list as "<action> <first word of the subject>" / "<action> <argument>".
Rows rows(Scenario& s)
{
    Rows out;
    const auto& e = editor(s);
    for (const auto& item : e.todo().items) {
        std::string text = todo::actionName(item.action);
        if (item.isCommit()) {
            const std::string& subject = e.context()->commits.at(item.commit).subject;
            text += " " + subject.substr(0, subject.find(' '));
        } else if (!item.arg.empty()) {
            text += " " + item.arg;
        }
        out.push_back(text);
    }
    return out;
}

void click(Scenario& s, const std::string& hex)
{
    s.ctx->ItemClick(irRow(hex).c_str());
    s.ctx->Yield(2);
}

void key(Scenario& s, const std::string& hex, ImGuiKeyChord chord)
{
    click(s, hex);
    s.ctx->KeyPress(chord);
    s.ctx->Yield(2);
}

// git before 2.38 cannot run update-ref rows, and ggui refuses a list with them there ("turn off
// Update refs"). A scenario whose list has one only because of a stacked branch does what that
// message asks on such a git; the branch then stays where it was. Returns whether branches move.
bool withoutUpdateRefsOnOldGit(Scenario& s)
{
    if (s.gitAtLeast(2, 38))
        return true;
    s.ctx->ItemUncheck(irWidget("ir_update_refs").c_str());
    s.ctx->Yield(2);
    return false;
}

// Clicks Start and waits for the editor to close and ggui to settle.
bool start(Scenario& s)
{
    s.ctx->ItemClick(irWidget("ir_start").c_str());
    const bool closed = s.waitUntil([&] { return !editor(s).isOpen(); }, 60.0f);
    s.settle();
    return closed;
}

std::vector<std::string> subjects(Scenario& s, const fs::path& repo, const std::string& rev)
{
    std::vector<std::string> out;
    for (const auto& l : gg::splitLines(s.gitOut(repo, {"log", "--format=%s", rev})))
        if (!l.empty())
            out.push_back(l.substr(0, l.find(' ')));
    return out;
}

// ggui shows the rebase stopped at `action` (the last done row), and the progress data says so.
bool stoppedAt(Scenario& s, const std::string& action)
{
    const bool ok = s.waitUntil([&] {
        const auto snap = s.session()->snapshot();
        return snap && snap->rebase && !snap->rebase->done.empty() && snap->rebase->done.back().action == action;
    });
    s.settle();
    return ok;
}

// No rebase in progress any more, as git and ggui see it.
bool finished(Scenario& s, const fs::path& repo)
{
    const bool ok = s.waitUntil([&] {
        const auto snap = s.session()->snapshot();
        return !fs::exists(repo / ".git" / "rebase-merge") && snap && !snap->rebase
            && snap->state == ggui::core::RepoState::None;
    });
    s.settle();
    return ok;
}

// The progress popup's text.
std::vector<std::string> progress(Scenario& s)
{
    s.ctx->ItemClick(kProgress);
    s.ctx->Yield(2);
    auto lines = s.drawnText("//$FOCUSED");
    s.ctx->KeyPress(ImGuiKey_Escape);
    s.ctx->Yield(2);
    return lines;
}

bool contains(const std::vector<std::string>& lines, const std::string& text)
{
    auto squash = [](std::string t) {
        t.erase(std::remove(t.begin(), t.end(), ' '), t.end());
        return t;
    };
    const std::string want = squash(text);
    return std::any_of(lines.begin(), lines.end(), [&](const std::string& l) { return squash(l).find(want) != std::string::npos; });
}

bool toastWith(Scenario& s, const std::string& title)
{
    return s.waitUntil([&] {
        for (const auto& t : s.app.toasts())
            if (t.title == title)
                return true;
        return false;
    });
}

// Journal operations as `git gg op log` lists them (one line each).
std::vector<std::string> operations(Scenario& s, const fs::path& repo)
{
    std::vector<std::string> out;
    for (const auto& line : gg::splitLines(s.gitgg(repo, {"op", "log"}).out))
        if (!line.empty() && line[0] != ' ')
            out.push_back(line);
    return out;
}

size_t countWith(const std::vector<std::string>& lines, const std::string& text)
{
    return static_cast<size_t>(
        std::count_if(lines.begin(), lines.end(), [&](const std::string& l) { return l.find(text) != std::string::npos; }));
}

std::string shortId(Scenario& s, const std::string& hex) { return s.session()->shortId(ggui::core::Oid::fromHex(hex)); }

fs::path writeTodo(Scenario& s, const std::string& name, const std::string& text)
{
    const fs::path file = s.root() / name;
    std::ofstream(file, std::ios::binary) << text;
    return file;
}

} // namespace

GG_TEST("rebase-native", "edit, break and a failing exec stop git rebase -i; Amend and continue; progress; one Undo",
    "IR-ENGINE-NATIVE", "IR-ACT-EDIT", "IR-ACT-BREAK", "IR-ACT-EXEC", "IR-NATIVE-STOP-EDIT", "IR-NATIVE-STOP-BREAK",
    "IR-NATIVE-STOP-EXEC", "CONF-NATIVE-AMEND-CONTINUE", "CONF-NATIVE-PROGRESS", "IR-NATIVE-UNDO", "CLI-SEQ-EDITOR",
    "IR-OPT-COMMITTER-DATE")
{
    const Repo r = makeRepo(s);
    GG_REQUIRE(s.openRepository(r.path));
    GG_REQUIRE(openFrom(s, r.c[2]));
    GG_CHECK(rows(s) == (Rows{"pick c2", "pick c3", "update-ref refs/heads/part1", "pick c4", "pick c5"}));
    key(s, r.c[2], ImGuiKey_E);
    key(s, r.c[3], ImGuiKey_B); // a break after c3
    key(s, r.c[4], ImGuiKey_R);
    s.setText("//Interactive rebase/**/###ir_msg_" + r.c[4], "c4 reworded natively\n# a comment line\n");
    key(s, r.c[4], ImGuiKey_X); // an exec after c4 that fails the first time
    s.setText("//Interactive rebase/**/###ir_exec_5", "test -f ok.txt");
    GG_CHECK(rows(s) == (Rows{"edit c2", "pick c3", "break", "update-ref refs/heads/part1", "reword c4", "exec test -f ok.txt", "pick c5"}));
    GG_CHECK(editor(s).engine().engine == todo::Engine::Native);
    GG_CHECK(s.textShown("//Interactive rebase", "Engine: git rebase (row 1 is edit, which stops the rebase)"));
    // git rebase sets the committer date to now: Keep original is for the in-memory engine.
    GG_CHECK(ctx->ItemInfo(irWidget("ir_committer_date").c_str()).ItemFlags & ImGuiItemFlags_Disabled);
    GG_CHECK(editor(s).canStart());
    const bool refs = withoutUpdateRefsOnOldGit(s);
    const size_t later = refs ? 1 : 0; // the update-ref row still to come
    GG_REQUIRE(start(s));

    // Stop 1: edit c2. The progress view: nothing done yet, stopped at edit c2, the rest to come.
    GG_REQUIRE(stoppedAt(s, "edit"));
    GG_CHECK(toastWith(s, "Interactive rebase stopped"));
    GG_CHECK(s.itemText("//##Toolbar/###tb_state").rfind("REBASING", 0) == 0);
    {
        const auto snap = s.session()->snapshot();
        GG_CHECK_STR_EQ(snap->rebase->done.back().commit, r.c[2]);
        GG_CHECK_EQ(snap->rebase->remaining.size(), 5u + later);
        GG_CHECK_STR_EQ(snap->rebase->headName, "refs/heads/main");
    }
    auto lines = progress(s);
    GG_CHECK(contains(lines, "Rebasing main: 0 done, " + std::to_string(5 + later) + " remaining"));
    GG_CHECK(contains(lines, "(nothing yet)"));
    GG_CHECK(contains(lines, "edit " + shortId(s, r.c[2]) + " c2 add b"));
    GG_CHECK(contains(lines, "Edit: change the commit (Amend and continue), or Continue as it is."));
    GG_CHECK(contains(lines, "exec test -f ok.txt"));
    GG_CHECK(contains(lines, "update-ref refs/heads/part1") == refs);
    // Amend c2 with a staged change, then go on to the break.
    s.write(r.path, "b.txt", "b amended\n");
    s.git(r.path, {"add", "b.txt"});
    ctx->ItemClick(kAmendContinue);
    GG_REQUIRE(stoppedAt(s, "break"));
    GG_CHECK_STR_EQ(s.gitOut(r.path, {"show", "HEAD~1:b.txt"}), "b amended");
    GG_CHECK_STR_EQ(s.gitOut(r.path, {"log", "-1", "--format=%s", "HEAD~1"}), "c2 add b");
    lines = progress(s);
    GG_CHECK(contains(lines, "Rebasing main: 2 done, " + std::to_string(3 + later) + " remaining"));
    GG_CHECK(contains(lines, "Break: Continue when you are ready."));

    // Stop 3: the exec fails (git exits 1 but went on: not an error). Amend and continue with
    // nothing staged just continues.
    ctx->ItemClick(kAmendContinue);
    GG_REQUIRE(stoppedAt(s, "exec"));
    GG_CHECK(!s.app.dialogs().current());
    lines = progress(s);
    GG_CHECK(contains(lines, "The command failed: fix the problem, then Continue."));
    GG_CHECK(contains(lines, "exec test -f ok.txt"));
    // The typed message went to git's editor for the reword (comments stripped by git).
    GG_CHECK_STR_EQ(s.gitOut(r.path, {"log", "-1", "--format=%B", "HEAD"}), "c4 reworded natively");
    // Continue with the problem unfixed... fixed: ok.txt exists now.
    s.write(r.path, "ok.txt", "ok\n");
    ctx->ItemClick(kContinue);
    GG_REQUIRE(finished(s, r.path));
    GG_CHECK(subjects(s, r.path, "main") == (std::vector<std::string>{"c5", "c4", "c3", "c2", "c1"}));
    GG_CHECK_STR_EQ(s.gitOut(r.path, {"log", "-1", "--format=%B", "main~1"}), "c4 reworded natively");
    GG_CHECK_STR_EQ(s.revParse(r.path, "part1"), refs ? s.revParse(r.path, "main~2") : r.c[3]);
    GG_CHECK_STR_EQ(s.revParse(r.path, "main~4"), r.c[1]);
    GG_CHECK_STR_EQ(s.gitOut(r.path, {"symbolic-ref", "HEAD"}), "refs/heads/main");
    GG_CHECK(!fs::exists(r.path / ".git" / "gg" / "rebase"));

    // One journal operation from start to finish: the steps joined it. One Undo restores it all.
    const auto ops = operations(s, r.path);
    GG_CHECK_EQ(countWith(ops, "interactive rebase (git rebase -i)"), 1u);
    GG_CHECK_EQ(countWith(ops, "--continue"), 0u);
    GG_CHECK_EQ(countWith(ops, "amend and continue"), 0u);
    ctx->ItemClick("//##Toolbar/###tb_undo");
    GG_CHECK(s.waitUntil([&] { return s.revParse(r.path, "main") == r.c[5]; }));
    s.settle();
    GG_CHECK_STR_EQ(s.revParse(r.path, "part1"), r.c[3]);
    GG_CHECK_STR_EQ(s.read(r.path, "b.txt"), "b\n");

    // Without ggui's prepared state the helper only edits git's rebase todo (sequence.editor).
    auto cli = s.gitgg(r.path, {"sequence-editor", ".git/COMMIT_EDITMSG"});
    GG_CHECK_EQ(cli.exitCode, 1);
    GG_CHECK(cli.err.find("is not git's rebase todo") != std::string::npos);
    ggui::setEnv("GG_SEQUENCE_DIR", (s.root() / "missing").string());
    cli = s.gitgg(r.path, {"sequence-editor", ".git/COMMIT_EDITMSG"});
    ggui::unsetEnv("GG_SEQUENCE_DIR");
    GG_CHECK_EQ(cli.exitCode, 1);
    GG_CHECK(cli.err.find("cannot read the prepared todo") != std::string::npos);
    GG_CHECK(s.gitgg(r.path, {"help", "sequence-editor"}).out.find("GIT_SEQUENCE_EDITOR") != std::string::npos);
}

GG_TEST("rebase-native", "conflict stop, Edit remaining todo like git rebase --edit-todo, exec after every commit, Run as git rebase",
    "IR-ENGINE-USER-CHOICE", "IR-OPT-EXEC-EACH", "IR-NATIVE-STOP-CONFLICT", "IR-ENTRY-STOPPED",
    "CONF-NATIVE-IREBASE-EDIT-TODO", "IR-PLAIN-EDIT-TODO", "CONF-NATIVE-CONTINUE")
{
    // a.txt line 2: c2 sets X, c3 sets Y (part1 at c3), c4 adds d.txt.
    const fs::path repo = s.fixture(Recipe::Empty);
    s.commitFile(repo, "a.txt", "1\n2\n3\n", "c1 add a");
    const std::string c1 = s.head(repo);
    s.commitFile(repo, "a.txt", "1\nX\n3\n", "c2 set X");
    const std::string c2 = s.head(repo);
    s.commitFile(repo, "a.txt", "1\nY\n3\n", "c3 set Y");
    const std::string c3 = s.head(repo);
    s.git(repo, {"branch", "part1"});
    s.commitFile(repo, "d.txt", "d\n", "c4 add d");
    const std::string c4 = s.head(repo);
    GG_REQUIRE(s.openRepository(repo));
    GG_REQUIRE(openFrom(s, c2));
    // c3 before c2: c3 conflicts on line 2. Every row in memory, but the user runs it with git.
    key(s, c3, ImGuiMod_Alt | ImGuiKey_UpArrow);
    GG_CHECK(rows(s) == (Rows{"pick c3", "pick c2", "update-ref refs/heads/part1", "pick c4"}));
    GG_CHECK(editor(s).engine().engine == todo::Engine::InMemory);
    ctx->ItemCheck(irWidget("ir_native").c_str());
    GG_CHECK(editor(s).engine().engine == todo::Engine::Native);
    GG_CHECK(s.textShown("//Interactive rebase", "you chose to run it as git rebase"));
    // Exec after every commit: a line in .git/exec.log per commit.
    s.setText(irWidget("ir_exec_each"), "echo x >> .git/exec.log");
    const bool refs = withoutUpdateRefsOnOldGit(s);
    GG_REQUIRE(start(s));
    GG_REQUIRE(stoppedAt(s, "pick"));
    GG_CHECK(s.waitUntil([&] {
        const auto st = s.session()->status();
        return st && !st->conflicted.empty();
    }));
    GG_CHECK_STR_EQ(s.session()->snapshot()->rebase->done.back().commit, c3);
    GG_CHECK(!s.itemExists(kAmendContinue)); // conflicts: resolve them first
    ctx->ItemClick(kContinue);                // git refuses: an error, nothing moved
    GG_CHECK(s.dismissError());
    GG_CHECK_STR_EQ(s.session()->snapshot()->rebase->done.back().commit, c3);
    auto lines = progress(s);
    GG_CHECK(contains(lines, "Conflicts: resolve them (or Commit with conflicts), then Continue."));
    GG_CHECK(contains(lines, "exec echo x >> .git/exec.log"));

    // Edit remaining todo: the rest of git's list, onto HEAD, in the editor.
    ctx->ItemClick(kEditTodo);
    GG_REQUIRE(editorReady(s));
    GG_CHECK(editor(s).editingRemaining());
    Rows remaining{"exec echo x >> .git/exec.log", "pick c2", "exec echo x >> .git/exec.log", "update-ref refs/heads/part1",
        "pick c4", "exec echo x >> .git/exec.log"};
    if (!refs)
        remaining.erase(remaining.begin() + 3);
    GG_CHECK(rows(s) == remaining);
    GG_CHECK_STR_EQ(editor(s).context()->onto, s.head(repo));
    GG_CHECK(s.textShown("//Interactive rebase", "Remaining todo of the rebase of main: 2 commit(s) onto HEAD " + shortId(s, s.head(repo))));
    GG_CHECK(s.textShown("//Interactive rebase", "Engine: git rebase (the rest of the rebase in progress)"));
    GG_CHECK(!s.itemExists(irWidget("ir_onto").c_str())); // no options for the rest
    // Drop c4 and reword c2 with a typed message.
    key(s, c4, ImGuiKey_D);
    key(s, c2, ImGuiKey_R);
    s.setText("//Interactive rebase/**/###ir_msg_" + c2, "c2 reworded in the remaining todo");
    const std::string saved = todo::format(editor(s).todo());
    // What git rebase --edit-todo writes for the same list, on a copy of the stopped repository.
    const fs::path copy = s.root() / "copy";
    fs::copy(repo, copy, fs::copy_options::recursive);
    s.track(copy);
    const fs::path listFile = writeTodo(s, "remaining.txt", saved);
    s.git(copy, {"-c", "sequence.editor=cp '" + listFile.generic_string() + "'", "rebase", "--edit-todo"});
    // git's list changed meanwhile (the copy's continues elsewhere): Save refuses.
    const std::string before = s.read(repo, ".git/rebase-merge/git-rebase-todo");
    s.write(repo, ".git/rebase-merge/git-rebase-todo", before + "exec true\n");
    ctx->ItemClick(irWidget("ir_start").c_str());
    GG_CHECK(s.dismissError());
    GG_CHECK(editor(s).isOpen());
    s.write(repo, ".git/rebase-merge/git-rebase-todo", before);
    GG_REQUIRE(start(s));
    GG_CHECK_STR_EQ(s.read(repo, ".git/rebase-merge/git-rebase-todo"), s.read(copy, ".git/rebase-merge/git-rebase-todo"));
    GG_CHECK(s.read(repo, ".git/rebase-merge/git-rebase-todo").find("drop " + c4) != std::string::npos);
    GG_REQUIRE(s.waitUntil([&] {
        const auto snap = s.session()->snapshot();
        const size_t n = refs ? 6 : 5;
        return snap->rebase && snap->rebase->remaining.size() == n && snap->rebase->remaining[n - 2].action == "drop";
    }));

    // Commit with conflicts: c3 keeps the conflict first-class; c2 conflicts with it (git stops
    // again: information, not an error), and once more.
    ctx->ItemClick(kCommitConflicts);
    GG_CHECK(s.waitUntil([&] {
        const auto snap = s.session()->snapshot();
        return !snap->rebase || snap->rebase->done.back().action != "pick" || snap->rebase->done.back().commit != c3;
    }));
    s.settle();
    for (int guard = 0; guard < 3 && fs::exists(repo / ".git" / "rebase-merge"); ++guard) {
        const bool conflicted = !s.gitOut(repo, {"diff", "--name-only", "--diff-filter=U"}).empty();
        const char* button = conflicted ? kCommitConflicts : kContinue;
        GG_REQUIRE(s.waitUntil([&] { return s.itemExists(button); }));
        ctx->ItemClick(button);
        s.settle();
    }
    GG_REQUIRE(finished(s, repo));
    GG_CHECK(!s.app.dialogs().current());
    GG_CHECK(subjects(s, repo, "main") == (std::vector<std::string>{"c2", "c3", "c1"}));
    GG_CHECK_STR_EQ(s.gitOut(repo, {"log", "-1", "--format=%B", "main"}), "c2 reworded in the remaining todo");
    GG_CHECK_STR_EQ(s.revParse(repo, "part1"), refs ? s.revParse(repo, "main") : c3);
    GG_CHECK_STR_EQ(s.revParse(repo, "main~2"), c1);
    GG_CHECK(s.gitMayFail(repo, {"grep", "-q", "-e", "^<<<<<<< ", "main~1", "--", "a.txt"}).ok());
    // Exec after every commit ran after c3, c2 and where c4 was (its exec row stayed).
    GG_CHECK_STR_EQ(s.read(repo, ".git/exec.log"), "x\nx\nx\n");
    GG_CHECK_EQ(countWith(operations(s, repo), "interactive rebase (git rebase -i)"), 1u);
    s.git(copy, {"rebase", "--abort"});
}

GG_TEST("rebase-native", "plain git rebase -i started as a test step, edited in ggui, finished in a terminal; hooks make it one operation",
    "IR-PLAIN-DETECT", "IR-NATIVE-TERMINAL-FOLLOW", "IR-NATIVE-UNDO", "CONF-NATIVE-PROGRESS", "IR-ENTRY-STOPPED")
{
    const Repo r = makeRepo(s);
    GG_REQUIRE(s.gitgg(r.path, {"hooks", "install"}).ok());
    const fs::path list = writeTodo(s, "todo.txt",
        "pick " + r.c[2] + "\nedit " + r.c[3] + "\npick " + r.c[4] + "\npick " + r.c[5] + "\n");
    ggui::setEnv("GIT_SEQUENCE_EDITOR", "cp '" + list.generic_string() + "'");
    s.git(r.path, {"rebase", "-i", r.c[1]});
    ggui::unsetEnv("GIT_SEQUENCE_EDITOR");
    GG_REQUIRE(fs::exists(r.path / ".git" / "rebase-merge" / "interactive"));

    // ggui detects it from .git/rebase-merge: state, progress, the same stop handling.
    GG_REQUIRE(s.openRepository(r.path));
    GG_REQUIRE(stoppedAt(s, "edit"));
    GG_CHECK(s.session()->snapshot()->state == ggui::core::RepoState::RebasingInteractive);
    GG_CHECK(s.itemExists(kAmendContinue));
    auto lines = progress(s);
    GG_CHECK(contains(lines, "Rebasing main: 1 done, 2 remaining"));
    GG_CHECK(contains(lines, "pick " + shortId(s, r.c[2]) + " c2 add b"));
    GG_CHECK(contains(lines, "edit " + shortId(s, r.c[3]) + " c3 add c"));
    GG_CHECK(contains(lines, "pick " + shortId(s, r.c[5]) + " c5 add e"));

    // Edit remaining todo: drop c4.
    ctx->ItemClick(kEditTodo);
    GG_REQUIRE(editorReady(s));
    GG_CHECK(rows(s) == (Rows{"pick c4", "pick c5"}));
    key(s, r.c[4], ImGuiKey_D);
    GG_REQUIRE(start(s));
    GG_CHECK(s.read(r.path, ".git/rebase-merge/git-rebase-todo").rfind("drop " + r.c[4], 0) == 0);

    // Finished in a terminal: ggui follows.
    s.git(r.path, {"rebase", "--continue"});
    GG_REQUIRE(finished(s, r.path));
    GG_CHECK(s.waitUntil([&] { return s.session()->snapshot()->head.hex() == s.head(r.path); }));
    GG_CHECK(subjects(s, r.path, "main") == (std::vector<std::string>{"c5", "c3", "c2", "c1"}));
    // With the managed hooks the whole plain rebase is one operation: one Undo restores it.
    auto ops = operations(s, r.path);
    GG_CHECK_EQ(countWith(ops, "[git] "), 1u);
    GG_CHECK_EQ(countWith(ops, "rebase -i"), 1u);
    ctx->ItemClick("//##Toolbar/###tb_undo");
    GG_CHECK(s.waitUntil([&] { return s.revParse(r.path, "main") == r.c[5]; }));
    s.settle();
    GG_CHECK_STR_EQ(s.revParse(r.path, "part1"), r.c[3]);

    // ggui starts it, the terminal finishes it: still one operation with the hooks.
    GG_REQUIRE(openFrom(s, r.c[4]));
    key(s, r.c[4], ImGuiKey_D);
    key(s, r.c[4], ImGuiKey_B);
    GG_REQUIRE(start(s));
    GG_REQUIRE(stoppedAt(s, "break"));
    s.git(r.path, {"rebase", "--continue"});
    GG_REQUIRE(finished(s, r.path));
    ops = operations(s, r.path);
    GG_CHECK_EQ(countWith(ops, "interactive rebase (git rebase -i)"), 1u);
    GG_CHECK(!fs::exists(r.path / ".git" / "gg" / "rebase"));
    GG_CHECK(subjects(s, r.path, "main") == (std::vector<std::string>{"c5", "c3", "c2", "c1"}));
    ctx->ItemClick("//##Toolbar/###tb_undo");
    GG_CHECK(s.waitUntil([&] { return s.revParse(r.path, "main") == r.c[5]; }));
    s.settle();

    // Without the hooks ggui cannot see the terminal's part: Undo refuses (refs moved outside the
    // journal); the rebase's operation is closed all the same.
    GG_REQUIRE(s.gitgg(r.path, {"hooks", "uninstall"}).ok());
    GG_REQUIRE(openFrom(s, r.c[4]));
    key(s, r.c[4], ImGuiKey_D);
    key(s, r.c[4], ImGuiKey_B);
    GG_REQUIRE(start(s));
    GG_REQUIRE(stoppedAt(s, "break"));
    GG_CHECK(fs::exists(r.path / ".git" / "gg" / "rebase"));
    s.git(r.path, {"rebase", "--continue"});
    GG_REQUIRE(finished(s, r.path));
    ctx->ItemClick("//##Toolbar/###tb_undo");
    GG_CHECK(s.dismissError());
    GG_CHECK(!fs::exists(r.path / ".git" / "gg" / "rebase"));
    GG_CHECK(s.revParse(r.path, "main") != r.c[5]);
}

GG_TEST("rebase-native", "git rebase -i refusals and options: moved branch, git before 2.38 with update-ref, local changes and autostash, Abort",
    "IR-OPT-AUTOSTASH", "IR-OPT-EMPTY", "CONF-NATIVE-ABORT")
{
    const Repo r = makeRepo(s);
    GG_REQUIRE(s.openRepository(r.path));

    // The branch moved since the list was read: refused, the editor stays.
    GG_REQUIRE(openFrom(s, r.c[4]));
    key(s, r.c[4], ImGuiKey_B);
    s.comboSelect(irWidget("ir_empty").c_str(), "Drop");
    s.commitFile(r.path, "f.txt", "f\n", "c6 add f");
    const std::string c6 = s.head(r.path);
    ctx->ItemClick(irWidget("ir_start").c_str());
    GG_CHECK(s.dismissError());
    GG_CHECK(editor(s).isOpen());
    ctx->ItemClick(irWidget("ir_cancel").c_str());

    // git older than 2.38 cannot run update-ref rows; without them it runs (--empty=ask).
    const fs::path realGit = gg::findInPath("git");
    const std::string path = ggui::getEnv("PATH");
    s.fakeTool("git", "if [ \"$1\" = version ]; then echo 'git version 2.37.1'; exit 0; fi\nexec '" + realGit.string() + "' \"$@\"\n");
    GG_REQUIRE(openFrom(s, r.c[3]));
    GG_CHECK(rows(s) == (Rows{"pick c3", "update-ref refs/heads/part1", "pick c4", "pick c5", "pick c6"}));
    key(s, r.c[5], ImGuiKey_B);
    ctx->ItemClick(irWidget("ir_start").c_str());
    GG_CHECK(s.dismissError());
    GG_CHECK(s.app.errorMessage().find("update-ref rows need git 2.38") != std::string::npos);
    ctx->ItemUncheck(irWidget("ir_update_refs").c_str());
    GG_REQUIRE(start(s));
    GG_REQUIRE(stoppedAt(s, "break"));
    ggui::setEnv("PATH", path);
    {
        std::ifstream log(s.root() / "git.log");
        std::stringstream text;
        text << log.rdbuf();
        GG_CHECK(text.str().find("--empty=ask\n") != std::string::npos);
    }
    // Abort: everything as before, the rebase's operation closed.
    ctx->ItemClick(kAbort);
    GG_REQUIRE(finished(s, r.path));
    GG_CHECK_STR_EQ(s.head(r.path), c6);
    GG_CHECK_STR_EQ(s.gitOut(r.path, {"symbolic-ref", "HEAD"}), "refs/heads/main");
    GG_CHECK(!fs::exists(r.path / ".git" / "gg" / "rebase"));

    // Local changes: git refuses without Autostash; with it they come back after the rebase. From
    // the root, commits that become empty kept.
    s.write(r.path, "a.txt", "a local change\n");
    GG_REQUIRE(openFrom(s, r.c[1]));
    GG_CHECK(editor(s).context()->upstream.empty());
    key(s, r.c[2], ImGuiKey_B);
    s.comboSelect(irWidget("ir_empty").c_str(), "Keep");
    ctx->ItemClick(irWidget("ir_start").c_str());
    GG_CHECK(s.dismissError());
    GG_CHECK(editor(s).isOpen());
    GG_CHECK_STR_EQ(s.head(r.path), c6);
    ctx->ItemCheck(irWidget("ir_autostash").c_str());
    withoutUpdateRefsOnOldGit(s);
    GG_REQUIRE(start(s));
    GG_REQUIRE(stoppedAt(s, "break"));
    ctx->ItemClick(kContinue);
    GG_REQUIRE(finished(s, r.path));
    GG_CHECK(subjects(s, r.path, "main") == (std::vector<std::string>{"c6", "c5", "c4", "c3", "c2", "c1"}));
    GG_CHECK_STR_EQ(s.read(r.path, "a.txt"), "a local change\n");
    GG_CHECK(s.gitOut(r.path, {"stash", "list"}).empty());
    s.git(r.path, {"checkout", "--", "a.txt"});

    // A commit that becomes empty onto "up" (which adds b.txt too): git stops on it (Ask =
    // --empty=stop); Skip drops it. A break as the last row: nothing remains after it.
    s.git(r.path, {"switch", "-q", "-c", "up", r.c[1]});
    s.write(r.path, "b.txt", "b\n");
    s.write(r.path, "x.txt", "x\n");
    s.git(r.path, {"add", "b.txt", "x.txt"});
    s.git(r.path, {"commit", "-q", "-m", "u1 add b and x"});
    const std::string u1 = s.head(r.path);
    s.git(r.path, {"switch", "-q", "main"});
    const std::string c2 = s.revParse(r.path, "main~4");
    GG_REQUIRE(openFrom(s, c2));
    ctx->ItemClick(irWidget("ir_onto").c_str());
    ctx->KeyChars("up");
    ctx->KeyPress(ImGuiKey_Enter);
    GG_REQUIRE(s.waitUntil([&] { return editor(s).context() && editor(s).context()->onto == u1; }));
    key(s, s.revParse(r.path, "main"), ImGuiKey_B);
    withoutUpdateRefsOnOldGit(s);
    GG_REQUIRE(start(s));
    GG_REQUIRE(stoppedAt(s, "pick"));
    GG_CHECK_STR_EQ(s.session()->snapshot()->rebase->done.back().commit, c2);
    auto lines = progress(s);
    GG_CHECK(contains(lines, "Continue to go on (or Skip this commit)."));
    ctx->ItemClick("//##Toolbar/Skip##tb_skip");
    GG_REQUIRE(stoppedAt(s, "break"));
    lines = progress(s);
    GG_CHECK(contains(lines, "(nothing: Continue finishes the rebase)"));
    ctx->ItemClick(kContinue);
    GG_REQUIRE(finished(s, r.path));
    GG_CHECK(subjects(s, r.path, "main") == (std::vector<std::string>{"c6", "c5", "c4", "c3", "u1", "c1"}));

    // A git rebase that runs through without stopping: exec after every commit.
    const std::string tip = s.revParse(r.path, "main");
    GG_REQUIRE(openFrom(s, tip));
    s.setText(irWidget("ir_exec_each"), "true");
    GG_REQUIRE(start(s));
    GG_REQUIRE(finished(s, r.path));
    GG_CHECK_STR_EQ(s.revParse(r.path, "main"), tip); // a lone pick on its parent stays as it is
    GG_CHECK(!fs::exists(r.path / ".git" / "gg" / "rebase"));
}

GG_TEST("rebase-native", "conflicted input: git rebase -i stops at edit on a commit with first-class conflicts; they are carried along",
    "IR-CONFLICTED-INPUT", "IR-NATIVE-STOP-EDIT", "IR-ACT-EDIT", "CONF-NATIVE-AMEND-CONTINUE", "IR-NATIVE-UNDO")
{
    // Base, "Conflicted commit" (conflict.txt with a region), "Descendant keeps the conflict".
    const fs::path path = s.fixture(Recipe::Conflicted2);
    const std::string tip = s.head(path);
    const std::string conflicted = s.revParse(path, "HEAD~1");
    const std::string descendant = tip;
    const std::string blob = s.revParse(path, "HEAD:conflict.txt");
    GG_REQUIRE(s.openRepository(path));
    GG_REQUIRE(openFrom(s, conflicted));
    // The descendant first, then edit the conflicted commit.
    click(s, descendant);
    s.ctx->KeyPress(ImGuiMod_Alt | ImGuiKey_UpArrow);
    key(s, conflicted, ImGuiKey_E);
    GG_CHECK(rows(s) == (Rows{"pick Descendant", "edit Conflicted"}));
    GG_CHECK(editor(s).engine().engine == todo::Engine::Native);
    GG_CHECK(s.waitUntil([&] {
        const auto p = editor(s).preview();
        return p && p->ok && p->rows.size() == 2 && editor(s).previewTodo() == editor(s).todo();
    }));
    {
        const auto p = editor(s).preview();
        GG_CHECK(p->rows[0].conflicts.empty());
        GG_REQUIRE(p->rows[1].conflicts.size() == 1);
        GG_CHECK(p->rows[1].conflicts[0].first == "conflict.txt" && !p->rows[1].newConflicts);
    }
    GG_REQUIRE(start(s));

    // Stopped at the edit: the regions are committed text, so git has no conflict and neither does
    // ggui (Edit, not Conflicts; Amend and continue, no Commit with conflicts).
    GG_REQUIRE(stoppedAt(s, "edit"));
    GG_CHECK(s.statusPorcelain(path).empty());
    GG_CHECK(s.read(path, "conflict.txt").find("<<<<<<< side 1\n") != std::string::npos);
    const auto lines = progress(s);
    GG_CHECK(contains(lines, "Edit: change the commit (Amend and continue), or Continue as it is."));
    GG_CHECK(!contains(lines, "Conflicts:"));
    GG_CHECK(s.itemExists(kAmendContinue));
    GG_CHECK(!s.itemExists(kCommitConflicts));
    s.write(path, "e.txt", "e\n");
    s.git(path, {"add", "e.txt"});
    ctx->ItemClick(kAmendContinue);
    GG_REQUIRE(finished(s, path));
    GG_CHECK(subjects(s, path, "main") == (Rows{"Conflicted", "Descendant", "Base"}));
    // The region is carried byte for byte; the amended file is there.
    GG_CHECK_STR_EQ(s.revParse(path, "main:conflict.txt"), blob);
    GG_CHECK_STR_EQ(s.gitOut(path, {"show", "main:e.txt"}), "e");
    GG_CHECK(s.gitMayFail(path, {"grep", "-l", "-e", "^<<<<<<< ", "main~1", "--"}).out.empty()); // Descendant, first now
    GG_CHECK(s.statusPorcelain(path).empty());

    // One operation: one Undo restores the original commits.
    GG_CHECK_EQ(countWith(operations(s, path), "interactive rebase (git rebase -i)"), 1u);
    ctx->ItemClick("//##Toolbar/###tb_undo");
    GG_CHECK(s.waitUntil([&] { return s.revParse(path, "main") == tip; }));
    s.settle();
    GG_CHECK(!fs::exists(path / "e.txt"));
}

GG_TEST("rebase-native", "failure paths: pre-rebase veto leaves no rebase; a corrupt journal during a stop keeps one Undo",
    "IR-ENGINE-NATIVE", "IR-NATIVE-UNDO", "HOOK-JOURNAL-CORRUPT", "REWRITE-FAIL-UNTOUCHED")
{
    const Repo r = makeRepo(s);
    const auto refsBefore = s.refs(r.path);
    GG_REQUIRE(s.openRepository(r.path));
    GG_REQUIRE(openFrom(s, r.c[2]));
    key(s, r.c[3], ImGuiKey_E);
    key(s, r.c[4], ImGuiKey_D);
    GG_CHECK(editor(s).engine().engine == todo::Engine::Native);
    withoutUpdateRefsOnOldGit(s);

    // git runs pre-rebase itself: refused, nothing starts, nothing moves.
    s.write(r.path, ".git/hooks/pre-rebase", "#!/bin/sh\necho \"no rebasing today\" >&2\nexit 1\n");
    fs::permissions(r.path / ".git" / "hooks" / "pre-rebase", fs::perms::owner_all, fs::perm_options::add);
    ctx->ItemClick(irWidget("ir_start").c_str());
    GG_REQUIRE(s.waitUntil([&] { return !s.app.errorMessage().empty(); }, 30.0f));
    GG_CHECK(s.app.errorMessage().find("pre-rebase") != std::string::npos);
    GG_CHECK(s.dismissError());
    GG_CHECK(!fs::exists(r.path / ".git" / "rebase-merge"));
    GG_CHECK(s.refs(r.path) == refsBefore);
    GG_CHECK(s.statusPorcelain(r.path).empty());
    GG_CHECK(editor(s).isOpen());
    fs::remove(r.path / ".git" / "hooks" / "pre-rebase");

    // Broken journal lines before the start and while stopped are skipped.
    auto corrupt = [&] { std::ofstream(r.path / ".git" / "gg" / "journal", std::ios::app | std::ios::binary) << "not json at all\n"; };
    corrupt();
    GG_REQUIRE(start(s));
    GG_REQUIRE(stoppedAt(s, "edit"));
    corrupt();
    ctx->ItemClick(kContinue);
    GG_REQUIRE(finished(s, r.path));
    GG_CHECK(subjects(s, r.path, "main") == (std::vector<std::string>{"c5", "c3", "c2", "c1"}));
    // The refused start is listed as failed; the rebase is one operation.
    const auto ops = operations(s, r.path);
    GG_CHECK_EQ(countWith(ops, "interactive rebase (git rebase -i)"), 2u);
    GG_CHECK_EQ(countWith(ops, "interactive rebase (git rebase -i) (failed)"), 1u);
    ctx->ItemClick("//##Toolbar/###tb_undo");
    GG_CHECK(s.waitUntil([&] { return s.refs(r.path) == refsBefore; }));
    s.settle();
    GG_CHECK(s.statusPorcelain(r.path).empty());
}

} // namespace ggtest

namespace ggtest {

GG_TEST("rebase-native", "Edit remaining todo reads a hand-edited list (short commands, CRLF, abbreviated ids, fixup -C/-c); refuses unreadable ones",
    "IR-ENTRY-STOPPED", "IR-PLAIN-EDIT-TODO", "IR-ACT-FIXUP-C", "IR-ACT-UPDATE-REF")
{
    const Repo r = makeRepo(s);
    // c6 on a side branch: the remaining list may name any commit.
    s.git(r.path, {"switch", "-q", "-c", "side", r.c[1]});
    s.commitFile(r.path, "f.txt", "f\n", "c6 add f");
    const std::string c6 = s.head(r.path);
    s.git(r.path, {"switch", "-q", "main"});
    const fs::path list = writeTodo(s, "todo.txt", "pick " + r.c[2] + "\nedit " + r.c[3] + "\npick " + r.c[4] + "\npick " + r.c[5] + "\n");
    ggui::setEnv("GIT_SEQUENCE_EDITOR", "cp '" + list.generic_string() + "'");
    s.git(r.path, {"rebase", "-i", r.c[1]});
    ggui::unsetEnv("GIT_SEQUENCE_EDITOR");
    GG_REQUIRE(s.openRepository(r.path));
    GG_REQUIRE(stoppedAt(s, "edit"));
    const std::string todoRel = ".git/rebase-merge/git-rebase-todo";
    const std::string tree = s.revParse(r.path, r.c[4] + "^{tree}");

    // Lists ggui cannot read are refused with git's wording; the editor stays closed.
    const std::pair<std::string, std::string> bad[] = {
        {"bogus " + r.c[4] + "\n", "invalid line 1"},
        {"pick " + r.c[4] + "\npick\n", "missing arguments for pick"},
        {"exec\n", "missing arguments for exec"},
        {"pick 0123abcd\n", "could not parse '0123abcd'"},
        {"pick no-such-branch\n", "could not parse 'no-such-branch'"},
        {"pick abc\n", "could not parse 'abc'"},
        {"pick " + tree + "\n", "could not parse '" + tree + "'"},
    };
    for (const auto& [text, message] : bad) {
        s.write(r.path, todoRel, text);
        ctx->ItemClick(kEditTodo);
        GG_REQUIRE(s.dialogOpen("Open interactive rebase"));
        const ggui::Form* f = s.app.dialogs().current();
        GG_CHECK(f && f->message.find(message) != std::string::npos);
        GG_CHECK(s.dismissError());
        GG_CHECK(!editor(s).isOpen());
    }

    // Readable but invalid: update-ref lines git would refuse, and a commit listed twice.
    s.write(r.path, todoRel,
        "pick " + r.c[4] + "\nupdate-ref part1\nupdate-ref refs/heads/x\nupdate-ref refs/heads/x\nupdate-ref refs/heads/main\npick "
            + r.c[4] + "\n");
    ctx->ItemClick(kEditTodo);
    GG_REQUIRE(editorReady(s));
    size_t badRefs = 0, duplicates = 0;
    for (const auto& i : editor(s).issues()) {
        badRefs += i.code == todo::Issue::Code::BadRef;
        duplicates += i.code == todo::Issue::Code::DuplicateCommit;
    }
    GG_CHECK_EQ(badRefs, 3u);
    GG_CHECK_EQ(duplicates, 1u);
    GG_CHECK(s.textShown("//Interactive rebase", "requires a fully qualified refname"));
    GG_CHECK(s.textShown("//Interactive rebase", "is already updated by an earlier row"));
    GG_CHECK(s.textShown("//Interactive rebase", "is the branch being rebased"));
    GG_CHECK(!editor(s).canStart());
    ctx->ItemClick(irWidget("ir_cancel").c_str());
    GG_CHECK(s.waitUntil([&] { return !editor(s).isOpen(); }));

    // Nothing left: an empty list is saved as git's "noop".
    s.write(r.path, todoRel, "");
    ctx->ItemClick(kEditTodo);
    GG_REQUIRE(editorReady(s));
    GG_CHECK(rows(s).empty());
    GG_REQUIRE(start(s));
    GG_CHECK_STR_EQ(s.read(r.path, todoRel), "noop\n");

    // --rebase-merges commands are read, and previewed: "side" (a branch, no label row
    // defines it) is merged into HEAD with c6's message.
    s.write(r.path, todoRel, "label here\nreset here\nmerge -C " + c6 + " side # c6 add f\n");
    ctx->ItemClick(kEditTodo);
    GG_REQUIRE(editorReady(s));
    GG_CHECK(rows(s) == (Rows{"label here", "reset here", "merge side"}));
    GG_CHECK(editor(s).todo().items.size() == 3u && editor(s).todo().items[2].fixup == todo::FixupMessage::Use
        && editor(s).todo().items[2].commit == c6 && editor(s).todo().items[2].subject == "c6 add f");
    GG_CHECK(editor(s).engine().engine == todo::Engine::Native);
    GG_CHECK(s.waitUntil([&] { return !editor(s).previewPending() && editor(s).preview() != nullptr; }, 30.0f));
    GG_CHECK(editor(s).preview() && editor(s).preview()->ok && editor(s).preview()->rows.size() == 1u
        && editor(s).preview()->rows[0].merge && editor(s).preview()->rows[0].subject == "c6 add f"
        && editor(s).preview()->rows[0].parents == (Rows{s.revParse(r.path, "HEAD"), c6}));
    ctx->ItemClick(irWidget("ir_cancel").c_str());
    GG_CHECK(s.waitUntil([&] { return !editor(s).isOpen(); }));

    // A list typed by hand: comments, blank and noop lines, CRLF, tabs, one-letter commands,
    // an upper-case abbreviated id, a branch name, a subject without "#" (older git).
    // (git before 2.38 has no update-ref: the list ends at the break there.)
    const bool updateRef = s.gitAtLeast(2, 38);
    std::string c4Upper = r.c[4].substr(0, 9);
    std::transform(c4Upper.begin(), c4Upper.end(), c4Upper.begin(), [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    s.write(r.path, todoRel,
        "# typed in an editor\r\n\r\nnoop\r\n\tf\t-C " + c4Upper + " # c4 add d\r\nx echo hi >> .git/typed.log\r\n"
        "p main c5 add e\r\nfixup -c " + c6.substr(0, 12) + " c6 add f\r\nb\r\n"
            + (updateRef ? "u refs/heads/part1\r\n" : ""));
    ctx->ItemClick(kEditTodo);
    GG_REQUIRE(editorReady(s));
    Rows typed{"fixup c4", "exec echo hi >> .git/typed.log", "pick c5", "fixup c6", "break", "update-ref refs/heads/part1"};
    if (!updateRef)
        typed.pop_back();
    GG_CHECK(rows(s) == typed);
    const auto& items = editor(s).todo().items;
    GG_REQUIRE(items.size() == typed.size());
    GG_CHECK(items[0].fixup == todo::FixupMessage::Use);
    GG_CHECK_STR_EQ(items[0].commit, r.c[4]);
    GG_CHECK_STR_EQ(items[2].commit, r.c[5]);
    GG_CHECK_STR_EQ(items[2].subject, "c5 add e");
    GG_CHECK(items[3].fixup == todo::FixupMessage::Edit);
    GG_CHECK_STR_EQ(items[3].commit, c6);
    // The preview folds the leading fixup into HEAD (the edited c3).
    GG_CHECK(s.waitUntil([&] { return !editor(s).previewPending() && editor(s).preview() != nullptr; }, 30.0f));
    GG_REQUIRE(editor(s).preview() != nullptr);
    GG_CHECK_EQ(editor(s).preview()->rows.size(), 2u);
    // Save writes full ids and long command names back, as git rebase --edit-todo would.
    GG_REQUIRE(start(s));
    const std::string saved = s.read(r.path, todoRel);
    GG_CHECK(saved.rfind("fixup -C " + r.c[4] + " # c4 add d\nexec echo hi >> .git/typed.log\npick " + r.c[5] + " # c5 add e\nfixup -c "
                 + c6 + " # c6 add f\nbreak\n" + (updateRef ? "update-ref refs/heads/part1\n" : ""),
                 0)
        == 0);
    ctx->ItemClick(kAbort);
    GG_REQUIRE(finished(s, r.path));
    GG_CHECK_STR_EQ(s.revParse(r.path, "main"), r.c[5]);
}

GG_TEST("rebase-native", "typed squash messages reach git's editor, also for a squash that amends after an update-ref row; exec after every commit around followers and drops",
    "IR-MSG-SQUASH", "IR-OPT-EXEC-EACH", "IR-ENGINE-USER-CHOICE", "IR-ACT-SQUASH", "IR-ACT-DROP")
{
    GG_REQUIRE_GIT(2, 38, "a squash after an update-ref row");
    const Repo r = makeRepo(s);
    GG_REQUIRE(s.openRepository(r.path));
    GG_REQUIRE(openFrom(s, r.c[2]));
    // [c2, c3, update-ref part1, c4, c5]: c3 squashed into c2; c4 squashed after the update-ref row
    // (it amends the finished c2+c3, which part1 keeps); c5 dropped.
    key(s, r.c[3], ImGuiKey_S);
    key(s, r.c[4], ImGuiKey_S);
    key(s, r.c[5], ImGuiKey_D);
    GG_CHECK(rows(s) == (Rows{"pick c2", "squash c3", "update-ref refs/heads/part1", "squash c4", "drop c5"}));
    s.setText("//Interactive rebase/**/###ir_msg_" + r.c[2], "c2 and c3\n");
    GG_REQUIRE(s.waitUntil([&] { return s.itemExists(("//Interactive rebase/**/###ir_msg_" + r.c[4]).c_str()); }));
    s.setText("//Interactive rebase/**/###ir_msg_" + r.c[4], "c2, c3 and c4\n");
    ctx->ItemCheck(irWidget("ir_native").c_str());
    s.setText(irWidget("ir_exec_each"), "echo x >> .git/exec.log");
    GG_REQUIRE(start(s));
    GG_REQUIRE(finished(s, r.path));
    GG_CHECK_STR_EQ(s.gitOut(r.path, {"log", "-1", "--format=%B", "main"}), "c2, c3 and c4");
    GG_CHECK_STR_EQ(s.gitOut(r.path, {"log", "-1", "--format=%B", "part1"}), "c2 and c3");
    GG_CHECK_STR_EQ(s.revParse(r.path, "main~1"), r.c[1]);
    GG_CHECK_STR_EQ(s.revParse(r.path, "part1~1"), r.c[1]);
    GG_CHECK(!s.gitMayFail(r.path, {"cat-file", "-e", "main:e.txt"}).ok());
    // One exec after the c2+c3 group and one after c4; none after the dropped c5.
    GG_CHECK_STR_EQ(s.read(r.path, ".git/exec.log"), "x\nx\n");
}

} // namespace ggtest
