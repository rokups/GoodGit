// Interactive rebase with --rebase-merges (§4.13, P4-01): the Rebase merges option gives Git's
// starting list (label, reset and merge rows, checked against the list `git rebase -i
// --rebase-merges` makes), the rows are edited like the others (action combos for merge rows,
// fields for labels, keys l/t/m and the Insert buttons), validation of labels, the preview
// replays the list in memory (a graph with lanes), Start runs git rebase --rebase-merges (the
// same commits as git on a copy: with fixed dates the ids are equal), a merge that conflicts
// stops the rebase like any native stop, and a randomized differential against git.
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
#include <cstdlib>
#include <deque>
#include <fstream>
#include <map>
#include <random>
#include <sstream>

namespace ggtest {

namespace {

namespace todo = gg::todo;
using Rows = std::vector<std::string>;
using Preview = ggui::core::RebasePreview;
using A = todo::Action;
using F = todo::FixupMessage;

// c0 ─ up: u1 (changes p.txt)
//    └ main: a1 ─ b1 ─ b2 ─ M1 ─ d1 ─ M2 ─ e1 ─ e2 (fixup! a1)
//              │    └ side: s1 ─ s2 ┘ (merged by M2)
//              └ topic: t1 ─ t2 ─ t3 ┘ (merged by M1; "stack" at M1)
// Every "chain" changes its own file (a, t, x, b, s, d, e): keeping each chain's order keeps
// every pick and merge free of conflicts. The merges are made by "Mona Merger".
struct MergeRepo {
    fs::path path;
    std::string c0, u1, a1, t1, t2, t3, b1, b2, m1, s1, s2, d1, m2, e1, e2;
    std::map<std::string, std::string> chainOf; // commit → chain
};

MergeRepo makeMergeRepo(Scenario& s)
{
    MergeRepo r;
    r.path = s.fixture(Recipe::Empty);
    const fs::path& p = r.path;
    auto commit = [&](const std::string& chain, const std::string& file, const std::string& content, const std::string& message) {
        s.write(p, file, content);
        s.git(p, {"add", file});
        s.git(p, {"commit", "-q", "-m", message});
        const std::string id = s.head(p);
        if (!chain.empty())
            r.chainOf[id] = chain;
        return id;
    };
    auto merge = [&](const std::string& branch, const std::string& message) {
        s.git(p, {"-c", "user.name=Mona Merger", "-c", "user.email=mona@example.com", "merge", "-q", "--no-ff", branch, "-m", message});
        return s.head(p);
    };
    s.write(p, "p.txt", "p\n");
    s.git(p, {"add", "p.txt"});
    r.c0 = commit("", "base.txt", "base\n", "c0 base");
    s.git(p, {"switch", "-q", "-c", "up"});
    r.u1 = commit("", "p.txt", "p up\n", "u1 change p");
    s.git(p, {"switch", "-q", "main"});
    r.a1 = commit("a", "a.txt", "a1\n", "a1 add a");
    s.git(p, {"switch", "-q", "-c", "topic"});
    r.t1 = commit("t", "t.txt", "t1\n", "t1 add t");
    r.t2 = commit("t", "t.txt", "t1\nt2\n", "t2 change t");
    r.t3 = commit("x", "x.txt", "x3\n", "t3 add x");
    s.git(p, {"switch", "-q", "main"});
    r.b1 = commit("b", "b.txt", "b1\n", "b1 add b");
    r.b2 = commit("b", "b.txt", "b1\nb2\n", "b2 change b");
    r.m1 = merge("topic", "Merge branch 'topic'");
    s.git(p, {"branch", "stack"});
    s.git(p, {"switch", "-q", "-c", "side", r.b1});
    r.s1 = commit("s", "s.txt", "s1\n", "s1 add s");
    r.s2 = commit("s", "s.txt", "s1\ns2\n", "s2 change s");
    s.git(p, {"switch", "-q", "main"});
    r.d1 = commit("d", "d.txt", "d1\n", "d1 add d");
    r.m2 = merge("side", "Merge branch 'side' into main");
    r.e1 = commit("e", "e.txt", "e1\n", "e1 add e");
    r.e2 = commit("a", "a.txt", "a1\ne2\n", "fixup! a1 add a");
    return r;
}

std::string historyRow(const std::string& hex) { return "//History/**/###row_" + hex; }
std::string irRow(const std::string& key) { return "//Interactive rebase/**/###ir_" + key; }
std::string irAction(const std::string& key) { return "//Interactive rebase/**/###ir_action_" + key; }
std::string irField(const char* kind, size_t row) { return "//Interactive rebase/**/###ir_" + std::string(kind) + "_" + std::to_string(row); }
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
        if (item.fixup != F::None && !item.commit.empty())
            text += item.fixup == F::Use ? " -C" : " -c";
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

// The row's key in the editor (###ir_<key>, ###ir_action_<key>).
std::string rowKey(const todo::Item& item, size_t row)
{
    if (item.isCommit())
        return item.commit;
    if (item.action == A::Merge && !item.commit.empty())
        return "merge_" + item.commit;
    return "row_" + std::to_string(row);
}

size_t indexOfCommit(Scenario& s, const std::string& id)
{
    const auto& items = editor(s).todo().items;
    for (size_t i = 0; i < items.size(); ++i)
        if (items[i].commit == id)
            return i;
    return items.size();
}

bool hasIssue(Scenario& s, todo::Issue::Code code)
{
    const auto& issues = editor(s).issues();
    return std::any_of(issues.begin(), issues.end(), [&](const todo::Issue& i) { return i.code == code; });
}

const Preview* previewReady(Scenario& s, float seconds = 30.0f)
{
    auto& e = editor(s);
    const bool ok = s.waitUntil(
        [&] { return e.isOpen() && !e.previewPending() && e.preview() && e.previewTodo() == e.todo(); }, seconds);
    s.ctx->Yield(2);
    if (ok && !e.preview()->ok)
        s.ctx->LogError("preview: %s", e.preview()->error.c_str());
    return ok ? e.preview().get() : nullptr;
}

std::string previewPane(Scenario& s) { return s.child("//Interactive rebase", "##ir_preview"); }

fs::path copyRepo(Scenario& s, const fs::path& repo, const std::string& name)
{
    const fs::path copy = s.root() / name;
    fs::copy(repo, copy, fs::copy_options::recursive);
    s.track(copy);
    return copy;
}

// git with fixed author and committer dates (the same ones ggui's git gets from the environment
// during Start), so both runs make byte-identical commits; `editor` is GIT_EDITOR.
constexpr const char* kDate = "@1900000000 +0000";

gg::RunResult gitDated(const fs::path& cwd, const std::vector<std::string>& args, const std::string& editor,
    const std::vector<std::pair<std::string, std::string>>& extraEnv = {})
{
    gg::RunRequest r;
    r.args.emplace_back("git");
    r.args.insert(r.args.end(), args.begin(), args.end());
    r.cwd = cwd;
    r.env.emplace_back("GIT_AUTHOR_DATE", kDate);
    r.env.emplace_back("GIT_COMMITTER_DATE", kDate);
    r.env.emplace_back("GIT_EDITOR", editor);
    for (const auto& [k, v] : extraEnv)
        r.env.emplace_back(k, v);
    return gg::run(r);
}

void datesForGgui(bool on)
{
    if (on) {
        ggui::setEnv("GIT_AUTHOR_DATE", kDate);
        ggui::setEnv("GIT_COMMITTER_DATE", kDate);
    } else {
        ggui::unsetEnv("GIT_AUTHOR_DATE");
        ggui::unsetEnv("GIT_COMMITTER_DATE");
    }
}

fs::path script(Scenario& s, const std::string& name, const std::string& text)
{
    const fs::path path = s.root() / name;
    std::ofstream(path, std::ios::binary) << text;
    fs::permissions(path, fs::perms::owner_all, fs::perm_options::add);
    return path;
}

// The list `git rebase -i <args>` starts with, made on a throwaway copy (the sequence editor keeps
// it and fails, so git stops there).
todo::Todo gitStartingTodo(Scenario& s, const fs::path& pristine, const std::string& name, const std::vector<std::string>& args)
{
    const fs::path copy = copyRepo(s, pristine, name);
    const fs::path out = s.root() / (name + ".todo");
    const fs::path keep = script(s, "keep-todo.sh", "#!/bin/sh\ncp \"$1\" \"$GG_KEEP_TODO\"\nexit 1\n");
    std::vector<std::string> a{"-c", "sequence.editor=" + keep.generic_string(), "rebase", "-i"};
    a.insert(a.end(), args.begin(), args.end());
    gitDated(copy, a, "true", {{"GG_KEEP_TODO", out.string()}});
    std::ifstream in(out, std::ios::binary);
    std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    return todo::parse(text);
}

// The editor's list is git's (git writes abbreviated ids, ggui full ones; subjects are display only).
bool sameList(Scenario& s, const todo::Todo& ours, const todo::Todo& git)
{
    bool same = ours.items.size() == git.items.size();
    for (size_t i = 0; same && i < ours.items.size(); ++i) {
        const auto& o = ours.items[i];
        const auto& g = git.items[i];
        same = o.action == g.action && o.fixup == g.fixup && o.arg == g.arg && o.commit.size() >= g.commit.size()
            && o.commit.compare(0, g.commit.size(), g.commit) == 0;
    }
    if (!same) {
        s.ctx->LogError("ggui's list:\n%s", todo::format(ours).c_str());
        s.ctx->LogError("git's list:\n%s", todo::format(git).c_str());
    }
    return same;
}

// git 2.36 names the labels of a merge's parents after the merge's subject ("Octopus-x-y-z", "-2",
// ...), current git (and ggui) after the branches there. Renames git's labels to ours, row by row,
// so that only the label names are forgiven.
todo::Todo labelsAsOurs(const todo::Todo& ours, todo::Todo git)
{
    std::map<std::string, std::string> rename;
    for (size_t i = 0; i < std::min(ours.items.size(), git.items.size()); ++i)
        if (ours.items[i].action == A::Label && git.items[i].action == A::Label)
            rename[git.items[i].arg] = ours.items[i].arg;
    for (auto& item : git.items) {
        if (item.action != A::Label && item.action != A::Reset && item.action != A::Merge)
            continue;
        std::istringstream words(item.arg);
        std::string word, arg;
        while (words >> word)
            arg += (arg.empty() ? "" : " ") + (rename.count(word) ? rename[word] : word);
        item.arg = arg;
    }
    return git;
}

// git before 2.38 has no update-ref rows and no --update-refs / --no-update-refs: a scenario there
// runs with Update refs off (the rebased branch alone moves) and asks git for its list without the
// option. Returns whether it turned Update refs off.
bool updateRefsOff(Scenario& s)
{
    if (s.gitAtLeast(2, 38))
        return false;
    s.ctx->ItemUncheck(irWidget("ir_update_refs").c_str());
    s.ctx->Yield(2);
    return true;
}

// Git's reword editor (as in test_rebase_i.cpp): " reworded" after the first line that is neither a
// comment nor blank. The same text is typed into ggui's message editors.
std::string reworded(const std::string& text)
{
    std::string out;
    bool done = false;
    size_t pos = 0;
    while (pos < text.size()) {
        size_t end = text.find('\n', pos);
        const bool last = end == std::string::npos;
        std::string line = text.substr(pos, last ? std::string::npos : end - pos);
        if (!done && !line.empty() && line[0] != '#' && line.find_first_not_of(" \t") != std::string::npos) {
            line += " reworded";
            done = true;
        }
        out += line;
        if (!last)
            out += '\n';
        pos = last ? text.size() : end + 1;
    }
    return out;
}

const char* kRewordEditor = "#!/bin/sh\n"
                            "awk '!d && !/^#/ && NF { $0 = $0 \" reworded\"; d = 1 } { print }' \"$1\" > \"$1.tmp\" && mv \"$1.tmp\" \"$1\"\n";

// Types the reworded default into every message editor the list shows: reword rows, squash groups
// Git opens its editor for, and `merge -c` rows.
void rewordAll(Scenario& s)
{
    const auto list = editor(s).todo();
    auto type = [&](const std::string& commit, size_t row) {
        // (A message editor scrolled out of the list is not drawn: scroll the list until it is.)
        const std::string ref = "//Interactive rebase/**/###ir_msg_" + commit;
        if (ImGuiWindow* table = s.ctx->GetWindowByRef(s.child("//Interactive rebase", "##ir_table").c_str()))
            for (int i = 0; i <= 20 && !s.itemExists(ref.c_str()); ++i) {
                s.ctx->ScrollToY(table->ID, table->ScrollMax.y * static_cast<float>(i) / 20.0f);
                s.ctx->Yield(2);
            }
        GG_CHECK(s.waitUntil([&] { return s.itemExists(ref.c_str()); }));
        s.setText(ref, reworded(editor(s).messageText(row)));
    };
    for (const auto& g : todo::groups(list)) {
        const auto& item = list.items[g.first];
        if (item.action == A::Reword || g.needsEditor)
            type(item.commit, g.first);
    }
    for (size_t i = 0; i < list.items.size(); ++i)
        if (list.items[i].action == A::Merge && list.items[i].fixup == F::Edit && !list.items[i].commit.empty())
            type(list.items[i].commit, i);
}

void installRecordingHooks(Scenario& s, const fs::path& repo)
{
    s.write(repo, ".git/hooks/post-rewrite",
        "#!/bin/sh\nd=$(git rev-parse --git-dir)\necho \"== $1\" >> \"$d/post-rewrite.log\"\ncat >> \"$d/post-rewrite.log\"\n");
    fs::permissions(repo / ".git" / "hooks" / "post-rewrite", fs::perms::owner_all, fs::perm_options::add);
}

// Clicks Start and waits for the editor to close and ggui to settle.
bool start(Scenario& s)
{
    s.ctx->ItemClick(irWidget("ir_start").c_str());
    const bool closed = s.waitUntil([&] { return !editor(s).isOpen(); }, 60.0f);
    s.settle();
    return closed;
}

std::vector<std::string> parentsOf(Scenario& s, const fs::path& repo, const std::string& rev)
{
    auto fields = gg::splitLines(s.gitOut(repo, {"rev-list", "--parents", "-n", "1", rev}));
    std::vector<std::string> out;
    std::string line = fields.empty() ? std::string() : fields.front();
    size_t pos = line.find(' ');
    while (pos != std::string::npos) {
        const size_t next = line.find(' ', pos + 1);
        out.push_back(line.substr(pos + 1, next == std::string::npos ? std::string::npos : next - pos - 1));
        pos = next;
    }
    return out;
}

// The preview is the history `tip` now has: from the rows with branches, each previewed commit
// has the tree, subject and parents (in order) of the commit there; parents outside the result are
// the very same commits, unchanged rows keep their ids, and every previewed commit is reached.
void checkGraph(Scenario& s, const fs::path& repo, const std::string& tip, const Preview& p)
{
    std::map<std::string, size_t> byId;
    for (size_t k = 0; k < p.rows.size(); ++k)
        byId[p.rows[k].id] = k;
    std::map<std::string, std::string> actualOf;
    std::vector<std::pair<std::string, std::string>> stack;
    bool tipFound = false;
    for (const auto& row : p.rows)
        for (const auto& b : row.branches) {
            stack.emplace_back(row.id, s.revParse(repo, b));
            tipFound = tipFound || b == tip;
        }
    GG_CHECK(tipFound || std::find(p.ontoBranches.begin(), p.ontoBranches.end(), tip) != p.ontoBranches.end());
    while (!stack.empty()) {
        const auto [pid, aid] = stack.back();
        stack.pop_back();
        if (auto it = actualOf.find(pid); it != actualOf.end()) {
            GG_CHECK_STR_EQ(it->second, aid);
            continue;
        }
        actualOf[pid] = aid;
        const auto& row = p.rows[byId.at(pid)];
        GG_CHECK_STR_EQ(s.revParse(repo, aid + "^{tree}"), row.tree);
        GG_CHECK_STR_EQ(s.gitOut(repo, {"log", "-1", "--format=%s", aid}), row.subject);
        if (row.unchanged)
            GG_CHECK_STR_EQ(aid, row.id);
        const auto parents = parentsOf(s, repo, aid);
        GG_CHECK_EQ(parents.size(), row.parents.size());
        GG_CHECK_EQ(row.merge, parents.size() > 1);
        for (size_t k = 0; k < parents.size() && k < row.parents.size(); ++k) {
            if (byId.count(row.parents[k]))
                stack.emplace_back(row.parents[k], parents[k]);
            else
                GG_CHECK_STR_EQ(row.parents[k], parents[k]);
        }
    }
    GG_CHECK_EQ(actualOf.size(), p.rows.size());
}

// Local branches and their commits.
std::string branches(Scenario& s, const fs::path& repo)
{
    return s.gitOut(repo, {"for-each-ref", "--format=%(refname) %(objectname)", "refs/heads"});
}

void checkClean(Scenario& s, const fs::path& repo)
{
    GG_CHECK(s.statusPorcelain(repo).empty());
    for (const char* dir : {"rebase-merge", "rebase-apply", "sequencer"})
        GG_CHECK(!fs::exists(repo / ".git" / dir));
    GG_CHECK(s.gitOut(repo, {"for-each-ref", "refs/rewritten"}).empty());
}

// Moves the commit row `id` up to `target` with Alt+Up.
void moveUp(Scenario& s, const std::string& id, size_t target)
{
    size_t at = indexOfCommit(s, id);
    if (at == target)
        return;
    s.ctx->ItemClick(irRow(id).c_str());
    for (; at > target; --at)
        s.ctx->KeyPress(ImGuiMod_Alt | ImGuiKey_UpArrow);
    s.ctx->Yield(2);
}

bool stoppedAt(Scenario& s, const std::string& action)
{
    const bool ok = s.waitUntil([&] {
        const auto snap = s.session()->snapshot();
        return snap && snap->rebase && !snap->rebase->done.empty() && snap->rebase->done.back().action == action;
    });
    s.settle();
    return ok;
}

bool finished(Scenario& s, const fs::path& repo)
{
    const bool ok = s.waitUntil([&] {
        const auto snap = s.session()->snapshot();
        return !fs::exists(repo / ".git" / "rebase-merge") && snap && !snap->rebase && snap->state == ggui::core::RepoState::None;
    });
    s.settle();
    return ok;
}

const char* kAbort = "//##Toolbar/Abort##tb_abort";
const char* kProgress = "//##Toolbar/Progress##tb_rebase_progress";
const char* kCommitConflicts = "//##Toolbar/Commit with conflicts##tb_commit_conflicts";

} // namespace

GG_TEST("rebase-merges", "Rebase merges gives git's --rebase-merges list; label, reset and merge rows are edited, validated, previewed and run like git",
    "IR-OPT-REBASE-MERGES", "IR-ACT-LABEL", "IR-ACT-RESET", "IR-ACT-MERGE", "IR-ENGINE-NATIVE", "IR-ENGINE-SHOWN",
    "IR-PREVIEW", "IR-PREVIEW-BRANCHES", "IR-OPT-AUTOSQUASH", "IR-OPT-UPDATE-REFS", "IR-UNDO-REDO", "IR-NATIVE-UNDO")
{
    const MergeRepo r = makeMergeRepo(s);
    installRecordingHooks(s, r.path);
    const fs::path pristine = copyRepo(s, r.path, "pristine");
    const auto refsBefore = s.refs(r.path);
    GG_REQUIRE(s.openRepository(r.path));
    GG_REQUIRE(openFrom(s, r.a1));
    GG_CHECK(!editor(s).options().rebaseMerges);
    GG_CHECK(!s.itemExists(irWidget("ir_insert_label").c_str()));

    // ---- the option: git's list, with and without update-ref rows and autosquash ---------------
    // (git before 2.38 has neither update-ref rows nor --update-refs: Update refs stays off there,
    // git's list is asked for without the option, and git 2.36's label names are forgiven.)
    ctx->ItemCheck(irWidget("ir_rebase_merges").c_str());
    GG_REQUIRE(editor(s).options().rebaseMerges);
    const bool old = updateRefsOff(s);
    auto sameAsGit = [&](const char* name, std::vector<std::string> args) {
        if (!old)
            return sameList(s, editor(s).todo(), gitStartingTodo(s, pristine, name, args));
        std::erase_if(args, [](const std::string& a) { return a == "--update-refs" || a == "--no-update-refs"; });
        return sameList(s, editor(s).todo(), labelsAsOurs(editor(s).todo(), gitStartingTodo(s, pristine, name, args)));
    };
    GG_CHECK(sameAsGit("start-1", {"--rebase-merges", "--update-refs", r.c0}));
    {
        std::string list;
        for (const auto& row : rows(s))
            list += row + "; ";
        ctx->LogInfo("rebase-merges list: %s", list.c_str());
    }
    GG_CHECK(editor(s).engine().engine == todo::Engine::Native);
    GG_CHECK(s.textShown("//Interactive rebase", "row 1 is label: --rebase-merges lists are replayed by git rebase"));
    GG_CHECK(s.textShown("//Interactive rebase", "and 2 merge(s)"));
    ctx->ItemUncheck(irWidget("ir_update_refs").c_str());
    GG_CHECK(sameAsGit("start-2", {"--rebase-merges", "--no-update-refs", r.c0}));
    ctx->ItemCheck(irWidget("ir_autosquash").c_str());
    GG_CHECK(sameAsGit("start-3", {"--rebase-merges", "--no-update-refs", "--autosquash", r.c0}));
    ctx->ItemUncheck(irWidget("ir_autosquash").c_str());
    if (!old)
        ctx->ItemCheck(irWidget("ir_update_refs").c_str());
    GG_CHECK(sameAsGit("start-4", {"--rebase-merges", "--update-refs", r.c0}));
    // Off: the straight list again; Ctrl+Z brings the merges back.
    ctx->ItemUncheck(irWidget("ir_rebase_merges").c_str());
    GG_CHECK(!todo::hasMergeRows(editor(s).todo()) && editor(s).engine().engine == todo::Engine::InMemory);
    ctx->ItemClick(irRow(r.a1).c_str());
    ctx->KeyPress(ImGuiMod_Ctrl | ImGuiKey_Z);
    ctx->Yield(2);
    GG_REQUIRE(editor(s).options().rebaseMerges && todo::hasMergeRows(editor(s).todo()));

    // ---- the preview: every commit and merge kept as it is, in lanes -------------------------
    {
        const Preview* p = previewReady(s);
        GG_REQUIRE(p && p->ok);
        GG_CHECK_EQ(p->rows.size(), 13u); // 11 commits, 2 merges
        for (const auto& row : p->rows)
            GG_CHECK(row.unchanged);
        const auto merges = std::count_if(p->rows.begin(), p->rows.end(), [](const Preview::Row& x) { return x.merge; });
        GG_CHECK_EQ(merges, 2);
        GG_CHECK(s.textShown(previewPane(s).c_str(), "13 commit(s)"));
        // Clicking a merge in the preview selects its merge row.
        size_t k = 0;
        while (k < p->rows.size() && !(p->rows[k].merge && p->rows[k].id == r.m1))
            ++k;
        GG_REQUIRE(k < p->rows.size());
        ctx->ItemClick((previewPane(s) + "/**/###irp_row_" + std::to_string(k)).c_str());
        ctx->Yield(2);
        GG_CHECK(editor(s).selection() == std::set<size_t>{indexOfCommit(s, r.m1)});
    }

    // ---- keys l, t, m insert label, reset and merge rows after the selection; Delete and undo ----
    const size_t b2Row = indexOfCommit(s, r.b2);
    ctx->ItemClick(irRow(r.b2).c_str());
    ctx->KeyPress(ImGuiKey_L);
    ctx->KeyPress(ImGuiKey_T);
    ctx->KeyPress(ImGuiKey_M);
    ctx->Yield(2);
    {
        const auto& items = editor(s).todo().items;
        GG_REQUIRE(items.size() > b2Row + 3);
        GG_CHECK(items[b2Row + 1].action == A::Label && items[b2Row + 1].arg == "label");
        GG_CHECK(items[b2Row + 2].action == A::Reset && items[b2Row + 2].arg == "onto");
        GG_CHECK(items[b2Row + 3].action == A::Merge && items[b2Row + 3].arg == "label" && items[b2Row + 3].commit.empty());
    }
    GG_CHECK(!todo::hasErrors(editor(s).issues()));
    // The plain merge gets Git's message, and the preview makes that merge onto the new base.
    {
        const Preview* p = previewReady(s);
        GG_REQUIRE(p && p->ok);
        const auto it = std::find_if(p->rows.begin(), p->rows.end(), [&](const Preview::Row& x) { return x.todoRow == b2Row + 3; });
        GG_REQUIRE(it != p->rows.end());
        GG_CHECK(it->merge && it->subject == "Merge branch 'label'" && it->parents.size() == 2 && it->parents[0] == r.c0);
    }
    // Delete removes the selected (inserted) merge row.
    ctx->KeyPress(ImGuiKey_Delete);
    ctx->Yield(2);
    GG_CHECK(editor(s).todo().items[b2Row + 3].commit == r.m1); // git's own row after it: merge -C topic
    for (int i = 0; i < 4; ++i)
        ctx->KeyPress(ImGuiMod_Ctrl | ImGuiKey_Z);
    ctx->Yield(2);
    GG_CHECK(editor(s).todo().items[b2Row + 1].commit == r.m1); // back to git's list (b2, merge -C topic)
    // The Insert buttons do the same with the mouse.
    ctx->ItemClick(irRow(r.b2).c_str());
    ctx->ItemClick(irWidget("ir_insert_label").c_str());
    ctx->ItemClick(irWidget("ir_insert_reset").c_str());
    ctx->ItemClick(irWidget("ir_insert_merge").c_str());
    ctx->Yield(2);
    {
        const auto& items = editor(s).todo().items;
        GG_CHECK(items[b2Row + 1].action == A::Label && items[b2Row + 2].action == A::Reset && items[b2Row + 3].action == A::Merge);
    }

    // ---- the fields: validation of label names and their use -----------------------------------
    s.setText(irField("label", b2Row + 1), "not valid");
    ctx->Yield(2);
    GG_CHECK(hasIssue(s, todo::Issue::Code::BadLabel));
    GG_CHECK(s.textShown("//Interactive rebase", "'not valid' is not a valid label"));
    s.setText(irField("label", b2Row + 1), "mine");
    s.setText(irField("reset", b2Row + 2), "nowhere");
    ctx->Yield(2);
    GG_CHECK(s.textShown("//Interactive rebase", "unknown label 'nowhere'"));
    GG_CHECK(!editor(s).canStart());
    s.setText(irField("reset", b2Row + 2), "branch-point");
    // Row 2 ("reset onto") going to "topic", which a row further down defines (row 9; one less
    // without update-ref rows).
    size_t topicRow = 0;
    for (size_t i = 0; i < editor(s).todo().items.size(); ++i)
        if (editor(s).todo().items[i].action == A::Label && editor(s).todo().items[i].arg == "topic")
            topicRow = i + 1;
    GG_CHECK_EQ(topicRow, old ? 8u : 9u);
    s.setText(irField("reset", 1), "topic");
    ctx->Yield(2);
    GG_CHECK(hasIssue(s, todo::Issue::Code::LabelDefinedLater));
    GG_CHECK(s.textShown("//Interactive rebase", "label 'topic' is used before row " + std::to_string(topicRow) + " defines it"));
    s.setText(irField("reset", 1), "onto");
    s.setText(irField("merge", b2Row + 3), "mine");
    ctx->Yield(2);
    GG_CHECK(!todo::hasErrors(editor(s).issues()));
    GG_CHECK(editor(s).todo().items[b2Row + 3].arg == "mine");
    s.setText(irField("merge", b2Row + 3), "");
    ctx->Yield(2);
    GG_CHECK(s.textShown("//Interactive rebase", "merge needs a label to merge"));
    // Off and on again: git's list.
    ctx->ItemUncheck(irWidget("ir_rebase_merges").c_str());
    ctx->ItemCheck(irWidget("ir_rebase_merges").c_str());
    GG_CHECK(sameAsGit("start-5", {"--rebase-merges", "--update-refs", r.c0}));

    // ---- merge rows' action combo: -C, -c (edit the message), merge (Git's message) ------------
    s.comboSelect(irAction("merge_" + r.m1).c_str(), "merge -c");
    const size_t m1Row = indexOfCommit(s, r.m1);
    GG_CHECK(editor(s).todo().items[m1Row].fixup == F::Edit);
    GG_CHECK_STR_EQ(editor(s).messageText(m1Row), "Merge branch 'topic'\n");
    s.setText("//Interactive rebase/**/###ir_msg_" + r.m1, "Merge topic, retold\n");
    s.comboSelect(irAction("merge_" + r.m2).c_str(), "merge");
    GG_CHECK(editor(s).todo().items[indexOfCommit(s, r.m2)].fixup == F::None);
    GG_CHECK(editor(s).todo().items[indexOfCommit(s, r.m2)].arg == "side");
    s.comboSelect(irAction("merge_" + r.m2).c_str(), "merge -C");
    s.comboSelect(irAction("merge_" + r.m2).c_str(), "merge");

    // A fixup right after a reset row amends whatever the reset went to: no preview for that (not
    // an error; git rebase runs it).
    {
        const size_t b1Row = indexOfCommit(s, r.b1);
        GG_REQUIRE(editor(s).todo().items[b1Row - 1].action == A::Reset);
        ctx->ItemClick(irRow(r.b1).c_str());
        ctx->KeyPress(ImGuiKey_F);
        ctx->Yield(2);
        GG_CHECK(!todo::hasErrors(editor(s).issues()));
        GG_CHECK(s.waitUntil([&] { return !editor(s).previewPending() && editor(s).preview() && editor(s).previewTodo() == editor(s).todo(); }));
        GG_REQUIRE(editor(s).preview() != nullptr);
        GG_CHECK(!editor(s).preview()->ok && editor(s).preview()->unsupported);
        GG_CHECK(editor(s).preview()->error.find("is fixup right after a reset or merge row") != std::string::npos);
        GG_CHECK(s.itemExists((previewPane(s) + "/###irp_unsupported").c_str()));
        ctx->KeyPress(ImGuiKey_P);
        ctx->Yield(2);
        GG_CHECK(editor(s).todo().items[b1Row].action == A::Pick);
    }

    // ---- a changed list: t3 first on topic, b2 dropped, M1 retold, M2 a new merge -------------
    moveUp(s, r.t3, indexOfCommit(s, r.t1));
    ctx->ItemClick(irRow(r.b2).c_str());
    ctx->KeyPress(ImGuiKey_D);
    ctx->Yield(2);
    GG_CHECK(!todo::hasErrors(editor(s).issues()));
    GG_CHECK(editor(s).todo().items[indexOfCommit(s, r.m1)].message == std::optional<std::string>("Merge topic, retold\n"));
    const Preview* p = previewReady(s);
    GG_REQUIRE(p && p->ok);
    const Preview expected = *p;
    GG_CHECK_EQ(expected.rows.size(), 12u);
    const std::string todoText = todo::format(editor(s).todo());
    ctx->LogInfo("list:\n%s", todoText.c_str());

    datesForGgui(true);
    GG_REQUIRE(start(s));
    datesForGgui(false);
    checkClean(s, r.path);
    checkGraph(s, r.path, "main", expected);
    // git on a copy with the same list: the same commits (and ids), the same post-rewrite.
    const fs::path copy = copyRepo(s, pristine, "git-copy");
    const fs::path todoFile = s.root() / "list.todo";
    std::ofstream(todoFile, std::ios::binary) << todoText;
    const fs::path retell = script(s, "retell.sh",
        "#!/bin/sh\nif head -n 1 \"$1\" | grep -q \"^Merge branch 'topic'$\"; then printf 'Merge topic, retold\\n' > \"$1\"; fi\n");
    const auto copyRun = gitDated(copy, {"-c", "sequence.editor=cp '" + todoFile.generic_string() + "'", "rebase", "-q", "-i", "--rebase-merges",
                                        s.gitAtLeast(2, 45) ? "--empty=stop" : "--empty=ask", r.c0},
        retell.generic_string());
    GG_CHECK(copyRun.ok());
    GG_CHECK_STR_EQ(branches(s, r.path), branches(s, copy));
    GG_CHECK_STR_EQ(s.read(r.path, ".git/post-rewrite.log"), s.read(copy, ".git/post-rewrite.log"));
    // M1 again (Mona's, retold), M2 a new merge by the current user with Git's message.
    if (!old) // (the update-ref row moved "stack" to the new M1)
        GG_CHECK_STR_EQ(s.gitOut(r.path, {"log", "-1", "--format=%an|%s", "stack"}), "Mona Merger|Merge topic, retold");
    GG_CHECK_STR_EQ(s.gitOut(r.path, {"log", "-1", "--format=%s", "main~2"}), "Merge branch 'side'");
    GG_CHECK(s.gitOut(r.path, {"log", "-1", "--format=%an", "main~2"}) != "Mona Merger");
    GG_CHECK(s.gitOut(r.path, {"log", "--format=%s", "main"}).find("b2 change b") == std::string::npos);

    // One Undo restores every ref.
    ctx->ItemClick("//History/**/###row_wt");
    ctx->KeyPress(ImGuiMod_Ctrl | ImGuiKey_Z);
    GG_CHECK(s.waitUntil([&] { return s.refs(r.path) == refsBefore; }));
    s.settle();
    GG_CHECK(s.statusPorcelain(r.path).empty());
}

GG_TEST("rebase-merges", "a merge that conflicts stops git rebase --rebase-merges: the preview shows it, Abort, then Commit with conflicts",
    "IR-ACT-MERGE", "IR-NATIVE-STOP-CONFLICT", "IR-PREVIEW-CONFLICTS", "IR-OPT-ONTO", "IR-NATIVE-UNDO")
{
    // topic and main both change f.txt; the merge M kept main's line. Onto `up`, which changes
    // f.txt's other line, the merge has to be made again and conflicts.
    const fs::path path = s.fixture(Recipe::Empty);
    s.commitFile(path, "f.txt", "1\n2\n3\n4\n5\n", "c0 add f");
    s.git(path, {"switch", "-q", "-c", "up"});
    s.commitFile(path, "u.txt", "u\n", "u1 add u");
    s.git(path, {"switch", "-q", "main"});
    s.commitFile(path, "a.txt", "a\n", "a1 add a");
    const std::string a1 = s.head(path);
    s.git(path, {"switch", "-q", "-c", "topic"});
    s.commitFile(path, "f.txt", "1\ntopic\n3\n4\n5\n", "t1 topic line");
    s.git(path, {"switch", "-q", "main"});
    s.commitFile(path, "f.txt", "1\nmain\n3\n4\n5\n", "b1 main line");
    s.gitMayFail(path, {"merge", "-q", "--no-ff", "topic", "-m", "Merge branch 'topic'"});
    s.write(path, "f.txt", "1\nmain\n3\n4\n5\n");
    s.git(path, {"add", "f.txt"});
    s.git(path, {"commit", "-q", "--no-edit"});
    GG_REQUIRE(parentsOf(s, path, "HEAD").size() == 2u);
    const auto refsBefore = s.refs(path);
    GG_REQUIRE(s.openRepository(path));

    auto open = [&] {
        GG_REQUIRE(openFrom(s, a1));
        ctx->ItemCheck(irWidget("ir_rebase_merges").c_str());
        ctx->ItemClick(irWidget("ir_onto").c_str());
        ctx->KeyChars("up");
        ctx->KeyPress(ImGuiKey_Enter);
        GG_REQUIRE(s.waitUntil([&] { return editor(s).context() && editor(s).context()->onto == s.revParse(path, "up"); }));
        GG_CHECK(rows(s)
            == (Rows{"label onto", "reset onto", "pick a1", "label branch-point", "pick t1", "update-ref refs/heads/topic", "label topic",
                "reset branch-point", "pick b1", "merge -C topic"}));
    };
    open();
    updateRefsOff(s);
    // The preview makes the merge with a first-class conflict in f.txt.
    {
        const Preview* p = previewReady(s);
        GG_REQUIRE(p && p->ok && !p->rows.empty());
        const auto& merge = p->rows.back();
        GG_CHECK(merge.merge && merge.subject == "Merge branch 'topic'");
        GG_REQUIRE(merge.conflicts.size() == 1u);
        GG_CHECK_STR_EQ(merge.conflicts[0].first, "f.txt");
        GG_CHECK(s.textShown(previewPane(s).c_str(), "1 with conflicts"));
    }
    GG_REQUIRE(start(s));
    GG_REQUIRE(stoppedAt(s, "merge -C"));
    GG_CHECK(fs::exists(path / ".git" / "MERGE_HEAD"));
    ctx->ItemClick(kProgress);
    ctx->Yield(2);
    const auto lines = s.drawnText("//$FOCUSED");
    ctx->KeyPress(ImGuiKey_Escape);
    ctx->Yield(2);
    auto shown = [&](std::string text) { // (drawn text has no spaces)
        std::erase(text, ' ');
        return std::any_of(lines.begin(), lines.end(), [&](const std::string& l) { return l.find(text) != std::string::npos; });
    };
    GG_CHECK(shown("merge -C"));
    GG_CHECK(shown("Conflicts: resolve them"));
    // Abort: everything as it was.
    ctx->ItemClick(kAbort);
    GG_REQUIRE(finished(s, path));
    GG_CHECK(s.refs(path) == refsBefore);
    GG_CHECK(s.statusPorcelain(path).empty());

    // Again, and Commit with conflicts: git makes the merge with the regions, and the rebase ends.
    open();
    updateRefsOff(s);
    GG_REQUIRE(start(s));
    GG_REQUIRE(stoppedAt(s, "merge -C"));
    GG_REQUIRE(s.waitUntil([&] { return s.itemExists(kCommitConflicts); }));
    ctx->ItemClick(kCommitConflicts);
    GG_REQUIRE(finished(s, path));
    const auto parents = parentsOf(s, path, "main");
    GG_REQUIRE(parents.size() == 2u);
    GG_CHECK_STR_EQ(s.gitOut(path, {"log", "-1", "--format=%s", "main^2"}), "t1 topic line");
    GG_CHECK_STR_EQ(s.gitOut(path, {"log", "-1", "--format=%s", "main"}), "Merge branch 'topic'");
    GG_CHECK(s.gitOut(path, {"show", "main:f.txt"}).find("<<<<<<<") != std::string::npos);
    GG_CHECK(s.gitOut(path, {"merge-base", "--is-ancestor", "up", "main"}).empty());
    checkClean(s, path);
    // One Undo restores every ref.
    ctx->ItemClick("//History/**/###row_wt");
    ctx->KeyPress(ImGuiMod_Ctrl | ImGuiKey_Z);
    GG_CHECK(s.waitUntil([&] { return s.refs(path) == refsBefore; }));
    s.settle();
    GG_CHECK_STR_EQ(s.statusPorcelain(path), "");
}

GG_TEST("rebase-merges", "randomized differential: --rebase-merges lists vs git rebase -i --rebase-merges on a copy",
    "IR-DIFFERENTIAL", "IR-OPT-REBASE-MERGES", "IR-ACT-LABEL", "IR-ACT-RESET", "IR-ACT-MERGE", "IR-ACT-PICK", "IR-ACT-REWORD",
    "IR-ACT-SQUASH", "IR-ACT-FIXUP", "IR-ACT-FIXUP-C", "IR-ACT-DROP", "IR-PREVIEW", "HOOK-REWRITE-RUN")
{
    std::uint64_t seed = 0x4b01ee55ULL;
    if (const char* env = std::getenv("GGUI_IR_SEED"))
        seed = std::strtoull(env, nullptr, 0);
    int rounds = 5;
    if (const char* env = std::getenv("GGUI_IR_ROUNDS"))
        rounds = std::atoi(env);
    ctx->LogInfo("rebase-merges differential seed 0x%llx, %d rounds", static_cast<unsigned long long>(seed), rounds);
    std::mt19937_64 rng(seed);
    auto chance = [&](double p) { return std::uniform_real_distribution<double>(0, 1)(rng) < p; };
    auto below = [&](size_t n) { return static_cast<size_t>(std::uniform_int_distribution<size_t>(0, n - 1)(rng)); };

    const MergeRepo r = makeMergeRepo(s);
    installRecordingHooks(s, r.path);
    const fs::path pristine = copyRepo(s, r.path, "pristine");
    const auto refsBefore = s.refs(r.path);
    const fs::path rewordEditor = script(s, "reword-editor.sh", kRewordEditor);
    GG_REQUIRE(s.openRepository(r.path));

    for (int round = 0; round < rounds; ++round) {
        const bool ontoUp = chance(0.5);
        const bool updateRefs = chance(0.7) && s.gitAtLeast(2, 38); // (update-ref rows: git 2.38)
        GG_REQUIRE(openFrom(s, r.a1));
        if (ontoUp) {
            ctx->ItemClick(irWidget("ir_onto").c_str());
            ctx->KeyChars("up");
            ctx->KeyPress(ImGuiKey_Enter);
            GG_REQUIRE(s.waitUntil([&] { return editor(s).context() && editor(s).context()->onto == r.u1; }));
        }
        if (!updateRefs)
            ctx->ItemUncheck(irWidget("ir_update_refs").c_str());
        ctx->ItemCheck(irWidget("ir_rebase_merges").c_str());
        s.comboSelect(irWidget("ir_empty").c_str(), "Keep");
        std::vector<std::string> startArgs{"--rebase-merges"};
        if (s.gitAtLeast(2, 38))
            startArgs.push_back(updateRefs ? "--update-refs" : "--no-update-refs");
        if (ontoUp) {
            startArgs.push_back("--onto");
            startArgs.push_back("up");
        }
        startArgs.push_back(r.c0);
        GG_CHECK(sameList(s, editor(s).todo(), gitStartingTodo(s, pristine, "start-" + std::to_string(round), startArgs)));

        // ---- a random list: each run of commit rows (between label/reset/merge/update-ref rows)
        // is shuffled keeping every chain's order; chain tails are dropped; actions and merge -c ---
        std::vector<todo::Item> want = editor(s).todo().items;
        std::set<std::string> dropped;
        {
            std::map<std::string, std::vector<std::string>> chains;
            for (const auto& item : want)
                if (item.isCommit())
                    chains[r.chainOf.at(item.commit)].push_back(item.commit);
            for (const auto& [name, list] : chains)
                if (chance(0.3))
                    for (size_t k = below(list.size()); k < list.size(); ++k)
                        dropped.insert(list[k]);
        }
        for (size_t i = 0; i < want.size();) {
            if (!want[i].isCommit()) {
                ++i;
                continue;
            }
            size_t end = i;
            while (end < want.size() && want[end].isCommit())
                ++end;
            std::map<std::string, std::deque<todo::Item>> byChain;
            for (size_t k = i; k < end; ++k)
                byChain[r.chainOf.at(want[k].commit)].push_back(want[k]);
            for (size_t k = i; k < end; ++k) {
                std::vector<std::string> open;
                for (const auto& [name, list] : byChain)
                    if (!list.empty())
                        open.push_back(name);
                auto& list = byChain[open[below(open.size())]];
                want[k] = list.front();
                list.pop_front();
            }
            bool first = true;
            for (size_t k = i; k < end; ++k) {
                auto& item = want[k];
                if (dropped.count(item.commit)) {
                    item.action = A::Drop;
                    continue;
                }
                if (!first && chance(0.4)) {
                    const size_t c = below(6);
                    item.action = c == 0 ? A::Pick : c == 1 ? A::Reword : c == 2 ? A::Squash : A::Fixup;
                    item.fixup = c == 4 ? F::Use : c == 5 ? F::Edit : F::None;
                } else if (first && chance(0.2)) {
                    item.action = A::Reword;
                }
                first = false;
            }
            // Git asks twice for a reword with squash rows after it; ggui has one editor per group.
            for (size_t k = i; k < end; ++k) {
                if (want[k].action != A::Reword)
                    continue;
                size_t j = k + 1;
                while (j < end && want[j].action == A::Drop)
                    ++j;
                if (j < end && (want[j].action == A::Squash || want[j].action == A::Fixup))
                    want[k].action = A::Pick;
            }
            i = end;
        }
        for (auto& item : want)
            if (item.action == A::Merge && chance(0.4))
                item.fixup = F::Edit;
        std::string wantText;
        for (const auto& item : want)
            wantText += std::string(todo::actionName(item.action)) + (item.fixup == F::Use ? " -C" : item.fixup == F::Edit ? " -c" : "")
                + " " + (item.commit.empty() ? item.arg : item.commit.substr(0, 7)) + "; ";
        ctx->LogInfo("round %d: onto %s, update-refs %d: %s", round, ontoUp ? "up" : "c0", updateRefs, wantText.c_str());

        // ---- entered like a user: Alt+Up into place, then the action combos, then the messages ---
        for (size_t target = 0; target < want.size(); ++target)
            if (want[target].isCommit()) {
                moveUp(s, want[target].commit, target);
                GG_REQUIRE(indexOfCommit(s, want[target].commit) == target);
            }
        for (size_t i = 0; i < want.size(); ++i) {
            const auto& item = want[i];
            const auto& now = editor(s).todo().items[i];
            if (item.commit.empty() || (now.action == item.action && now.fixup == item.fixup))
                continue;
            const std::string label = std::string(todo::actionName(item.action))
                + (item.fixup == F::Use ? " -C" : item.fixup == F::Edit ? " -c" : "");
            s.comboSelect(irAction(rowKey(item, i)).c_str(), label.c_str());
        }
        {
            const auto& items = editor(s).todo().items;
            GG_REQUIRE(items.size() == want.size());
            for (size_t i = 0; i < want.size(); ++i)
                GG_CHECK(items[i].action == want[i].action && items[i].fixup == want[i].fixup && items[i].commit == want[i].commit
                    && items[i].arg == want[i].arg);
        }
        GG_CHECK(!todo::hasErrors(editor(s).issues()));
        rewordAll(s);
        const std::string todoText = todo::format(editor(s).todo());
        const Preview* p = previewReady(s);
        GG_REQUIRE(p && p->ok);
        const Preview expected = *p;

        // ---- Start, then the same list with git on a copy (same dates: the same ids) ----------
        const fs::path copy = copyRepo(s, pristine, "git-" + std::to_string(round));
        fs::remove(r.path / ".git" / "post-rewrite.log");
        datesForGgui(true);
        const bool started = start(s);
        datesForGgui(false);
        GG_REQUIRE(started);
        const fs::path todoFile = s.root() / ("list-" + std::to_string(round) + ".todo");
        std::ofstream(todoFile, std::ios::binary) << todoText;
        std::vector<std::string> args{"-c", "sequence.editor=cp '" + todoFile.generic_string() + "'", "rebase", "-q", "-i", "--rebase-merges",
            "--empty=keep"};
        if (ontoUp) {
            args.push_back("--onto");
            args.push_back("up");
        }
        args.push_back(r.c0);
        const auto copyRun = gitDated(copy, args, rewordEditor.generic_string());
        GG_CHECK(copyRun.ok());

        checkClean(s, r.path);
        GG_CHECK_STR_EQ(branches(s, r.path), branches(s, copy));
        GG_CHECK_STR_EQ(s.revParse(r.path, "HEAD"), s.revParse(copy, "HEAD"));
        GG_CHECK_STR_EQ(s.gitOut(r.path, {"symbolic-ref", "HEAD"}), "refs/heads/main");
        const std::string ours = fs::exists(r.path / ".git" / "post-rewrite.log") ? s.read(r.path, ".git/post-rewrite.log") : "";
        const std::string theirs = fs::exists(copy / ".git" / "post-rewrite.log") ? s.read(copy, ".git/post-rewrite.log") : "";
        GG_CHECK_STR_EQ(ours, theirs);
        checkGraph(s, r.path, "main", expected);
        if (ctx->IsError()) {
            ctx->LogError("rebase-merges differential round %d failed (seed 0x%llx): %s\n%s", round,
                static_cast<unsigned long long>(seed), wantText.c_str(), todoText.c_str());
            return;
        }

        // One Undo restores every ref.
        if (s.refs(r.path) != refsBefore) {
            ctx->ItemClick("//History/**/###row_wt");
            ctx->KeyPress(ImGuiMod_Ctrl | ImGuiKey_Z);
            GG_REQUIRE(s.waitUntil([&] { return s.refs(r.path) == refsBefore; }));
            s.settle();
        }
        GG_CHECK(s.statusPorcelain(r.path).empty());
    }
}

} // namespace ggtest

namespace ggtest {

GG_TEST("rebase-merges", "an octopus merge: git's merge row with three labels, previewed and run like git onto a new base",
    "IR-ACT-MERGE", "IR-ACT-LABEL", "IR-ACT-RESET", "IR-OPT-REBASE-MERGES", "IR-PREVIEW")
{
    // a1, then x1, y1, z1 on branches x, y, z from a1, merged at once (octopus), then e1; up
    // changes another file.
    const fs::path path = s.fixture(Recipe::Empty);
    s.commitFile(path, "base.txt", "base\n", "c0 base");
    const std::string c0 = s.head(path);
    s.git(path, {"switch", "-q", "-c", "up"});
    s.commitFile(path, "u.txt", "u\n", "u1 add u");
    s.git(path, {"switch", "-q", "main"});
    s.commitFile(path, "a.txt", "a\n", "a1 add a");
    const std::string a1 = s.head(path);
    for (const char* b : {"x", "y", "z"}) {
        s.git(path, {"switch", "-q", "-c", b, a1});
        s.commitFile(path, std::string(b) + ".txt", std::string(b) + "\n", std::string(b) + "1 add " + b);
    }
    s.git(path, {"switch", "-q", "main"});
    s.git(path, {"merge", "-q", "--no-ff", "x", "y", "z", "-m", "Octopus x y z"});
    GG_REQUIRE(parentsOf(s, path, "HEAD").size() == 4u);
    s.commitFile(path, "e.txt", "e\n", "e1 add e");
    installRecordingHooks(s, path);
    const fs::path pristine = copyRepo(s, path, "pristine");
    const auto refsBefore = s.refs(path);
    GG_REQUIRE(s.openRepository(path));
    GG_REQUIRE(openFrom(s, a1));
    ctx->ItemClick(irWidget("ir_onto").c_str());
    ctx->KeyChars("up");
    ctx->KeyPress(ImGuiKey_Enter);
    GG_REQUIRE(s.waitUntil([&] { return editor(s).context() && editor(s).context()->onto == s.revParse(path, "up"); }));
    ctx->ItemCheck(irWidget("ir_rebase_merges").c_str());
    const bool off = updateRefsOff(s);
    std::vector<std::string> startArgs{"--rebase-merges", "--update-refs", "--onto", "up", c0};
    if (off)
        startArgs.erase(startArgs.begin() + 1);
    todo::Todo gitList = gitStartingTodo(s, pristine, "start-octopus", startArgs);
    if (off)
        gitList = labelsAsOurs(editor(s).todo(), gitList);
    GG_CHECK(sameList(s, editor(s).todo(), gitList));
    Rows expectedRows{"label onto", "reset onto", "pick a1", "label branch-point", "pick x1", "update-ref refs/heads/x", "label x",
        "reset branch-point", "pick y1", "update-ref refs/heads/y", "label y", "reset branch-point", "pick z1",
        "update-ref refs/heads/z", "label z", "reset branch-point", "merge -C x y z", "pick e1"};
    if (off)
        std::erase_if(expectedRows, [](const std::string& row) { return row.rfind("update-ref ", 0) == 0; });
    GG_CHECK(rows(s) == expectedRows);
    const Preview* p = previewReady(s);
    GG_REQUIRE(p && p->ok);
    const Preview expected = *p;
    GG_REQUIRE(expected.rows.size() == 6u);
    GG_CHECK(expected.rows[4].merge && expected.rows[4].parents.size() == 4u && expected.rows[4].subject == "Octopus x y z");
    const std::string todoText = todo::format(editor(s).todo());

    datesForGgui(true);
    const bool started = start(s);
    datesForGgui(false);
    GG_REQUIRE(started);
    checkClean(s, path);
    checkGraph(s, path, "main", expected);
    GG_CHECK(parentsOf(s, path, "main~1").size() == 4u);
    // git on a copy: the same commits.
    const fs::path copy = copyRepo(s, pristine, "git-copy");
    const fs::path todoFile = s.root() / "octopus.todo";
    std::ofstream(todoFile, std::ios::binary) << todoText;
    // (--empty=stop is --empty=ask before git 2.45.)
    const std::string empty = s.gitAtLeast(2, 45) ? "--empty=stop" : "--empty=ask";
    const auto copyRun = gitDated(copy, {"-c", "sequence.editor=cp '" + todoFile.generic_string() + "'", "rebase", "-q", "-i", "--rebase-merges",
                                            empty, "--onto", "up", c0},
        "true");
    GG_CHECK(copyRun.ok());
    GG_CHECK_STR_EQ(branches(s, path), branches(s, copy));
    GG_CHECK_STR_EQ(s.read(path, ".git/post-rewrite.log"), s.read(copy, ".git/post-rewrite.log"));
    ctx->ItemClick("//History/**/###row_wt");
    ctx->KeyPress(ImGuiMod_Ctrl | ImGuiKey_Z);
    GG_CHECK(s.waitUntil([&] { return s.refs(path) == refsBefore; }));
    s.settle();
    GG_CHECK(s.statusPorcelain(path).empty());
}

} // namespace ggtest
