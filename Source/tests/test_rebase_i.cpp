// Interactive rebase: the todo editor, its entry points and options, run on the in-memory engine,
// and the live preview beside the list (§4.13; P3-15, P3-16, P3-17).
#include "panels/HistoryPanel.hpp"
#include "panels/RebasePanel.hpp"
#include "shell/App.hpp"
#include "shell/Dialogs.hpp"
#include "shell/Session.hpp"
#include "tests/Harness.hpp"
#include "util/FrameProbe.hpp"

#include <libgg/GitRunner.hpp>
#include <libgg/Todo.hpp>

#include <algorithm>

namespace ggtest {

namespace {

namespace todo = gg::todo;

// c1 … c5 on main, each adding its own file; "part1" at c3 (a stacked branch).
struct Repo {
    fs::path path;
    std::vector<std::string> c; // c[1] = c1 … c[5] = c5 (c[0] unused)
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
std::string irAction(const std::string& hex) { return "//Interactive rebase/**/###ir_action_" + hex; }
std::string irWidget(const char* id) { return std::string("//Interactive rebase/###") + id; }

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

// The list as "<action> <first word of the subject>" / "<action> <argument>".
std::vector<std::string> rows(Scenario& s)
{
    std::vector<std::string> out;
    const auto& e = editor(s);
    for (const auto& item : e.todo().items) {
        std::string text = todo::actionName(item.action);
        if (item.fixup != todo::FixupMessage::None)
            text += item.fixup == todo::FixupMessage::Use ? " -C" : " -c";
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

using Rows = std::vector<std::string>;

std::vector<std::string> subjects(Scenario& s, const fs::path& repo, const std::string& rev)
{
    std::vector<std::string> out;
    for (const auto& l : gg::splitLines(s.gitOut(repo, {"log", "--format=%s", rev})))
        if (!l.empty())
            out.push_back(l.substr(0, l.find(' ')));
    return out;
}

bool hasIssue(Scenario& s, todo::Issue::Code code)
{
    for (const auto& i : editor(s).issues())
        if (i.code == code)
            return true;
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

// Clicks a row (selects it and focuses the editor), with modifiers held.
void click(Scenario& s, const std::string& hex, ImGuiKeyChord mods = 0)
{
    if (mods)
        s.ctx->KeyDown(mods);
    s.ctx->ItemClick(irRow(hex).c_str());
    if (mods)
        s.ctx->KeyUp(mods);
    s.ctx->Yield(2);
}

// A key pressed on a row.
void key(Scenario& s, const std::string& hex, ImGuiKeyChord chord)
{
    click(s, hex);
    s.ctx->KeyPress(chord);
    s.ctx->Yield(2);
}

// ---- live preview helpers --------------------------------------------------------------------

using Preview = ggui::core::RebasePreview;

// The preview once it answers the list as it is now.
const Preview* previewReady(Scenario& s, float seconds = 30.0f)
{
    auto& e = editor(s);
    const bool ok = s.waitUntil(
        [&] { return e.isOpen() && !e.previewPending() && e.preview() && e.previewTodo() == e.todo(); }, seconds);
    s.ctx->Yield(2);
    return ok ? e.preview().get() : nullptr;
}

std::string previewPane(Scenario& s) { return s.child("//Interactive rebase", "##ir_preview"); }
bool previewShows(Scenario& s, const std::string& text) { return s.textShown(previewPane(s).c_str(), text); }

// The preview's rows as "<subject first word>" (oldest first).
std::vector<std::string> previewSubjects(const Preview& p)
{
    std::vector<std::string> out;
    for (const auto& row : p.rows)
        out.push_back(row.subject.substr(0, row.subject.find(' ')));
    return out;
}

std::vector<std::string> conflictPaths(const Preview::Row& row)
{
    std::vector<std::string> out;
    for (const auto& [path, sides] : row.conflicts)
        out.push_back(path);
    return out;
}

// Files of a commit with first-class conflict regions, as the repository has them.
std::vector<std::string> conflictedFiles(Scenario& s, const fs::path& repo, const std::string& rev)
{
    std::vector<std::string> out;
    const auto r = s.gitMayFail(repo, {"grep", "-l", "-e", "^<<<<<<< ", rev, "--"});
    for (const auto& line : gg::splitLines(r.out))
        if (!line.empty())
            out.push_back(line.substr(line.find(':') + 1));
    return out;
}

// The preview is what `tip` now is: per commit the tree, the subject, the first-class conflicts,
// emptiness and the branches at it.
void checkMatches(Scenario& s, const fs::path& repo, const std::string& tip, const Preview& p)
{
    const size_t n = p.rows.size();
    for (size_t k = 0; k < n; ++k) {
        const auto& row = p.rows[k];
        const std::string rev = tip + "~" + std::to_string(n - 1 - k);
        const std::string id = s.revParse(repo, rev);
        GG_CHECK_STR_EQ(s.revParse(repo, rev + "^{tree}"), row.tree);
        GG_CHECK_STR_EQ(s.gitOut(repo, {"log", "-1", "--format=%s", rev}), row.subject);
        GG_CHECK(conflictedFiles(s, repo, rev) == conflictPaths(row));
        const std::string parentTree = s.gitMayFail(repo, {"rev-parse", "-q", "--verify", rev + "~1^{tree}"}).out;
        GG_CHECK_EQ(gg::trim(parentTree) == row.tree, row.empty);
        if (row.unchanged)
            GG_CHECK_STR_EQ(id, row.id);
        for (const auto& b : row.branches)
            GG_CHECK_STR_EQ(s.revParse(repo, b), id);
    }
    GG_CHECK_STR_EQ(s.revParse(repo, tip + "~" + std::to_string(n)), p.onto);
}

// a.txt changed on line 2 by c2 and again by c3 (part1), then c4 adds d.txt.
struct LineRepo {
    fs::path path;
    std::string c1, c2, c3, c4;
};

LineRepo makeLineRepo(Scenario& s)
{
    LineRepo r;
    r.path = s.fixture(Recipe::Empty);
    s.commitFile(r.path, "a.txt", "1\n2\n3\n", "c1 add a");
    r.c1 = s.head(r.path);
    s.commitFile(r.path, "a.txt", "1\nX\n3\n", "c2 set X");
    r.c2 = s.head(r.path);
    s.commitFile(r.path, "a.txt", "1\nY\n3\n", "c3 set Y");
    r.c3 = s.head(r.path);
    s.git(r.path, {"branch", "part1"});
    s.commitFile(r.path, "d.txt", "d\n", "c4 add d");
    r.c4 = s.head(r.path);
    return r;
}

} // namespace

GG_TEST("rebase-i", "entry points: I key, History menu, selection, Commit menu (asks for a base), Branches",
    "IR-ENTRY-HISTORY-KEY", "IR-ENTRY-HISTORY-MENU", "HIST-KEY-I", "ACT-IREBASE", "IR-ENTRY-SELECTION",
    "IR-ENTRY-COMMIT-MENU", "IR-ENTRY-BRANCH", "BR-IREBASE-ONTO", "IR-ROWS")
{
    const Repo r = makeRepo(s);
    const auto refsBefore = s.refs(r.path);
    GG_REQUIRE(s.openRepository(r.path));
    GG_REQUIRE(rowReady(s, r.c[3]));

    // I on c3: c3 and its descendants up to HEAD (main), update-ref for part1.
    ctx->ItemClick(historyRow(r.c[3]).c_str());
    ctx->KeyPress(ImGuiKey_I);
    GG_REQUIRE(editorReady(s));
    GG_CHECK(rows(s) == (Rows{"pick c3", "update-ref refs/heads/part1", "pick c4", "pick c5"}));
    GG_CHECK_STR_EQ(editor(s).context()->tipRef, "refs/heads/main");
    GG_CHECK_STR_EQ(editor(s).context()->upstream, r.c[2]);
    // Rows: action, short ID, subject, author, date, branch badges.
    GG_CHECK(s.itemExists(irRow(r.c[3]).c_str()));
    GG_CHECK(s.itemExists(irAction(r.c[4]).c_str()));
    GG_CHECK(s.itemExists("//Interactive rebase/**/###ir_badge_part1"));
    GG_CHECK(s.textShown("//Interactive rebase", "c4 add d"));
    GG_CHECK(s.textShown("//Interactive rebase", s.gitOut(r.path, {"log", "-1", "--format=%an", r.c[4]})));
    GG_CHECK(s.textShown("//Interactive rebase", s.gitOut(r.path, {"log", "-1", "--format=%ad", "--date=format:%Y-%m-%d", r.c[4]})));
    GG_CHECK(s.textShown("//Interactive rebase", "Rebase 3 commit(s) of main onto " + s.session()->shortId(ggui::core::Oid::fromHex(r.c[2]))));
    // A tab next to History: History in front hides it, the editor stays open.
    ctx->WindowFocus("//History");
    ctx->Yield(2);
    GG_CHECK(!s.itemExists(irRow(r.c[3]).c_str()));
    GG_CHECK(editor(s).isOpen());
    ctx->WindowFocus("//Interactive rebase");
    ctx->Yield(2);
    ctx->ItemClick(irWidget("ir_cancel").c_str());
    GG_CHECK(!editor(s).isOpen());

    // History menu on c2.
    s.contextMenu(historyRow(r.c[2]).c_str(), "Interactive rebase from here...");
    GG_REQUIRE(editorReady(s));
    GG_CHECK(rows(s) == (Rows{"pick c2", "pick c3", "update-ref refs/heads/part1", "pick c4", "pick c5"}));
    GG_CHECK(editor(s).selection() == (std::set<size_t>{0}));
    ctx->ItemClick(irWidget("ir_cancel").c_str());

    // A selection (c2 and c4): from the oldest, both rows selected.
    ctx->ItemClick(historyRow(r.c[2]).c_str());
    ctx->KeyDown(ImGuiMod_Ctrl);
    ctx->ItemClick(historyRow(r.c[4]).c_str());
    ctx->KeyUp(ImGuiMod_Ctrl);
    s.contextMenu(historyRow(r.c[2]).c_str(), "Interactive rebase selection...");
    GG_REQUIRE(editorReady(s));
    GG_CHECK_STR_EQ(editor(s).context()->upstream, r.c[1]);
    GG_CHECK(editor(s).selection() == (std::set<size_t>{0, 3}));
    ctx->ItemClick(irWidget("ir_cancel").c_str());

    // Commit menu: asks for the base; an unknown one is an error.
    ctx->MenuClick("//##MainMenuBar/Commit/Interactive rebase...");
    GG_REQUIRE(s.dialogOpen("Interactive rebase onto"));
    s.dialogText("Interactive rebase onto", "base", "nonexistent");
    s.dialogButton("Interactive rebase onto", "Open");
    GG_CHECK(s.dismissError());
    GG_CHECK(!editor(s).isOpen());
    ctx->MenuClick("//##MainMenuBar/Commit/Interactive rebase...");
    GG_REQUIRE(s.dialogOpen("Interactive rebase onto"));
    s.dialogText("Interactive rebase onto", "base", r.c[3].substr(0, 12));
    s.dialogButton("Interactive rebase onto", "Open");
    GG_REQUIRE(editorReady(s));
    GG_CHECK(rows(s) == (Rows{"pick c4", "pick c5"}));
    ctx->ItemClick(irWidget("ir_cancel").c_str());
    GG_CHECK(s.refs(r.path) == refsBefore);

    // Branches: part1 onto c1 with c2 dropped. Only part1 moves; HEAD (main) stays.
    s.showPanel("Branches");
    s.contextMenu("//Branches/branch_part1/###branch_part1", "Interactive rebase onto...");
    GG_REQUIRE(s.dialogOpen("Interactive rebase onto"));
    s.dialogText("Interactive rebase onto", "base", r.c[1]);
    s.dialogButton("Interactive rebase onto", "Open");
    GG_REQUIRE(editorReady(s));
    GG_CHECK(rows(s) == (Rows{"pick c2", "pick c3"}));
    GG_CHECK_STR_EQ(editor(s).context()->tipRef, "refs/heads/part1");
    key(s, r.c[2], ImGuiKey_D);
    GG_REQUIRE(start(s));
    GG_CHECK(subjects(s, r.path, "part1") == (std::vector<std::string>{"c3", "c1"}));
    GG_CHECK_STR_EQ(s.revParse(r.path, "part1~1"), r.c[1]);
    GG_CHECK_STR_EQ(s.revParse(r.path, "main"), r.c[5]);
    GG_CHECK_STR_EQ(s.head(r.path), r.c[5]);
    GG_CHECK(s.statusPorcelain(r.path).empty());
}

GG_TEST("rebase-i", "edit the list: Alt+arrows, drag, newest first, multi-select, keys, undo/redo, engine",
    "IR-KEY-ALT-UPDOWN", "IR-DRAG", "IR-NEWEST-FIRST", "IR-MULTISELECT", "IR-KEY-LETTERS", "IR-UNDO-REDO",
    "IR-INSERT-EXEC-BREAK", "IR-ENGINE-SHOWN", "IR-ACT-PICK", "IR-ACT-DROP", "IR-ACT-SQUASH", "IR-ACT-FIXUP",
    "IR-OPT-UPDATE-REFS")
{
    const Repo r = makeRepo(s);
    const std::string tree = s.revParse(r.path, "main^{tree}");
    GG_REQUIRE(s.openRepository(r.path));
    GG_REQUIRE(rowReady(s, r.c[2]));
    ctx->ItemClick(historyRow(r.c[2]).c_str());
    ctx->KeyPress(ImGuiKey_I);
    GG_REQUIRE(editorReady(s));
    // Nothing to undo yet; p on a pick row changes nothing.
    ctx->KeyPress(ImGuiMod_Ctrl | ImGuiKey_Z);
    key(s, r.c[2], ImGuiKey_P);
    ctx->KeyPress(ImGuiMod_Alt | ImGuiKey_DownArrow);
    ctx->KeyPress(ImGuiMod_Ctrl | ImGuiKey_Z);
    GG_CHECK(rows(s) == (Rows{"pick c2", "pick c3", "update-ref refs/heads/part1", "pick c4", "pick c5"}));
    // Without --update-refs part1 stays where it is.
    ctx->ItemUncheck(irWidget("ir_update_refs").c_str());
    GG_CHECK(rows(s) == (Rows{"pick c2", "pick c3", "pick c4", "pick c5"}));

    // Alt+Up twice, Alt+Down once (keyboard reorder).
    key(s, r.c[5], ImGuiMod_Alt | ImGuiKey_UpArrow);
    ctx->KeyPress(ImGuiMod_Alt | ImGuiKey_UpArrow);
    GG_CHECK(rows(s) == (Rows{"pick c2", "pick c5", "pick c3", "pick c4"}));
    ctx->KeyPress(ImGuiMod_Alt | ImGuiKey_UpArrow);
    ctx->KeyPress(ImGuiMod_Alt | ImGuiKey_UpArrow); // already first: nothing moves
    GG_CHECK(rows(s) == (Rows{"pick c5", "pick c2", "pick c3", "pick c4"}));
    ctx->KeyPress(ImGuiMod_Alt | ImGuiKey_DownArrow);
    ctx->KeyPress(ImGuiMod_Alt | ImGuiKey_DownArrow);
    GG_CHECK(rows(s) == (Rows{"pick c2", "pick c3", "pick c5", "pick c4"}));
    // Newest first: the list is shown the other way round and Alt+Up moves toward newer.
    ctx->ItemCheck(irWidget("ir_newest_first").c_str());
    GG_CHECK(editor(s).newestFirst());
    GG_CHECK(editor(s).displayOrder() == (std::vector<size_t>{3, 2, 1, 0}));
    key(s, r.c[5], ImGuiMod_Alt | ImGuiKey_UpArrow);
    ctx->KeyPress(ImGuiMod_Alt | ImGuiKey_UpArrow); // already newest
    GG_CHECK(rows(s) == (Rows{"pick c2", "pick c3", "pick c4", "pick c5"}));
    ctx->KeyPress(ImGuiMod_Alt | ImGuiKey_DownArrow);
    GG_CHECK(rows(s) == (Rows{"pick c2", "pick c3", "pick c5", "pick c4"}));
    ctx->KeyPress(ImGuiMod_Alt | ImGuiKey_UpArrow);
    ctx->ItemUncheck(irWidget("ir_newest_first").c_str());

    // Drag c2 onto c4 (below it: after), then c5 onto c3 (above it: before).
    ctx->ItemDragAndDrop(irRow(r.c[2]).c_str(), irRow(r.c[4]).c_str());
    s.ctx->Yield(2);
    GG_CHECK(rows(s) == (Rows{"pick c3", "pick c4", "pick c2", "pick c5"}));
    ctx->ItemDragAndDrop(irRow(r.c[5]).c_str(), irRow(r.c[3]).c_str());
    s.ctx->Yield(2);
    GG_CHECK(rows(s) == (Rows{"pick c5", "pick c3", "pick c4", "pick c2"}));
    // Two selected rows dragged together (dropping on one of them does nothing).
    click(s, r.c[3]);
    click(s, r.c[4], ImGuiMod_Ctrl);
    ctx->ItemDragAndDrop(irRow(r.c[3]).c_str(), irRow(r.c[4]).c_str());
    s.ctx->Yield(2);
    GG_CHECK(rows(s) == (Rows{"pick c5", "pick c3", "pick c4", "pick c2"}));
    ctx->ItemDragAndDrop(irRow(r.c[3]).c_str(), irRow(r.c[2]).c_str());
    s.ctx->Yield(2);
    GG_CHECK(rows(s) == (Rows{"pick c5", "pick c2", "pick c3", "pick c4"}));
    GG_CHECK(editor(s).selection() == (std::set<size_t>{2, 3}));
    ctx->ItemClick(irWidget("ir_undo").c_str());
    // Undo (Ctrl+Z and the button) and redo (Ctrl+Y and the button).
    ctx->KeyPress(ImGuiMod_Ctrl | ImGuiKey_Z);
    GG_CHECK(rows(s) == (Rows{"pick c3", "pick c4", "pick c2", "pick c5"}));
    ctx->ItemClick(irWidget("ir_undo").c_str());
    GG_CHECK(rows(s) == (Rows{"pick c2", "pick c3", "pick c4", "pick c5"}));
    ctx->KeyPress(ImGuiMod_Ctrl | ImGuiKey_Y);
    ctx->ItemClick(irWidget("ir_redo").c_str());
    ctx->KeyPress(ImGuiMod_Ctrl | ImGuiKey_Z);
    ctx->KeyPress(ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_Z);
    GG_CHECK(rows(s) == (Rows{"pick c5", "pick c3", "pick c4", "pick c2"}));
    GG_CHECK_STR_EQ(s.revParse(r.path, "main"), r.c[5]); // the repository's Undo was not touched

    // Multi-select: Ctrl-click and Shift-click; d drops every selected row.
    click(s, r.c[3]);
    click(s, r.c[2], ImGuiMod_Ctrl);
    GG_CHECK(editor(s).selection() == (std::set<size_t>{1, 3}));
    ctx->KeyPress(ImGuiKey_D);
    GG_CHECK(rows(s) == (Rows{"pick c5", "drop c3", "pick c4", "drop c2"}));
    ctx->KeyPress(ImGuiMod_Ctrl | ImGuiKey_Z);
    click(s, r.c[5], ImGuiMod_Shift); // from the last clicked row (c2)
    GG_CHECK(editor(s).selection() == (std::set<size_t>{0, 1, 2, 3}));
    click(s, r.c[4], ImGuiMod_Ctrl);
    GG_CHECK(editor(s).selection() == (std::set<size_t>{0, 1, 3}));

    // Keys set the action; the engine and its reason follow.
    GG_CHECK(editor(s).engine().engine == todo::Engine::InMemory);
    GG_CHECK(s.textShown("//Interactive rebase", "Engine: in memory"));
    key(s, r.c[4], ImGuiKey_R);
    GG_CHECK(rows(s)[2] == "reword c4");
    ctx->KeyPress(ImGuiKey_E);
    GG_CHECK(rows(s)[2] == "edit c4");
    GG_CHECK(editor(s).engine().engine == todo::Engine::Native);
    GG_CHECK(editor(s).engine().reason.find("row 3 is edit") != std::string::npos);
    GG_CHECK(!editor(s).canStart());
    ctx->ItemCheck(irWidget("ir_native").c_str()); // the user's choice
    GG_CHECK(editor(s).engine().reason.find("you chose") != std::string::npos);
    ctx->ItemUncheck(irWidget("ir_native").c_str());
    key(s, r.c[4], ImGuiKey_P);
    GG_CHECK(editor(s).engine().engine == todo::Engine::InMemory);
    // x inserts an exec line after the selection (a command is required), b a break; Delete
    // removes them (commit rows stay).
    ctx->KeyPress(ImGuiKey_X);
    GG_CHECK(rows(s) == (Rows{"pick c5", "pick c3", "pick c4", "exec", "pick c2"}));
    GG_CHECK(hasIssue(s, todo::Issue::Code::EmptyExec));
    GG_CHECK(editor(s).engine().reason.find("runs a command") != std::string::npos);
    s.setText("//Interactive rebase/**/###ir_exec_3", "make test");
    GG_CHECK(rows(s)[3] == "exec make test");
    GG_CHECK(!hasIssue(s, todo::Issue::Code::EmptyExec));
    ctx->ItemClick(irWidget("ir_insert_break").c_str());
    GG_CHECK(rows(s) == (Rows{"pick c5", "pick c3", "pick c4", "exec make test", "break", "pick c2"}));
    GG_CHECK(editor(s).engine().reason.find("row 4 runs a command") != std::string::npos);
    ctx->KeyPress(ImGuiKey_Delete);
    key(s, r.c[4], ImGuiKey_B);
    GG_CHECK(rows(s) == (Rows{"pick c5", "pick c3", "pick c4", "break", "exec make test", "pick c2"}));
    GG_CHECK(editor(s).engine().reason.find("row 4 is break") != std::string::npos);
    ctx->KeyPress(ImGuiKey_Delete);
    ctx->ItemClick(irWidget("ir_insert_exec").c_str()); // nothing selected: at the end
    GG_CHECK(rows(s).back() == "exec");
    ctx->ItemClick(irWidget("ir_undo").c_str());
    GG_CHECK(rows(s) == (Rows{"pick c5", "pick c3", "pick c4", "exec make test", "pick c2"}));
    click(s, r.c[4]);
    click(s, r.c[2], ImGuiMod_Shift);
    ctx->KeyPress(ImGuiKey_P); // commit rows only: nothing changes
    ctx->KeyPress(ImGuiKey_Delete);
    GG_CHECK(rows(s) == (Rows{"pick c5", "pick c3", "pick c4", "pick c2"}));
    ctx->KeyPress(ImGuiKey_Delete);                   // nothing selected
    ctx->KeyPress(ImGuiMod_Alt | ImGuiKey_UpArrow);   // nothing selected
    GG_CHECK(rows(s) == (Rows{"pick c5", "pick c3", "pick c4", "pick c2"}));
    // "Exec after every commit" runs through git rebase too.
    s.setText(irWidget("ir_exec_each"), "true");
    GG_CHECK(editor(s).engine().reason.find("exec after every commit") != std::string::npos);
    s.setText(irWidget("ir_exec_each"), "");

    // s and f: c4 squashed into c3, c2 fixed up into the same group.
    key(s, r.c[4], ImGuiKey_S);
    key(s, r.c[2], ImGuiKey_F);
    GG_CHECK(rows(s) == (Rows{"pick c5", "pick c3", "squash c4", "fixup c2"}));
    GG_CHECK(editor(s).engine().engine == todo::Engine::InMemory);
    GG_REQUIRE(start(s));
    GG_CHECK(subjects(s, r.path, "main") == (std::vector<std::string>{"c3", "c5", "c1"}));
    GG_CHECK_STR_EQ(s.revParse(r.path, "main^{tree}"), tree);
    GG_CHECK_STR_EQ(s.gitOut(r.path, {"log", "-1", "--format=%B", "main"}), "c3 add c\n\nc4 add d");
    GG_CHECK_STR_EQ(s.revParse(r.path, "part1"), r.c[3]); // no --update-refs
    GG_CHECK(s.statusPorcelain(r.path).empty());
}

GG_TEST("rebase-i", "messages: reword and squash editors, fixup -C, first row validation, one Undo",
    "IR-MSG-REWORD", "IR-MSG-SQUASH", "IR-ACT-REWORD", "IR-ACT-FIXUP-C", "IR-VALIDATE-FIRST", "IR-MEMORY-ONE-UNDO",
    "IR-ACT-UPDATE-REF")
{
    const Repo r = makeRepo(s);
    GG_REQUIRE(s.openRepository(r.path));
    GG_REQUIRE(rowReady(s, r.c[3]));
    ctx->ItemClick(historyRow(r.c[3]).c_str());
    ctx->KeyPress(ImGuiKey_I);
    GG_REQUIRE(editorReady(s));
    // [c3, update-ref part1, c4, c5]; reword c4 through its combo, with a message typed inline.
    s.comboSelect(irAction(r.c[4]).c_str(), "reword");
    const std::string msg = "//Interactive rebase/**/###ir_msg_" + r.c[4];
    GG_REQUIRE(s.waitUntil([&] { return s.itemExists(msg.c_str()); }));
    GG_CHECK_STR_EQ(editor(s).messageText(2), "c4 add d\n");
    s.setText(msg, "c4 reworded\n\nwith a body");
    GG_CHECK(editor(s).todo().items[2].message.has_value());
    // As a fixup of c3 the row has no message of its own any more.
    key(s, r.c[4], ImGuiKey_F);
    GG_CHECK(!editor(s).todo().items[2].message.has_value());
    ctx->KeyPress(ImGuiMod_Ctrl | ImGuiKey_Z);
    GG_CHECK(editor(s).todo().items[2].message.has_value());
    // c5 squashed into it: the group changed, so the typed message goes back to Git's template.
    key(s, r.c[5], ImGuiKey_S);
    GG_CHECK(!editor(s).todo().items[2].message.has_value());
    GG_CHECK(editor(s).messageText(2).rfind("# This is a combination of 2 commits.\n# This is the 1st commit message:\n\nc4 add d\n", 0) == 0);
    ctx->KeyPress(ImGuiMod_Ctrl | ImGuiKey_Z); // back: the typed message returns
    GG_CHECK(editor(s).todo().items[2].message.has_value());
    ctx->KeyPress(ImGuiMod_Ctrl | ImGuiKey_Y);
    s.setText(msg, "c4 and c5\n# a comment line\n\ncombined");
    // fixup -C on the first row: an error; Start is refused until it is fixed.
    s.comboSelect(irAction(r.c[3]).c_str(), "fixup -C");
    GG_CHECK(hasIssue(s, todo::Issue::Code::SquashWithoutCommit));
    GG_CHECK(s.textShown("//Interactive rebase", "Row 1: cannot 'fixup' without a previous commit"));
    GG_CHECK(!editor(s).canStart());
    ctx->ItemClick(irWidget("ir_start").c_str()); // disabled: nothing happens
    GG_CHECK(editor(s).isOpen());
    ctx->ItemClick(irWidget("ir_undo").c_str());
    GG_CHECK(!hasIssue(s, todo::Issue::Code::SquashWithoutCommit));
    GG_REQUIRE(start(s));
    GG_CHECK(subjects(s, r.path, "main") == (std::vector<std::string>{"c4", "c3", "c2", "c1"}));
    GG_CHECK_STR_EQ(s.gitOut(r.path, {"log", "-1", "--format=%B", "main"}), "c4 and c5\n\ncombined");
    GG_CHECK_STR_EQ(s.revParse(r.path, "main~1"), r.c[3]); // the unchanged prefix keeps its id
    GG_CHECK_STR_EQ(s.revParse(r.path, "part1"), r.c[3]);   // update-ref: stays with c3

    // One Undo reverts the whole rebase.
    ctx->ItemClick("//History/**/###row_wt");
    ctx->KeyPress(ImGuiMod_Ctrl | ImGuiKey_Z);
    GG_CHECK(s.waitUntil([&] { return s.revParse(r.path, "main") == r.c[5]; }));
    s.settle();
    GG_CHECK(s.statusPorcelain(r.path).empty());

    // fixup -C: c5's message replaces c4's (c4 moved after c5 by drag, c4 fixup -C into c5).
    GG_REQUIRE(rowReady(s, r.c[4]));
    ctx->ItemClick(historyRow(r.c[4]).c_str());
    ctx->KeyPress(ImGuiKey_I);
    GG_REQUIRE(editorReady(s));
    s.comboSelect(irAction(r.c[5]).c_str(), "fixup -C");
    GG_CHECK(rows(s) == (Rows{"pick c4", "fixup -C c5"}));
    GG_CHECK(!s.itemExists(("//Interactive rebase/**/###ir_msg_" + r.c[4]).c_str())); // no editor: Git keeps a message
    s.comboSelect(irAction(r.c[5]).c_str(), "fixup -c");
    GG_CHECK(s.waitUntil([&] { return s.itemExists(("//Interactive rebase/**/###ir_msg_" + r.c[4]).c_str()); }));
    s.comboSelect(irAction(r.c[5]).c_str(), "fixup -C");
    GG_REQUIRE(start(s));
    GG_CHECK_STR_EQ(s.gitOut(r.path, {"log", "-1", "--format=%B", "main"}), "c5 add e");
    GG_CHECK_STR_EQ(s.revParse(r.path, "main~1"), r.c[3]);
    GG_CHECK_STR_EQ(s.revParse(r.path, "main^{tree}"), s.revParse(r.path, r.c[5] + "^{tree}"));
}

GG_TEST("rebase-i", "autosquash places fixup!/squash!/amend! like git rebase -i --autosquash", "IR-OPT-AUTOSQUASH",
    "IR-AUTOSQUASH-ORDER")
{
    Repo r;
    r.path = s.fixture(Recipe::Empty);
    const fs::path& p = r.path;
    s.commitFile(p, "a.txt", "a\n", "c1 add a");
    const std::string c1 = s.head(p);
    s.commitFile(p, "b.txt", "b\n", "c2 add b");
    s.commitFile(p, "c.txt", "c\n", "c3 add c");
    s.commitFile(p, "b.txt", "b fixed\n", "fixup! c2 add b");
    s.commitFile(p, "c.txt", "c amended\n", "amend! c3 add c\n\nc3 add c, amended\n\nnew body");
    s.commitFile(p, "d.txt", "d\n", "c4 add d");
    s.commitFile(p, "b.txt", "b squashed\n", "squash! fixup! c2 add b\n\nsquash body");
    s.commitFile(p, "e.txt", "e\n", "fixup! c4");

    // Git's todo for the same range (the sequence editor copies it out and empties it: nothing runs).
    const fs::path out = s.root() / "git-todo.txt";
    s.gitMayFail(p, {"-c", "sequence.editor=f() { cp \"$1\" '" + out.string() + "'; : > \"$1\"; }; f", "rebase", "-i",
                        "--autosquash", c1});
    GG_CHECK(!fs::exists(p / ".git" / "rebase-merge"));
    Rows expected;
    for (const auto& line : gg::splitLines(s.read(s.root(), "git-todo.txt"))) {
        if (line.empty() || line[0] == '#')
            continue;
        std::string action = line.substr(0, line.find(' '));
        std::string rest = line.substr(action.size() + 1);
        if (rest.rfind("-C ", 0) == 0) {
            action += " -C";
            rest = rest.substr(3);
        }
        const std::string subject = s.gitOut(p, {"log", "-1", "--format=%s", rest.substr(0, rest.find(' '))});
        expected.push_back(action + " " + subject.substr(0, subject.find(' ')));
    }
    GG_CHECK(expected.size() == 7);
    const fs::path copy = s.root() / "copy";
    s.git(s.root(), {"clone", "-q", p.string(), copy.string()});
    s.track(copy);

    GG_REQUIRE(s.openRepository(p));
    const std::string c2 = s.revParse(p, "HEAD~6");
    GG_REQUIRE(rowReady(s, c2));
    ctx->ItemClick(historyRow(c2).c_str());
    ctx->KeyPress(ImGuiKey_I);
    GG_REQUIRE(editorReady(s));
    ctx->ItemUncheck(irWidget("ir_update_refs").c_str());
    const Rows initial = rows(s);
    ctx->ItemCheck(irWidget("ir_autosquash").c_str());
    GG_CHECK(rows(s) == expected);
    ctx->ItemUncheck(irWidget("ir_autosquash").c_str());
    GG_CHECK(rows(s) == initial);
    ctx->ItemCheck(irWidget("ir_autosquash").c_str());
    GG_REQUIRE(start(s));

    // The same rebase with git on a copy: trees, messages and authors match.
    s.git(copy, {"-c", "sequence.editor=true", "-c", "core.editor=true", "rebase", "-q", "-i", "--autosquash", c1});
    auto history = [&](const fs::path& repo) {
        return s.gitOut(repo, {"log", "--format=%T %an %ae %at%n%B", "HEAD"});
    };
    GG_CHECK_STR_EQ(history(p), history(copy));
}

GG_TEST("rebase-i", "options: onto, update-refs, autostash, committer date", "IR-OPT-ONTO", "IR-OPT-AUTOSTASH",
    "IR-OPT-COMMITTER-DATE")
{
    const Repo r = makeRepo(s);
    s.git(r.path, {"switch", "-q", "-c", "other", r.c[1]});
    s.commitFile(r.path, "o.txt", "o\n", "o1 add o");
    const std::string o1 = s.head(r.path);
    s.git(r.path, {"switch", "-q", "main"});
    GG_REQUIRE(s.openRepository(r.path));
    GG_REQUIRE(rowReady(s, r.c[2]));
    ctx->ItemClick(historyRow(r.c[2]).c_str());
    ctx->KeyPress(ImGuiKey_I);
    GG_REQUIRE(editorReady(s));
    GG_CHECK(editor(s).options().updateRefs); // on by default
    // An unknown base is refused; the editor stays.
    ctx->ItemClick(irWidget("ir_onto").c_str());
    ctx->KeyChars("nope");
    ctx->KeyPress(ImGuiKey_Enter);
    GG_CHECK(s.dismissError());
    GG_CHECK(editor(s).isOpen());
    GG_CHECK_STR_EQ(editor(s).context()->onto, r.c[1]);
    // Onto "other" (Enter applies; the list stays).
    s.setText(irWidget("ir_onto"), "");
    ctx->KeyChars("other");
    ctx->KeyPress(ImGuiKey_Enter);
    GG_REQUIRE(s.waitUntil([&] { return editor(s).context() && editor(s).context()->onto == o1; }));
    GG_CHECK(rows(s) == (Rows{"pick c2", "pick c3", "update-ref refs/heads/part1", "pick c4", "pick c5"}));
    GG_CHECK(s.textShown("//Interactive rebase", "onto " + s.session()->shortId(ggui::core::Oid::fromHex(o1))));
    // Update refs off and on again: the update-ref line comes back after c3.
    ctx->ItemUncheck(irWidget("ir_update_refs").c_str());
    ctx->ItemCheck(irWidget("ir_update_refs").c_str());
    GG_CHECK(rows(s) == (Rows{"pick c2", "pick c3", "update-ref refs/heads/part1", "pick c4", "pick c5"}));
    // A local change, kept through the rebase by autostash; committer dates kept.
    s.write(r.path, "a.txt", "a local change\n");
    ctx->ItemCheck(irWidget("ir_autostash").c_str());
    s.comboSelect(irWidget("ir_committer_date").c_str(), "Keep original");
    GG_CHECK(editor(s).options().keepCommitterDate);
    GG_REQUIRE(start(s));
    GG_CHECK(subjects(s, r.path, "main") == (std::vector<std::string>{"c5", "c4", "c3", "c2", "o1", "c1"}));
    GG_CHECK_STR_EQ(s.revParse(r.path, "main~4"), o1);
    GG_CHECK_STR_EQ(s.revParse(r.path, "part1"), s.revParse(r.path, "main~2"));
    GG_CHECK_STR_EQ(s.read(r.path, "a.txt"), "a local change\n");
    GG_CHECK(s.gitOut(r.path, {"stash", "list"}).empty());
    GG_CHECK_STR_EQ(s.gitOut(r.path, {"log", "-1", "--format=%ct", "main"}), s.gitOut(r.path, {"log", "-1", "--format=%ct", r.c[5]}));

    // Committer date "now" (the default) and no autostash: the local change stays too (no collision).
    const std::string tip = s.revParse(r.path, "main");
    GG_REQUIRE(rowReady(s, tip));
    ctx->ItemClick(historyRow(tip).c_str());
    ctx->KeyPress(ImGuiKey_I);
    GG_REQUIRE(editorReady(s));
    key(s, tip, ImGuiKey_R);
    s.setText("//Interactive rebase/**/###ir_msg_" + tip, "c5 reworded");
    GG_REQUIRE(start(s));
    GG_CHECK_STR_EQ(s.gitOut(r.path, {"log", "-1", "--format=%s", "main"}), "c5 reworded");
    GG_CHECK(std::stoll(s.gitOut(r.path, {"log", "-1", "--format=%ct", "main"}))
        > std::stoll(s.gitOut(r.path, {"log", "-1", "--format=%ct", tip})));
    GG_CHECK_STR_EQ(s.read(r.path, "a.txt"), "a local change\n");

    // A failed apply puts the autostash back: part1 (moved by the rebase) is locked.
    const std::string tip2 = s.revParse(r.path, "main");
    const std::string c2 = s.revParse(r.path, "main~3");
    GG_REQUIRE(rowReady(s, c2));
    ctx->ItemClick(historyRow(c2).c_str());
    ctx->KeyPress(ImGuiKey_I);
    GG_REQUIRE(editorReady(s));
    key(s, c2, ImGuiKey_D);
    ctx->ItemCheck(irWidget("ir_autostash").c_str());
    s.write(r.path, ".git/refs/heads/part1.lock", "");
    ctx->ItemClick(irWidget("ir_start").c_str());
    GG_CHECK(s.dismissError());
    fs::remove(r.path / ".git" / "refs" / "heads" / "part1.lock");
    GG_CHECK(editor(s).isOpen());
    GG_CHECK_STR_EQ(s.revParse(r.path, "main"), tip2);
    GG_CHECK_STR_EQ(s.read(r.path, "a.txt"), "a local change\n");
    GG_CHECK(s.gitOut(r.path, {"stash", "list"}).empty());
    ctx->ItemClick(irWidget("ir_cancel").c_str());

    // From the root, c1 (which adds a.txt) dropped: the autostash no longer applies and stays.
    GG_REQUIRE(rowReady(s, r.c[1]));
    ctx->ItemClick(historyRow(r.c[1]).c_str());
    ctx->KeyPress(ImGuiKey_I);
    GG_REQUIRE(editorReady(s));
    GG_CHECK(editor(s).context()->upstream.empty());
    GG_CHECK(s.textShown("//Interactive rebase", "onto the root"));
    key(s, r.c[1], ImGuiKey_D);
    ctx->ItemCheck(irWidget("ir_autostash").c_str());
    GG_REQUIRE(start(s));
    GG_CHECK(subjects(s, r.path, "main").back() == "o1");
    GG_CHECK(!s.gitOut(r.path, {"stash", "list"}).empty());
    GG_CHECK(s.waitUntil([&] {
        for (const auto& t : s.app.toasts())
            if (t.message.find("autostash") != std::string::npos)
                return true;
        return false;
    }));
}

GG_TEST("rebase-i", "validation warnings, Cancel changes nothing, a branch moved meanwhile is refused",
    "IR-VALIDATE-DROP-BRANCH", "IR-VALIDATE-PUBLISHED", "IR-MEMORY-CANCEL", "IR-TIP-MOVED")
{
    const Repo r = makeRepo(s);
    s.git(r.path, {"update-ref", "refs/remotes/origin/main", r.c[2]});
    s.git(r.path, {"switch", "-q", "-c", "side", r.c[1]});
    s.commitFile(r.path, "s.txt", "s\n", "s1 add s");
    const std::string s1 = s.head(r.path);
    s.git(r.path, {"switch", "-q", "--detach"});
    s.commitFile(r.path, "t.txt", "t\n", "t1 add t");
    const std::string t1 = s.head(r.path);
    s.git(r.path, {"tag", "only-tag"});
    s.git(r.path, {"switch", "-q", "main"});
    const auto refsBefore = s.refs(r.path);
    GG_REQUIRE(s.openRepository(r.path));
    GG_REQUIRE(rowReady(s, r.c[2]));
    ctx->ItemClick(historyRow(r.c[2]).c_str());
    ctx->KeyPress(ImGuiKey_I);
    GG_REQUIRE(editorReady(s));
    GG_CHECK(editor(s).issues().empty());
    // Dropping c2 (published) and c3: part1 keeps none of its commits.
    key(s, r.c[2], ImGuiKey_D);
    GG_CHECK(hasIssue(s, todo::Issue::Code::Published));
    GG_CHECK(s.textShown("//Interactive rebase", "is already on a remote"));
    key(s, r.c[3], ImGuiKey_D);
    GG_CHECK(hasIssue(s, todo::Issue::Code::BranchLosesCommits));
    GG_CHECK(s.textShown("//Interactive rebase", "every commit of branch 'part1' is dropped"));
    key(s, r.c[4], ImGuiKey_D);
    key(s, r.c[5], ImGuiKey_D);
    GG_CHECK(s.textShown("//Interactive rebase", "every commit of branch 'main' is dropped"));
    GG_CHECK(editor(s).canStart()); // warnings only
    ctx->ItemClick(irWidget("ir_cancel").c_str());
    GG_CHECK(!editor(s).isOpen());
    s.settle();
    GG_CHECK(s.refs(r.path) == refsBefore);
    GG_CHECK(s.statusPorcelain(r.path).empty());

    // A commit on another branch: that branch is the tip. One only a tag reaches: refused.
    GG_REQUIRE(rowReady(s, s1));
    ctx->ItemClick(historyRow(s1).c_str());
    ctx->KeyPress(ImGuiKey_I);
    GG_REQUIRE(editorReady(s));
    GG_CHECK_STR_EQ(editor(s).context()->tipRef, "refs/heads/side");
    ctx->ItemClick(irWidget("ir_cancel").c_str());
    GG_REQUIRE(rowReady(s, t1));
    ctx->ItemClick(historyRow(t1).c_str());
    ctx->KeyPress(ImGuiKey_I);
    GG_CHECK(s.dismissError());
    GG_CHECK(!editor(s).isOpen());

    // main moves (plain git) while the editor is open: Start is refused, nothing changes.
    ctx->ItemClick(historyRow(r.c[2]).c_str());
    ctx->KeyPress(ImGuiKey_I);
    GG_REQUIRE(editorReady(s));
    key(s, r.c[4], ImGuiKey_D);
    s.commitFile(r.path, "f.txt", "f\n", "c6 add f");
    const std::string c6 = s.head(r.path);
    GG_REQUIRE(rowReady(s, c6));
    ctx->ItemClick(irWidget("ir_start").c_str());
    GG_CHECK(s.dismissError());
    GG_CHECK(editor(s).isOpen());
    GG_CHECK_STR_EQ(s.revParse(r.path, "main"), c6);
    // Closing the panel cancels it.
    ctx->WindowClose("//Interactive rebase");
    GG_CHECK(s.waitUntil([&] { return !editor(s).isOpen(); }));
}

GG_TEST("rebase-i", "open as interactive rebase from the Squash and Rebase onto dialogs", "IR-OPEN-AS")
{
    const Repo r = makeRepo(s);
    s.git(r.path, {"branch", "other", r.c[1]});
    GG_REQUIRE(s.openRepository(r.path));
    GG_REQUIRE(rowReady(s, r.c[4]));
    // Rebase onto other: the starting todo has the new base.
    s.contextMenu(historyRow(r.c[4]).c_str(), "Rebase onto...");
    GG_REQUIRE(s.dialogOpen("Rebase onto"));
    s.dialogText("Rebase onto", "destination", "other");
    s.dialogButton("Rebase onto", "Open as interactive rebase...");
    GG_REQUIRE(editorReady(s));
    GG_CHECK(rows(s) == (Rows{"pick c4", "pick c5"}));
    GG_CHECK_STR_EQ(editor(s).context()->onto, r.c[1]);
    ctx->ItemClick(irWidget("ir_cancel").c_str());
    // An unknown target is refused.
    s.contextMenu(historyRow(r.c[4]).c_str(), "Squash...");
    GG_REQUIRE(s.dialogOpen("Squash"));
    s.dialogText("Squash", "target", "nope");
    s.dialogButton("Squash", "Open as interactive rebase...");
    GG_CHECK(s.dismissError());
    GG_CHECK(!editor(s).isOpen());
    // Squash c4 into c2: c4 moves after c2 as squash; its message is the combination.
    s.contextMenu(historyRow(r.c[4]).c_str(), "Squash...");
    GG_REQUIRE(s.dialogOpen("Squash"));
    s.dialogText("Squash", "target", r.c[2]);
    s.dialogButton("Squash", "Open as interactive rebase...");
    GG_REQUIRE(editorReady(s));
    GG_CHECK(rows(s) == (Rows{"pick c2", "squash c4", "pick c3", "update-ref refs/heads/part1", "pick c5"}));
    GG_REQUIRE(start(s));
    GG_CHECK(subjects(s, r.path, "main") == (std::vector<std::string>{"c5", "c3", "c2", "c1"}));
    GG_CHECK_STR_EQ(s.gitOut(r.path, {"log", "-1", "--format=%B", "main~2"}), "c2 add b\n\nc4 add d");
    GG_CHECK_STR_EQ(s.revParse(r.path, "part1"), s.revParse(r.path, "main~1"));
    // Squash into the parent with the fixup choice: c5 into c3'.
    const std::string tip = s.revParse(r.path, "main");
    GG_REQUIRE(rowReady(s, tip));
    s.contextMenu(historyRow(tip).c_str(), "Squash...");
    GG_REQUIRE(s.dialogOpen("Squash"));
    s.dialogCheck("Squash", "combine", "Combine the messages (squash; otherwise keep the target's: fixup)", false);
    s.dialogButton("Squash", "Open as interactive rebase...");
    GG_REQUIRE(editorReady(s));
    GG_CHECK(rows(s) == (Rows{"pick c3", "fixup c5", "update-ref refs/heads/part1"}));
    ctx->ItemClick(irWidget("ir_cancel").c_str());
}

GG_TEST("rebase-i", "a detached HEAD follows the rebase; update-ref moves a branch", "IR-ACT-PICK", "IR-ACT-UPDATE-REF",
    "IR-PREVIEW-BRANCHES")
{
    const Repo r = makeRepo(s);
    s.git(r.path, {"switch", "-q", "--detach", r.c[4]});
    GG_REQUIRE(s.openRepository(r.path));
    GG_REQUIRE(rowReady(s, r.c[3]));
    ctx->ItemClick(historyRow(r.c[3]).c_str());
    ctx->KeyPress(ImGuiKey_I);
    GG_REQUIRE(editorReady(s));
    GG_CHECK(editor(s).context()->tipRef.empty());
    GG_CHECK(s.textShown("//Interactive rebase", "Rebase 2 commit(s) of HEAD onto"));
    click(s, r.c[4]);
    ctx->KeyPress(ImGuiMod_Alt | ImGuiKey_UpArrow);
    ctx->KeyPress(ImGuiMod_Alt | ImGuiKey_UpArrow);
    GG_CHECK(rows(s) == (Rows{"pick c4", "pick c3", "update-ref refs/heads/part1"}));
    // The preview: the detached HEAD and part1 end at c3.
    const Preview* p = previewReady(s);
    GG_REQUIRE(p && p->ok && p->rows.size() == 2);
    GG_CHECK(p->rows[1].branches == (Rows{"part1", "HEAD"}));
    GG_CHECK(std::any_of(p->moves.begin(), p->moves.end(), [](const auto& m) { return m.ref == "HEAD"; }));
    GG_CHECK(s.itemExists((previewPane(s) + "/**/###irp_badge_HEAD").c_str()));
    const Preview expected = *p;
    GG_REQUIRE(start(s));
    checkMatches(s, r.path, "HEAD", expected);
    GG_CHECK(s.session()->snapshot()->headDetached);
    GG_CHECK(subjects(s, r.path, "HEAD") == (std::vector<std::string>{"c3", "c4", "c2", "c1"}));
    GG_CHECK_STR_EQ(s.revParse(r.path, "main"), r.c[5]); // branches stay
    GG_CHECK_STR_EQ(s.revParse(r.path, "part1"), s.head(r.path)); // update-ref after c3
    GG_CHECK(s.statusPorcelain(r.path).empty());
}

// ---- live preview (P3-17) ------------------------------------------------------------------------

GG_TEST("rebase-i", "live preview: first-class conflicts and moving branches, the same as Start and git rebase -i",
    "IR-PREVIEW", "IR-PREVIEW-CONFLICTS", "IR-PREVIEW-BRANCHES")
{
    const LineRepo r = makeLineRepo(s);
    const fs::path copy = s.root() / "copy";
    s.git(s.root(), {"clone", "-q", r.path.string(), copy.string()});
    s.track(copy);
    GG_REQUIRE(s.openRepository(r.path));
    GG_REQUIRE(rowReady(s, r.c2));
    ctx->ItemClick(historyRow(r.c2).c_str());
    ctx->KeyPress(ImGuiKey_I);
    GG_REQUIRE(editorReady(s));

    // Nothing edited yet: every commit stays, no branch moves.
    const Preview* p = previewReady(s);
    GG_REQUIRE(p && p->ok);
    GG_CHECK(previewSubjects(*p) == (Rows{"c2", "c3", "c4"}));
    GG_CHECK(p->rows[0].unchanged && p->rows[1].unchanged && p->rows[2].unchanged);
    GG_CHECK_STR_EQ(p->rows[2].id, r.c4);
    GG_CHECK(p->rows[1].branches == (Rows{"part1"}));
    GG_CHECK(p->rows[2].branches == (Rows{"main"}));
    GG_CHECK(p->moves.empty());
    GG_CHECK_STR_EQ(p->onto, r.c1);
    GG_CHECK(previewShows(s, "3 commit(s)"));
    GG_CHECK(previewShows(s, "c1 add a"));      // the base
    GG_CHECK(s.itemExists((previewPane(s) + "/**/###irp_badge_part1").c_str()));

    // c4 before c3 (conflict-free): c2 keeps its id, part1 moves with the update-ref row to the tip.
    click(s, r.c4);
    ctx->KeyPress(ImGuiMod_Alt | ImGuiKey_UpArrow);
    ctx->KeyPress(ImGuiMod_Alt | ImGuiKey_UpArrow);
    GG_CHECK(rows(s) == (Rows{"pick c2", "pick c4", "pick c3", "update-ref refs/heads/part1"}));
    p = previewReady(s);
    GG_REQUIRE(p && p->ok);
    GG_CHECK(previewSubjects(*p) == (Rows{"c2", "c4", "c3"}));
    GG_CHECK(p->rows[0].unchanged && !p->rows[1].unchanged && !p->rows[2].unchanged);
    GG_CHECK(p->rows[2].branches == (Rows{"main", "part1"}));
    GG_CHECK(p->moves.size() == 2);
    GG_CHECK(previewShows(s, "Moves: main, part1"));
    for (const auto& row : p->rows)
        GG_CHECK(row.conflicts.empty() && row.decisions.empty() && !row.empty);
    // git rebase -i with the same todo on a copy: the same trees, subjects and branch positions.
    s.write(s.root(), "todo.txt", todo::format(editor(s).todo()));
    s.git(copy, {"-c", "sequence.editor=cp '" + (s.root() / "todo.txt").string() + "'", "-c", "core.editor=true",
                    "rebase", "-q", "-i", "--update-refs", r.c1});
    checkMatches(s, copy, "main", *p);
    GG_CHECK_STR_EQ(s.revParse(copy, "part1"), s.revParse(copy, "main"));
    // An edit row and an exec row need git rebase, but the preview shows the history all the same.
    key(s, r.c3, ImGuiKey_E);
    ctx->KeyPress(ImGuiKey_X);
    s.setText("//Interactive rebase/**/###ir_exec_3", "true");
    GG_CHECK(editor(s).engine().engine == todo::Engine::Native);
    const Preview* native = previewReady(s);
    GG_REQUIRE(native && native->ok);
    GG_CHECK(previewSubjects(*native) == (Rows{"c2", "c4", "c3"}));
    for (int i = 0; i < 3; ++i) // the command, the exec row, the edit action
        ctx->ItemClick(irWidget("ir_undo").c_str());
    GG_CHECK(rows(s) == (Rows{"pick c2", "pick c4", "pick c3", "update-ref refs/heads/part1"}));

    // Without c2, c3 conflicts on a.txt; the conflict is carried into its descendants.
    key(s, r.c2, ImGuiKey_D);
    p = previewReady(s);
    GG_REQUIRE(p && p->ok);
    GG_CHECK(previewSubjects(*p) == (Rows{"c4", "c3"}));
    GG_CHECK(p->rows[0].conflicts.empty());
    GG_CHECK(conflictPaths(p->rows[1]) == (Rows{"a.txt"}));
    GG_CHECK(p->rows[1].newConflicts);
    GG_CHECK(p->rows[1].decisions.empty());
    GG_CHECK(previewShows(s, "2 commit(s), 1 with conflicts"));
    s.screenshot("rebase-i-preview-conflicts");
    // Without --update-refs part1 stays on the old commits.
    ctx->ItemUncheck(irWidget("ir_update_refs").c_str());
    p = previewReady(s);
    GG_REQUIRE(p && p->ok);
    GG_CHECK(p->staying == (Rows{"part1"}));
    GG_CHECK(p->rows[1].branches == (Rows{"main"}));
    GG_CHECK(previewShows(s, "Stay on the old commits: part1"));
    ctx->ItemCheck(irWidget("ir_update_refs").c_str());
    p = previewReady(s);
    GG_REQUIRE(p && p->ok && p->staying.empty());
    // A result row selects its rows in the list; its tooltip lists the conflicted files.
    ctx->ItemClick((previewPane(s) + "/**/###irp_row_1").c_str());
    GG_CHECK(editor(s).selection() == (std::set<size_t>{2}));
    ctx->MouseMove((previewPane(s) + "/**/###irp_row_1").c_str());
    ctx->SleepNoSkip(1.0f, 0.1f);
    GG_CHECK(s.textShown("//##Tooltip_00", "First-class conflicts in:"));
    ctx->MouseMove((previewPane(s) + "/**/###irp_row_0").c_str());
    ctx->SleepNoSkip(1.0f, 0.1f);
    GG_CHECK(s.textShown("//##Tooltip_00", "New commit from"));

    // Every commit dropped: the branches go to the base.
    key(s, r.c4, ImGuiKey_D);
    key(s, r.c3, ImGuiKey_D);
    p = previewReady(s);
    GG_REQUIRE(p && p->ok);
    GG_CHECK(p->rows.empty());
    GG_CHECK(p->ontoBranches == (Rows{"main", "part1"}));
    GG_CHECK(previewShows(s, "0 commit(s)"));
    ctx->KeyPress(ImGuiMod_Ctrl | ImGuiKey_Z);
    ctx->KeyPress(ImGuiMod_Ctrl | ImGuiKey_Z);
    p = previewReady(s);
    GG_REQUIRE(p && p->ok && p->rows.size() == 2);

    // Start gives exactly the previewed result.
    const Preview expected = *p;
    GG_REQUIRE(start(s));
    checkMatches(s, r.path, "main", expected);
    GG_CHECK_STR_EQ(s.revParse(r.path, "part1"), s.revParse(r.path, "main"));
    GG_CHECK(s.statusPorcelain(r.path).empty());
}

GG_TEST("rebase-i", "live preview: non-text conflicts that need a decision, commits that are or become empty",
    "IR-PREVIEW-CONFLICTS", "IR-PREVIEW-EMPTY")
{
    const fs::path path = s.fixture(Recipe::Empty);
    s.write(path, "bin.dat", std::string("A\0", 2));
    s.commitFile(path, "a.txt", "a\n", "c1 add a");
    s.git(path, {"add", "bin.dat"});
    s.git(path, {"commit", "-q", "--amend", "--no-edit"});
    const std::string c1 = s.head(path);
    s.write(path, "bin.dat", std::string("B\0", 2));
    s.git(path, {"commit", "-q", "-am", "c2 binary B"});
    const std::string c2 = s.head(path);
    s.write(path, "bin.dat", std::string("C\0", 2));
    s.git(path, {"commit", "-q", "-am", "c3 binary C"});
    const std::string c3 = s.head(path);
    s.git(path, {"commit", "-q", "--allow-empty", "-m", "c4 empty"});
    s.commitFile(path, "f.txt", "f\n", "c5 add f");
    s.git(path, {"rm", "-q", "f.txt"});
    s.git(path, {"commit", "-q", "-m", "c6 remove f"});
    const std::string c6 = s.head(path);
    s.commitFile(path, "f.txt", "f\n", "c7 add f again");
    const std::string c7 = s.head(path);

    GG_REQUIRE(s.openRepository(path));
    GG_REQUIRE(rowReady(s, c2));
    ctx->ItemClick(historyRow(c2).c_str());
    ctx->KeyPress(ImGuiKey_I);
    GG_REQUIRE(editorReady(s));
    const Preview* p = previewReady(s);
    GG_REQUIRE(p && p->ok);
    GG_CHECK(previewSubjects(*p) == (Rows{"c2", "c3", "c4", "c5", "c6", "c7"}));
    // c4 was empty to begin with (Git keeps it).
    GG_CHECK(p->rows[2].empty && p->rows[2].wasEmpty);
    GG_CHECK(!p->rows[3].empty && !p->rows[5].empty);
    GG_CHECK(previewShows(s, "6 commit(s), 1 empty"));

    // c3 before c2: both change the binary file, which needs a decision (pre-flight).
    click(s, c3);
    ctx->KeyPress(ImGuiMod_Alt | ImGuiKey_UpArrow);
    p = previewReady(s);
    GG_REQUIRE(p && p->ok);
    GG_CHECK(previewSubjects(*p) == (Rows{"c3", "c2", "c4", "c5", "c6", "c7"}));
    GG_REQUIRE(p->rows[0].decisions.size() == 1 && p->rows[1].decisions.size() == 1);
    GG_CHECK_STR_EQ(p->rows[0].decisions[0].path, "bin.dat");
    GG_CHECK_STR_EQ(p->rows[0].decisions[0].kind, "binary");
    GG_CHECK(p->rows[0].conflicts.empty());
    GG_CHECK(previewShows(s, "2 need a decision"));
    ctx->MouseMove((previewPane(s) + "/**/###irp_row_0").c_str());
    ctx->SleepNoSkip(1.0f, 0.1f);
    GG_CHECK(s.textShown("//##Tooltip_00", "bin.dat (binary)"));
    // Start asks about exactly those; Cancel leaves everything as it was.
    const std::string tip = s.head(path);
    ctx->ItemClick(irWidget("ir_start").c_str());
    GG_REQUIRE(s.dialogOpen("Resolve conflicts before rewriting"));
    const ggui::Form* f = s.app.dialogs().current();
    GG_REQUIRE(f->field("conflict_0") && f->field("conflict_1") && !f->field("conflict_2"));
    GG_CHECK(f->field("conflict_0")->text.find("c3 binary C: bin.dat (binary)") != std::string::npos);
    GG_CHECK(f->field("conflict_1")->text.find("c2 binary B: bin.dat (binary)") != std::string::npos);
    s.dialogButton("Resolve conflicts before rewriting", "Cancel");
    s.settle();
    GG_CHECK_STR_EQ(s.head(path), tip);
    GG_CHECK(editor(s).isOpen());

    // Back to Git's order, c6 dropped: c7 adds f.txt that is already there, so it becomes empty.
    // Without an identity for the new commits the preview shows the engine's error.
    s.git(path, {"config", "user.name", ""});
    ctx->WindowFocus("//Interactive rebase");
    ctx->KeyPress(ImGuiMod_Ctrl | ImGuiKey_Z);
    key(s, c6, ImGuiKey_D);
    p = previewReady(s);
    GG_REQUIRE(p && !p->ok);
    GG_CHECK(previewShows(s, "Cannot compute the result:"));
    s.git(path, {"config", "--unset", "user.name"});
    ctx->KeyPress(ImGuiMod_Ctrl | ImGuiKey_Z);
    ctx->KeyPress(ImGuiMod_Ctrl | ImGuiKey_Y);
    p = previewReady(s);
    GG_REQUIRE(p && p->ok);
    GG_CHECK(previewSubjects(*p) == (Rows{"c2", "c3", "c4", "c5", "c7"}));
    GG_CHECK(p->rows[4].empty && !p->rows[4].wasEmpty);
    GG_CHECK(p->rows[0].unchanged && p->rows[3].unchanged && !p->rows[4].unchanged);
    GG_CHECK(previewShows(s, "5 commit(s), 2 empty"));
    s.screenshot("rebase-i-preview-empty");
    ctx->MouseMove((previewPane(s) + "/**/###irp_row_4").c_str());
    ctx->SleepNoSkip(1.0f, 0.1f);
    GG_CHECK(s.textShown("//##Tooltip_00", "Becomes empty"));
    ctx->MouseMove((previewPane(s) + "/**/###irp_row_2").c_str());
    ctx->SleepNoSkip(1.0f, 0.1f);
    GG_CHECK(s.textShown("//##Tooltip_00", "it was empty before"));
    const Preview expected = *p;
    GG_REQUIRE(start(s));
    checkMatches(s, path, "main", expected);
    GG_CHECK_STR_EQ(s.revParse(path, "main~5"), c1);
    GG_CHECK(s.statusPorcelain(path).empty());
    (void)c7;
}

GG_TEST("rebase-i", "live preview on a worker: the newest edit wins, frames never wait, nothing is written",
    "IR-PREVIEW-LATEST", "IR-PREVIEW-NO-WRITE", "IR-PREVIEW")
{
    // 30 commits from the root, each adding a file.
    const fs::path path = s.fixture(Recipe::Empty);
    std::vector<std::string> c{""};
    for (int i = 1; i <= 30; ++i) {
        s.commitFile(path, "f" + std::to_string(i) + ".txt", std::to_string(i) + "\n", "c" + std::to_string(i) + " add f" + std::to_string(i));
        c.push_back(s.head(path));
    }
    const auto before = s.gitDirBytes(path);
    GG_REQUIRE(s.openRepository(path));
    GG_REQUIRE(rowReady(s, c[1]));
    ctx->ItemClick(historyRow(c[1]).c_str());
    ctx->KeyPress(ImGuiKey_I);
    GG_REQUIRE(editorReady(s));
    const Preview* p = previewReady(s);
    GG_REQUIRE(p && p->ok);
    GG_CHECK(p->rows.size() == 30);
    GG_CHECK(p->onto.empty());
    GG_CHECK(previewShows(s, "(the root)"));

    // Slow workers: several edits in a row. The list reacts at once, only the newest preview is
    // shown and no frame waits for the worker.
    auto& probe = ggui::frameProbe();
    const int requested = editor(s).previewsRequested();
    const int shown = editor(s).previewsShown();
    gg::setSlowGitLatency(std::chrono::milliseconds(600));
    probe.reset();
    key(s, c[5], ImGuiKey_D);
    key(s, c[9], ImGuiKey_D);
    ctx->KeyPress(ImGuiMod_Alt | ImGuiKey_UpArrow);
    key(s, c[20], ImGuiKey_D);
    key(s, c[9], ImGuiKey_P);
    GG_CHECK(editor(s).previewPending());
    GG_CHECK(previewShows(s, "Updating..."));
    p = previewReady(s, 60.0f);
    gg::setSlowGitLatency(std::chrono::milliseconds(0));
    GG_REQUIRE(p && p->ok);
    ctx->LogInfo("frames %lld, max %.1f ms; previews requested %d, shown %d", probe.frames, probe.maxMs,
        editor(s).previewsRequested() - requested, editor(s).previewsShown() - shown);
    GG_CHECK(editor(s).previewsRequested() - requested == 5);
    GG_CHECK(editor(s).previewsShown() - shown < 5);
    GG_CHECK(probe.maxMs < 33.0);
    GG_CHECK(p->rows.size() == 28);
    const auto names = previewSubjects(*p);
    GG_CHECK(std::find(names.begin(), names.end(), "c20") == names.end());
    GG_CHECK(std::find(names.begin(), names.end(), "c5") == names.end());
    GG_CHECK(std::find(names.begin(), names.end(), "c9") != names.end());

    // Errors in the list: no preview until they are fixed.
    s.comboSelect(irAction(c[1]).c_str(), "fixup");
    GG_CHECK(editor(s).preview() == nullptr);
    GG_CHECK(previewShows(s, "Fix the errors in the list"));
    ctx->ItemClick(irWidget("ir_undo").c_str());
    GG_REQUIRE(previewReady(s));

    // Cancelled from the toolbar: the preview says so and the list stays editable.
    gg::setSlowGitLatency(std::chrono::milliseconds(3000));
    key(s, c[3], ImGuiKey_D);
    GG_CHECK(s.waitUntil([&] { return s.itemExists("//##Toolbar/Cancel##tb_cancel"); }, 10.0f));
    ctx->ItemClick("//##Toolbar/Cancel##tb_cancel");
    GG_CHECK(s.waitUntil([&] { return !editor(s).previewPending(); }));
    gg::setSlowGitLatency(std::chrono::milliseconds(0));
    GG_CHECK(previewShows(s, "The preview was cancelled."));
    key(s, c[3], ImGuiKey_P);
    GG_REQUIRE(previewReady(s));

    // Nothing the previews computed reached the repository.
    ctx->ItemClick(irWidget("ir_cancel").c_str());
    s.settle();
    ctx->MenuClick("//##MainMenuBar/Repository/Close repository");
    ctx->Yield(3);
    const auto after = s.gitDirBytes(path);
    for (const auto& [name, bytes] : after)
        if (!before.count(name) || before.at(name) != bytes)
            ctx->LogError("changed under .git: %s", name.c_str());
    GG_CHECK(after == before);
}

} // namespace ggtest
