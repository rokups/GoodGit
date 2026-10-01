// ggui's todo editor as Git's sequence.editor (§4.13 "Optional", §6): the Settings
// option (per scope, asking before it replaces a user's own sequence.editor), plain `git rebase -i`
// run as a background test step that waits while the list is edited in ggui, Save / Cancel / closing
// the editor, `git rebase --edit-todo` from a terminal, a --rebase-merges list, a git interrupted
// while it waits, and `git gg sequence-editor` without a ggui for the repository (it starts one, or
// uses git's editor without a display).
#include "panels/HistoryPanel.hpp"
#include "panels/RebasePanel.hpp"
#include "shell/App.hpp"
#include "shell/Session.hpp"
#include "tests/Harness.hpp"
#include "util/Env.hpp"

#include <libgg/GitRunner.hpp>
#include <libgg/SequenceEditorLink.hpp>
#include <libgg/Todo.hpp>

#include <algorithm>
#include <chrono>
#include <future>
#include <sstream>

namespace ggtest {

namespace {

namespace todo = gg::todo;
using Rows = std::vector<std::string>;
using Env = std::vector<std::pair<std::string, std::optional<std::string>>>;

// c1 … c5 on main, each adding its own file; "part1" at c3.
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

// A git command running in the background while the test drives ggui (git waits for the list).
class BackgroundGit {
public:
    BackgroundGit(const fs::path& cwd, std::vector<std::string> args, Env env = {})
    {
        gg::RunRequest r;
        r.args.emplace_back("git");
        for (auto& a : args)
            r.args.push_back(std::move(a));
        r.cwd = cwd;
        r.env = std::move(env);
        r.cancel = m_cancel;
        m_future = std::async(std::launch::async, [r] { return gg::run(r); });
    }
    ~BackgroundGit()
    {
        if (m_future.valid()) {
            m_cancel.cancel(); // a failed scenario: do not leave git waiting
            m_future.wait();
        }
    }
    BackgroundGit(const BackgroundGit&) = delete;
    BackgroundGit& operator=(const BackgroundGit&) = delete;

    bool done() const { return m_future.valid() && m_future.wait_for(std::chrono::seconds(0)) == std::future_status::ready; }
    // Kills git and git-gg (Ctrl+C in the terminal).
    void interrupt() { m_cancel.cancel(); }
    // Waits (frames keep running) and returns the result.
    gg::RunResult finish(Scenario& s)
    {
        if (!s.waitUntil([&] { return done(); }, 60.0f))
            return {};
        return m_future.get();
    }

private:
    gg::CancelToken m_cancel;
    std::future<gg::RunResult> m_future;
};

std::string irRow(const std::string& key) { return "//Interactive rebase/**/###ir_" + key; }
std::string irWidget(const char* id) { return std::string("//Interactive rebase/###") + id; }
const char* kAbort = "//###Toolbar/Abort##tb_abort";
const char* kContinue = "//###Toolbar/Continue##tb_continue";
const char* kSettingsTabs = "//Settings/##settings_tabs/Git/##config_scope/";
const char* kOption = "Use ggui's todo editor for git rebase -i##sequence_editor";

ggui::RebasePanel& editor(Scenario& s) { return s.session()->rebase(); }

// The editor shows a list git waits for.
bool editorForGit(Scenario& s)
{
    const bool ok = s.waitUntil([&] { return editor(s).editingForGit() && editor(s).context() != nullptr; }, 30.0f);
    s.ctx->Yield(2);
    return ok;
}

// ggui (this process) is registered for the repository, so git gg finds it.
bool registered(Scenario& s, const fs::path& repo)
{
    const std::string key = gg::seqlink::gitDirKey(repo / ".git");
    return s.waitUntil([&] {
        for (const auto& i : gg::seqlink::instances())
            if (i.pid == ggui::processId() && i.gitDir == key)
                return true;
        return false;
    });
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
        } else if (item.action == todo::Action::Merge && !item.commit.empty()) {
            text += " " + item.arg;
        } else if (!item.arg.empty()) {
            text += " " + item.arg;
        }
        out.push_back(text);
    }
    return out;
}

void key(Scenario& s, const std::string& hex, ImGuiKeyChord chord)
{
    s.ctx->ItemClick(irRow(hex).c_str());
    s.ctx->Yield(2);
    s.ctx->KeyPress(chord);
    s.ctx->Yield(2);
}

std::vector<std::string> subjects(Scenario& s, const fs::path& repo, const std::string& rev)
{
    std::vector<std::string> out;
    std::vector<std::string> args{"log", "--format=%s"};
    std::istringstream words(rev);
    for (std::string w; words >> w;)
        args.push_back(w);
    for (const auto& l : gg::splitLines(s.gitOut(repo, args)))
        if (!l.empty())
            out.push_back(l.substr(0, l.find(' ')));
    return out;
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

std::string config(Scenario& s, const fs::path& repo, const char* flag, const char* key)
{
    return gg::trim(s.gitMayFail(repo, {"config", flag, "--get", key}).out);
}

// Settings ▸ Git ▸ <scope>; returns the tab's path.
std::string settingsScope(Scenario& s, const char* scope)
{
    if (!s.app.settingsOpen()) {
        s.ctx->MenuClick("//##MainMenuBar/Repository/Settings...");
        s.ctx->Yield(2);
    }
    s.ctx->ItemClick("//Settings/##settings_tabs/Git");
    const std::string tabs = kSettingsTabs;
    s.waitUntil([&] { return s.itemExists((tabs + scope).c_str()); });
    s.ctx->ItemClick((tabs + scope).c_str());
    s.ctx->Yield(2);
    return tabs + scope + "/";
}

// Shows the Git tab again so ggui reads the configuration plain git changed.
void rereadConfig(Scenario& s)
{
    s.ctx->ItemClick("//Settings/##settings_tabs/General");
    s.ctx->Yield(2);
    s.ctx->ItemClick("//Settings/##settings_tabs/Git");
    s.settle();
}

// A stand-in for the ggui program git gg starts (GG_GGUI): logs its arguments, then `body`.
fs::path gguiStub(Scenario& s, const std::string& name, const std::string& body)
{
    s.fakeTool(name, body);
    return s.toolPath(name);
}

bool disabled(Scenario& s, const char* ref) { return (s.ctx->ItemInfo(ref).ItemFlags & ImGuiItemFlags_Disabled) != 0; }

} // namespace

GG_TEST("sequence-editor", "Settings: ggui's todo editor for git rebase -i per scope; off removes only ggui's value; a user's own sequence.editor is kept unless replaced")
{
    const fs::path repo = s.fixture(Recipe::Linear);
    GG_REQUIRE(s.openRepository(repo));
    // Repository scope: on sets sequence.editor, off unsets it again.
    std::string tab = settingsScope(s, "Repository");
    GG_REQUIRE(s.waitUntil([&] { return s.itemExists((tab + kOption).c_str()); }));
    GG_CHECK(s.textShown("//Settings", "git rebase -i here uses: git's editor (sequence.editor is not set)"));
    ctx->ItemClick((tab + kOption).c_str());
    GG_CHECK(s.waitUntil([&] { return config(s, repo, "--local", "sequence.editor") == "git gg sequence-editor"; }));
    s.settle();
    GG_CHECK(s.textShown("//Settings", "git rebase -i here uses: ggui's todo editor  (Repository)"));
    ctx->ItemClick((tab + kOption).c_str());
    GG_CHECK(s.waitUntil([&] { return !s.gitMayFail(repo, {"config", "--local", "--get", "sequence.editor"}).ok(); }));
    s.settle();
    GG_CHECK(!s.gitMayFail(repo, {"config", "--local", "--get-regexp", "^(sequence|gg)\\."}).ok());

    // User scope: the repository inherits it.
    tab = settingsScope(s, "User");
    ctx->ItemClick((tab + kOption).c_str());
    GG_CHECK(s.waitUntil([&] { return config(s, repo, "--global", "sequence.editor") == "git gg sequence-editor"; }));
    s.settle();
    settingsScope(s, "Repository");
    GG_CHECK(s.textShown("//Settings", "git rebase -i here uses: ggui's todo editor  (User)"));
    tab = settingsScope(s, "User");
    ctx->ItemClick((tab + kOption).c_str());
    GG_CHECK(s.waitUntil([&] { return !s.gitMayFail(repo, {"config", "--global", "--get", "sequence.editor"}).ok(); }));
    s.settle();

    // The user's own sequence.editor: ggui asks before replacing it.
    s.git(repo, {"config", "--local", "sequence.editor", "my-editor --todo"});
    rereadConfig(s);
    tab = settingsScope(s, "Repository");
    GG_CHECK(s.textShown("//Settings", "git rebase -i here uses: 'my-editor --todo'  (Repository)"));
    ctx->ItemClick((tab + kOption).c_str());
    GG_REQUIRE(s.dialogOpen("Replace sequence.editor"));
    GG_CHECK(s.textShown("//$FOCUSED", "my-editor --todo"));
    s.dialogButton("Replace sequence.editor", "Cancel");
    s.settle();
    GG_CHECK_STR_EQ(config(s, repo, "--local", "sequence.editor"), "my-editor --todo");
    GG_CHECK(config(s, repo, "--local", "gg.previousSequenceEditor").empty());
    ctx->ItemClick((tab + kOption).c_str());
    GG_REQUIRE(s.dialogOpen("Replace sequence.editor"));
    s.dialogButton("Replace sequence.editor", "Replace");
    GG_CHECK(s.waitUntil([&] {
        return config(s, repo, "--local", "sequence.editor") == "git gg sequence-editor"
            && config(s, repo, "--local", "gg.previousSequenceEditor") == "my-editor --todo";
    }));
    s.settle();
    // Off puts the user's value back and forgets the copy.
    ctx->ItemClick((tab + kOption).c_str());
    GG_CHECK(s.waitUntil([&] {
        return config(s, repo, "--local", "sequence.editor") == "my-editor --todo"
            && config(s, repo, "--local", "gg.previousSequenceEditor").empty();
    }));
    s.settle();
    // The option shows off for the user's own value.
    GG_CHECK(!(ctx->ItemInfo((tab + kOption).c_str()).StatusFlags & ImGuiItemStatusFlags_Checked));
    GG_CHECK(s.fsck(repo));
}

GG_TEST("sequence-editor", "plain git rebase -i opens ggui's todo editor; Save hands the edited list back and git runs it; git rebase --edit-todo from a terminal")
{
    const Repo r = makeRepo(s);
    GG_REQUIRE(s.openRepository(r.path));
    // Turned on in Settings (Repository).
    const std::string tab = settingsScope(s, "Repository");
    GG_REQUIRE(s.waitUntil([&] { return s.itemExists((tab + kOption).c_str()); }));
    ctx->ItemClick((tab + kOption).c_str());
    GG_REQUIRE(s.waitUntil([&] { return config(s, r.path, "--local", "sequence.editor") == "git gg sequence-editor"; }));
    ctx->WindowClose("//Settings");
    s.settle();
    GG_REQUIRE(registered(s, r.path));
    // git gg must hand the list to this ggui, not start another one.
    const fs::path stub = gguiStub(s, "ggui-stub", "exit 1\n");
    const Env env{{"GG_GGUI", stub.string()}};

    BackgroundGit rebase(r.path, {"rebase", "-i", r.c[1]}, env);
    GG_REQUIRE(editorForGit(s));
    GG_CHECK(!rebase.done()); // git waits for the list
    GG_CHECK(rows(s) == (Rows{"pick c2", "pick c3", "pick c4", "pick c5"}));
    GG_CHECK(s.textShown("//Interactive rebase",
        "git rebase -i: Rebase 4 commit(s) of main onto " + s.session()->shortId(ggui::core::Oid::fromHex(r.c[1]))));
    GG_CHECK(s.textShown("//Interactive rebase", "Engine: git rebase (git rebase -i is waiting for this list)"));
    GG_CHECK(s.textShown("//Interactive rebase", "git opens its own editor for reword, squash and merge -c messages."));
    GG_CHECK(!s.itemExists(irWidget("ir_autosquash").c_str())); // git's options are git's
    // git's rebase has begun: the stop handling waits for the list.
    GG_CHECK(s.waitUntil([&] { return s.itemExists(kAbort); }));
    GG_CHECK(disabled(s, kAbort));
    GG_CHECK(disabled(s, kContinue));

    // Edit: drop c3, c5 before c4, reword c2 (git's editor keeps the message: no inline editor).
    key(s, r.c[3], ImGuiKey_D);
    key(s, r.c[5], ImGuiMod_Alt | ImGuiKey_UpArrow);
    key(s, r.c[2], ImGuiKey_R);
    GG_CHECK(rows(s) == (Rows{"reword c2", "drop c3", "pick c5", "pick c4"}));
    GG_CHECK(!s.itemExists(irRow("msg_" + r.c[2]).c_str()));
    // The preview works on git's fresh list.
    GG_CHECK(s.waitUntil([&] {
        const auto& p = editor(s).preview();
        return p && !editor(s).previewPending() && p->ok && p->rows.size() == 3;
    }));
    ctx->ItemClick(irWidget("ir_start").c_str());
    GG_CHECK(s.waitUntil([&] { return !editor(s).isOpen(); }));
    auto result = rebase.finish(s);
    GG_CHECK_EQ(result.exitCode, 0);
    s.settle();
    GG_CHECK(subjects(s, r.path, "main") == (std::vector<std::string>{"c4", "c5", "c2", "c1"}));
    GG_CHECK_STR_EQ(s.revParse(r.path, "part1"), r.c[3]);
    GG_CHECK(!fs::exists(r.path / ".git" / "rebase-merge"));
    GG_CHECK(!fs::exists(s.root() / "ggui-stub.log")); // no second ggui
    GG_CHECK(s.fsck(r.path));

    // git rebase --edit-todo from a terminal while a rebase is stopped: the remaining list.
    BackgroundGit second(r.path, {"rebase", "-i", "HEAD~3"}, env);
    GG_REQUIRE(editorForGit(s));
    const std::string c5 = s.revParse(r.path, "HEAD~1");
    key(s, s.revParse(r.path, "HEAD~2"), ImGuiKey_E); // stop at the first commit (c2)
    ctx->ItemClick(irWidget("ir_start").c_str());
    GG_CHECK_EQ(second.finish(s).exitCode, 0);
    GG_REQUIRE(fs::exists(r.path / ".git" / "rebase-merge" / "done"));
    const std::string before = s.read(r.path, ".git/rebase-merge/git-rebase-todo");
    {
        BackgroundGit edit(r.path, {"rebase", "--edit-todo"}, env);
        GG_REQUIRE(editorForGit(s));
        GG_CHECK(s.textShown("//Interactive rebase", "Remaining todo of the rebase of main: 2 commit(s)"));
        GG_CHECK(s.textShown("//Interactive rebase", "(git rebase --edit-todo is waiting for this list)"));
        GG_CHECK(rows(s) == (Rows{"pick c5", "pick c4"}));
        // Cancel: git keeps the list as it was.
        ctx->ItemClick(irWidget("ir_cancel").c_str());
        GG_CHECK_EQ(edit.finish(s).exitCode, 0);
        GG_CHECK_STR_EQ(s.read(r.path, ".git/rebase-merge/git-rebase-todo"), before);
    }
    {
        BackgroundGit edit(r.path, {"rebase", "--edit-todo"}, env);
        GG_REQUIRE(editorForGit(s));
        key(s, c5, ImGuiKey_D);
        ctx->ItemClick(irWidget("ir_start").c_str());
        GG_CHECK_EQ(edit.finish(s).exitCode, 0);
        GG_CHECK(s.read(r.path, ".git/rebase-merge/git-rebase-todo").rfind("drop " + c5, 0) == 0);
    }
    s.git(r.path, {"rebase", "--continue"});
    GG_CHECK(s.waitUntil([&] { return !fs::exists(r.path / ".git" / "rebase-merge"); }));
    s.settle();
    GG_CHECK(subjects(s, r.path, "main") == (std::vector<std::string>{"c4", "c2", "c1"}));
    GG_CHECK(s.fsck(r.path));
}

GG_TEST("sequence-editor", "Cancel, closing the editor and an interrupted git leave the repository as it was; git waits while another todo is open, and no other todo replaces git's")
{
    const Repo r = makeRepo(s);
    s.git(r.path, {"config", "sequence.editor", "git gg sequence-editor"});
    GG_REQUIRE(s.openRepository(r.path));
    GG_REQUIRE(registered(s, r.path));
    const Env env{{"GG_GGUI", gguiStub(s, "ggui-stub", "exit 1\n").string()}};
    const auto refsBefore = s.refs(r.path);
    auto unchanged = [&] {
        return !fs::exists(r.path / ".git" / "rebase-merge") && s.refs(r.path) == refsBefore && s.head(r.path) == r.c[5];
    };

    // Another todo is open in the editor: git waits (a notice says so) until it is closed.
    GG_REQUIRE(s.waitUntil([&] { return s.session()->history().row(ggui::core::Oid::fromHex(r.c[4])) != nullptr; }));
    ctx->ItemClick(("//History/**/###row_" + r.c[4]).c_str());
    ctx->KeyPress(ImGuiKey_I);
    GG_REQUIRE(s.waitUntil([&] { return editor(s).isOpen() && editor(s).context() && !editor(s).editingForGit(); }));
    {
        BackgroundGit rebase(r.path, {"rebase", "-i", r.c[1]}, env);
        GG_CHECK(toastWith(s, "git rebase -i is waiting"));
        GG_CHECK(!editor(s).editingForGit());
        ctx->ItemClick(irWidget("ir_cancel").c_str());
        GG_REQUIRE(editorForGit(s));
        GG_CHECK(rows(s) == (Rows{"pick c2", "pick c3", "pick c4", "pick c5"}));
        key(s, r.c[2], ImGuiKey_D);
        // Cancel: git gets an empty list and stops ("nothing to do").
        ctx->ItemClick(irWidget("ir_cancel").c_str());
        const auto result = rebase.finish(s);
        GG_CHECK_EQ(result.exitCode, 1);
        GG_CHECK(result.err.find("nothing to do") != std::string::npos);
        GG_CHECK(!editor(s).isOpen());
    }
    s.settle();
    GG_CHECK(unchanged());

    // Closing the editor's window cancels as well.
    {
        BackgroundGit rebase(r.path, {"rebase", "-i", r.c[2]}, env);
        GG_REQUIRE(editorForGit(s));
        key(s, r.c[3], ImGuiKey_D);
        // Another todo cannot replace the list git waits for.
        ctx->MenuClick("//##MainMenuBar/Commit/Interactive rebase...");
        GG_REQUIRE(s.dialogOpen("Interactive rebase onto"));
        s.dialogText("Interactive rebase onto", "base", r.c[1]);
        s.dialogButton("Interactive rebase onto", "Open");
        GG_CHECK(toastWith(s, "Todo editor in use"));
        GG_CHECK(editor(s).editingForGit());
        GG_CHECK(rows(s) == (Rows{"drop c3", "pick c4", "pick c5"}));
        ctx->WindowClose("//Interactive rebase");
        const auto result = rebase.finish(s);
        GG_CHECK_EQ(result.exitCode, 1);
        GG_CHECK(result.err.find("nothing to do") != std::string::npos);
    }
    s.settle();
    GG_CHECK(unchanged());

    // git interrupted while it waits (Ctrl+C): the editor closes with a notice.
    {
        BackgroundGit rebase(r.path, {"rebase", "-i", r.c[2]}, env);
        GG_REQUIRE(editorForGit(s));
        rebase.interrupt();
        GG_CHECK(s.waitUntil([&] { return !editor(s).isOpen(); }));
        GG_CHECK(toastWith(s, "git rebase -i stopped waiting"));
        rebase.finish(s);
    }
    // The killed git left its starting state behind; git's own cleanup removes it.
    s.gitMayFail(r.path, {"rebase", "--quit"});
    s.settle();
    GG_CHECK(unchanged());
    GG_CHECK(!fs::exists(s.root() / "ggui-stub.log"));
    GG_CHECK(s.fsck(r.path));
}

GG_TEST("sequence-editor", "git rebase -i --rebase-merges: git's list opens in merges mode with the preview; the edited list runs")
{
    // c0 ─ up: u1;  main: a1 ─ M (merges topic: t1 ─ t2) ─ e1, each commit its own file.
    const fs::path p = s.fixture(Recipe::Empty);
    auto commit = [&](const std::string& file, const std::string& message) {
        s.write(p, file, file + "\n");
        s.git(p, {"add", file});
        s.git(p, {"commit", "-q", "-m", message});
        return s.head(p);
    };
    commit("base.txt", "c0 base");
    s.git(p, {"switch", "-q", "-c", "up"});
    commit("u.txt", "u1 add u");
    s.git(p, {"switch", "-q", "main"});
    commit("a.txt", "a1 add a");
    s.git(p, {"switch", "-q", "-c", "topic"});
    const std::string t1 = commit("t1.txt", "t1 add t1");
    const std::string t2 = commit("t2.txt", "t2 add t2");
    s.git(p, {"switch", "-q", "main"});
    s.git(p, {"merge", "-q", "--no-ff", "topic", "-m", "Merge branch 'topic'"});
    const std::string m = s.head(p);
    commit("e.txt", "e1 add e");
    s.git(p, {"config", "sequence.editor", "git gg sequence-editor"});
    GG_REQUIRE(s.openRepository(p));
    GG_REQUIRE(registered(s, p));
    const Env env{{"GG_GGUI", gguiStub(s, "ggui-stub", "exit 1\n").string()}};

    BackgroundGit rebase(p, {"rebase", "-i", "--rebase-merges", "up"}, env);
    GG_REQUIRE(editorForGit(s));
    const Rows list = rows(s);
    GG_CHECK(std::find(list.begin(), list.end(), "label onto") != list.end());
    GG_CHECK(std::count_if(list.begin(), list.end(), [](const std::string& l) { return l.rfind("merge", 0) == 0; }) == 1);
    GG_CHECK(s.textShown("//Interactive rebase", "git rebase -i: Rebase 4 commit(s) and 1 merge(s) of main onto"));
    // Merges mode: the label/reset/merge tools are there.
    GG_CHECK(s.itemExists(irWidget("ir_insert_label").c_str()));
    GG_CHECK(s.itemExists(irRow("merge_" + m).c_str()));
    // The preview replays the merge.
    GG_CHECK(s.waitUntil([&] {
        const auto& pv = editor(s).preview();
        return pv && !editor(s).previewPending() && pv->ok
            && std::any_of(pv->rows.begin(), pv->rows.end(), [](const auto& row) { return row.parents.size() == 2; });
    }));
    // Drop t1 from the merged branch.
    key(s, t1, ImGuiKey_D);
    ctx->ItemClick(irWidget("ir_start").c_str());
    const auto result = rebase.finish(s);
    GG_CHECK_EQ(result.exitCode, 0);
    s.settle();
    // main: e1 on a merge of (a1 on up) and t2 (without t1), t2 on the new a1.
    GG_CHECK(subjects(s, p, "--first-parent main") == (std::vector<std::string>{"e1", "Merge", "a1", "u1", "c0"}));
    GG_CHECK_STR_EQ(s.gitOut(p, {"log", "-1", "--format=%s", "main~1^2"}), "t2 add t2");
    GG_CHECK_STR_EQ(s.revParse(p, "main~1^2~1"), s.revParse(p, "main~2"));
    GG_CHECK(s.gitMayFail(p, {"cat-file", "-e", "main:t1.txt"}).exitCode != 0);
    GG_CHECK(s.gitMayFail(p, {"cat-file", "-e", "main:t2.txt"}).ok());
    GG_CHECK(s.gitMayFail(p, {"cat-file", "-e", "main:u.txt"}).ok());
    GG_CHECK_STR_EQ(s.revParse(p, "topic"), t2); // no update-refs: the branch stays
    GG_CHECK(s.fsck(p));
}

GG_TEST("sequence-editor", "no ggui has the repository open: git gg starts ggui and waits for it; without a display git's editor; a ggui that exits early fails clearly")
{
    const Repo r = makeRepo(s);
    s.git(r.path, {"config", "sequence.editor", "git gg sequence-editor"});
    // Nothing is open in this ggui (the Welcome screen).
    GG_REQUIRE(s.waitUntil([&] { return !s.app.session(); }));

    // git gg starts "ggui <worktree>" (the stub records it and stays until told to quit); the
    // repository opened in a ggui then gets the list.
    const fs::path quit = s.root() / "quit-stub";
    const fs::path stub = gguiStub(s, "ggui-stub", "while [ ! -e '" + quit.string() + "' ]; do sleep 0.1; done\n");
    const fs::path log = s.root() / "ggui-stub.log";
    {
        BackgroundGit rebase(r.path, {"rebase", "-i", r.c[2]}, Env{{"GG_GGUI", stub.string()}, {"DISPLAY", ":97"}});
        GG_REQUIRE(s.waitUntil([&] { return fs::exists(log) && !gg::trim(s.read(s.root(), "ggui-stub.log")).empty(); }));
        std::error_code ec;
        GG_CHECK(fs::equivalent(fs::path(gg::trim(s.read(s.root(), "ggui-stub.log"))), r.path, ec));
        GG_CHECK(!rebase.done());
        GG_REQUIRE(s.openRepository(r.path));
        GG_REQUIRE(editorForGit(s));
        GG_CHECK(rows(s) == (Rows{"pick c3", "pick c4", "pick c5"}));
        key(s, r.c[4], ImGuiKey_D);
        ctx->ItemClick(irWidget("ir_start").c_str());
        GG_CHECK_EQ(rebase.finish(s).exitCode, 0);
        s.write(s.root(), "quit-stub", "");
    }
    s.settle();
    GG_CHECK(subjects(s, r.path, "main") == (std::vector<std::string>{"c5", "c3", "c2", "c1"}));
    GG_CHECK(s.fsck(r.path));
    s.app.closeRepository();
    GG_REQUIRE(s.waitUntil([&] { return !s.app.session(); }));

    // The started ggui exits before it shows the list: git stops, with git gg's reason.
    const std::string main = s.head(r.path);
    {
        const fs::path failing = gguiStub(s, "ggui-fails", "exit 3\n");
        BackgroundGit rebase(r.path, {"rebase", "-i", r.c[1]}, Env{{"GG_GGUI", failing.string()}, {"DISPLAY", ":97"}});
        const auto result = rebase.finish(s);
        GG_CHECK(result.exitCode != 0);
        GG_CHECK(result.err.find("ggui exited before it showed the todo list") != std::string::npos);
    }
    GG_CHECK(!fs::exists(r.path / ".git" / "rebase-merge"));
    GG_CHECK_STR_EQ(s.head(r.path), main);

    // No display: git's own editor (GIT_EDITOR) edits the list. (git runs an editor with sh: the
    // script itself, not its Windows launcher.) Windows always has a display.
    gguiStub(s, "drop-first", "sed -i '1s/^pick/drop/' \"$1\"\n");
    const fs::path gitEditor = s.root() / "fake-bin" / "drop-first";
#ifndef _WIN32
    {
        BackgroundGit rebase(r.path, {"rebase", "-i", r.c[1]},
            Env{{"GG_GGUI", stub.string()}, {"DISPLAY", std::nullopt}, {"WAYLAND_DISPLAY", std::nullopt},
                {"GIT_EDITOR", gitEditor.generic_string()}});
        const auto result = rebase.finish(s);
        GG_CHECK_EQ(result.exitCode, 0);
        GG_CHECK(result.err.find("no display to show ggui on; using git's editor") != std::string::npos);
    }
    GG_CHECK(subjects(s, r.path, "main") == (std::vector<std::string>{"c5", "c3", "c1"}));
#endif
    // A missing ggui program: git's editor as well.
    {
        BackgroundGit rebase(r.path, {"rebase", "-i", r.c[1]},
            Env{{"GG_GGUI", (s.root() / "no-such-ggui").generic_string()}, {"DISPLAY", ":97"}, {"GIT_EDITOR", gitEditor.generic_string()}});
        const auto result = rebase.finish(s);
        GG_CHECK_EQ(result.exitCode, 0);
        GG_CHECK(result.err.find("ggui was not found; using git's editor") != std::string::npos);
    }
#ifdef _WIN32
    GG_CHECK(subjects(s, r.path, "main") == (std::vector<std::string>{"c5", "c3", "c1"}));
#else
    GG_CHECK(subjects(s, r.path, "main") == (std::vector<std::string>{"c5", "c1"}));
#endif
    GG_CHECK(s.fsck(r.path));
}

} // namespace ggtest
