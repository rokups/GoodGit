// Pre-flight dialog for non-text conflicts (§4.10 pre-flight; P3-03).
#include "panels/HistoryPanel.hpp"
#include "shell/App.hpp"
#include "shell/Dialogs.hpp"
#include "shell/Session.hpp"
#include "tests/Harness.hpp"

#include <fstream>
#include <functional>
#include <map>
#include <sstream>

namespace ggtest {

namespace {

constexpr const char* kDialog = "Resolve conflicts before rewriting";

// base → "ours" (one commit) and base → "theirs" (one commit, checked out). Each side is
// built by a function that stages changes; the base too.
struct TwoSides {
    fs::path path;
    std::string ours, theirs;
};

using Stage = std::function<void(Scenario&, const fs::path&)>;

TwoSides twoSides(Scenario& s, const std::string& name, const Stage& base, const Stage& ours, const Stage& theirs)
{
    TwoSides t;
    t.path = s.path(name);
    s.git(s.root(), {"init", "-q", "-b", "main", t.path.string()});
    s.track(t.path);
    s.write(t.path, "README", "base\n");
    s.git(t.path, {"add", "README"});
    base(s, t.path);
    s.git(t.path, {"commit", "-q", "-m", "base"});
    s.git(t.path, {"switch", "-q", "-c", "ours"});
    ours(s, t.path);
    s.git(t.path, {"commit", "-q", "-m", "ours change"});
    t.ours = s.head(t.path);
    s.git(t.path, {"switch", "-q", "-c", "theirs", "main"});
    theirs(s, t.path);
    s.git(t.path, {"commit", "-q", "-m", "theirs change"});
    t.theirs = s.head(t.path);
    return t;
}

std::string hashObject(Scenario& s, const fs::path& repo, const std::string& content)
{
    return gg::trim(s.git(repo, {"hash-object", "-w", "--stdin"}, content).out);
}

void cacheInfo(Scenario& s, const fs::path& repo, const std::string& mode, const std::string& id, const std::string& path)
{
    s.git(repo, {"update-index", "--add", "--cacheinfo", mode + "," + id + "," + path});
}

// Rebase "theirs" onto "ours" through the UI (History row menu).
void rebaseTheirs(Scenario& s, const TwoSides& t)
{
    s.waitUntil([&] { return s.session()->history().row(ggui::core::Oid::fromHex(t.theirs)) != nullptr; });
    s.contextMenu(("//History/**/###row_" + t.theirs).c_str(), "Rebase onto...");
    if (!s.dialogOpen("Rebase onto"))
        return;
    s.dialogText("Rebase onto", "destination", "ours");
    s.dialogButton("Rebase onto", "Rebase");
}

} // namespace

GG_TEST("preflight", "every non-text conflict kind asks for a decision, then the rewrite goes through",
    "CONF-PREFLIGHT-BINARY", "CONF-PREFLIGHT-MODIFY-DELETE", "CONF-PREFLIGHT-TYPE", "CONF-PREFLIGHT-SUBMODULE",
    "CONF-PREFLIGHT-MODE", "CONF-PREFLIGHT-RENAME", "CONF-PREFLIGHT-FILTER", "CONF-PREFLIGHT-OPTOUT",
    "CONF-PREFLIGHT-DISK-FILE")
{
    const std::string rename = "one\ntwo\nthree\nfour\nfive\nsix\nseven\neight\n";
    struct Case {
        const char* name;
        const char* kind;
        Stage base, ours, theirs;
        const char* choice;     // combo entry chosen
        std::function<void(Scenario&, const TwoSides&)> check;
        const char* diskFile = nullptr; // content for "Use a file from disk"
    };
    const std::vector<Case> cases{
        {"binary", "binary",
            [](Scenario& sc, const fs::path& p) { sc.write(p, "b.bin", std::string("base\0b", 6)); sc.git(p, {"add", "b.bin"}); },
            [](Scenario& sc, const fs::path& p) { sc.write(p, "b.bin", std::string("ours\0b", 6)); sc.git(p, {"add", "b.bin"}); },
            [](Scenario& sc, const fs::path& p) { sc.write(p, "b.bin", std::string("thrs\0b", 6)); sc.git(p, {"add", "b.bin"}); },
            "Take side A (the new base)",
            [](Scenario& sc, const TwoSides& t) { GG_CHECK(sc.read(t.path, "b.bin") == std::string("ours\0b", 6)); }},
        {"modify-delete", "modify/delete",
            [](Scenario& sc, const fs::path& p) { sc.write(p, "f.txt", "x\n"); sc.git(p, {"add", "f.txt"}); },
            [](Scenario& sc, const fs::path& p) { sc.write(p, "f.txt", "changed\n"); sc.git(p, {"add", "f.txt"}); },
            [](Scenario& sc, const fs::path& p) { sc.git(p, {"rm", "-q", "f.txt"}); },
            "Keep deleted",
            [](Scenario&, const TwoSides& t) { GG_CHECK(!fs::exists(t.path / "f.txt")); }},
        {"symlink", "symlink",
            [](Scenario&, const fs::path&) {},
            [](Scenario& sc, const fs::path& p) { cacheInfo(sc, p, "120000", hashObject(sc, p, "target"), "l"); },
            [](Scenario& sc, const fs::path& p) { sc.write(p, "l", "regular\n"); sc.git(p, {"add", "l"}); },
            "Take side B (",
            [](Scenario& sc, const TwoSides& t) { GG_CHECK_STR_EQ(sc.read(t.path, "l"), "regular\n"); }},
        {"submodule", "submodule",
            [](Scenario&, const fs::path&) {},
            [](Scenario& sc, const fs::path& p) { cacheInfo(sc, p, "160000", sc.revParse(p, "HEAD"), "sub"); },
            [](Scenario& sc, const fs::path& p) { cacheInfo(sc, p, "160000", std::string(40, '1'), "sub"); },
            "Take side A (the new base)",
            [](Scenario& sc, const TwoSides& t) { GG_CHECK(sc.gitOut(t.path, {"ls-tree", "HEAD", "sub"}).rfind("160000 commit", 0) == 0); }},
        {"mode", "mode",
            [](Scenario&, const fs::path&) {},
            [](Scenario& sc, const fs::path& p) { sc.write(p, "m.sh", "ours\n"); sc.git(p, {"add", "m.sh"}); },
            [](Scenario& sc, const fs::path& p) {
                sc.write(p, "m.sh", "theirs\n");
                fs::permissions(p / "m.sh", fs::perms::owner_exec, fs::perm_options::add);
                sc.git(p, {"add", "m.sh"});
            },
            "Keep mode 100755",
            [](Scenario& sc, const TwoSides& t) {
                // The mode is decided; the text conflict itself is first-class.
                GG_CHECK(sc.gitOut(t.path, {"ls-tree", "HEAD", "m.sh"}).rfind("100755", 0) == 0);
                GG_CHECK(sc.gitOut(t.path, {"show", "HEAD:m.sh"}).find("<<<<<<<") != std::string::npos);
            }},
        {"rename", "rename",
            [rename](Scenario& sc, const fs::path& p) { sc.write(p, "f.txt", rename); sc.git(p, {"add", "f.txt"}); },
            [](Scenario& sc, const fs::path& p) { sc.git(p, {"mv", "f.txt", "g.txt"}); },
            [](Scenario& sc, const fs::path& p) { sc.git(p, {"mv", "f.txt", "h.txt"}); },
            "Take side B (",
            [](Scenario&, const TwoSides& t) { GG_CHECK(fs::exists(t.path / "h.txt")); }},
        {"rename-a", "rename",
            [rename](Scenario& sc, const fs::path& p) { sc.write(p, "f.txt", rename); sc.git(p, {"add", "f.txt"}); },
            [](Scenario& sc, const fs::path& p) { sc.git(p, {"mv", "f.txt", "g.txt"}); },
            [](Scenario& sc, const fs::path& p) { sc.git(p, {"mv", "f.txt", "h.txt"}); },
            "Take side A (",
            [](Scenario&, const TwoSides& t) { GG_CHECK(fs::exists(t.path / "g.txt") && !fs::exists(t.path / "h.txt")); }},
        {"rename-base", "rename",
            [rename](Scenario& sc, const fs::path& p) { sc.write(p, "f.txt", rename); sc.git(p, {"add", "f.txt"}); },
            [](Scenario& sc, const fs::path& p) { sc.git(p, {"mv", "f.txt", "g.txt"}); },
            [](Scenario& sc, const fs::path& p) { sc.git(p, {"mv", "f.txt", "h.txt"}); },
            "Take the base (",
            [](Scenario&, const TwoSides& t) { GG_CHECK(fs::exists(t.path / "f.txt") && !fs::exists(t.path / "g.txt")); }},
        {"rename-none", "rename",
            [rename](Scenario& sc, const fs::path& p) { sc.write(p, "f.txt", rename); sc.git(p, {"add", "f.txt"}); },
            [](Scenario& sc, const fs::path& p) { sc.git(p, {"mv", "f.txt", "g.txt"}); },
            [](Scenario& sc, const fs::path& p) { sc.git(p, {"mv", "f.txt", "h.txt"}); },
            "Keep deleted",
            [](Scenario&, const TwoSides& t) {
                GG_CHECK(!fs::exists(t.path / "f.txt") && !fs::exists(t.path / "g.txt") && !fs::exists(t.path / "h.txt"));
            }},
        {"add-add-mode", "mode",
            [](Scenario&, const fs::path&) {},
            [](Scenario& sc, const fs::path& p) { sc.write(p, "n.sh", "ours\n"); sc.git(p, {"add", "n.sh"}); },
            [](Scenario& sc, const fs::path& p) {
                sc.write(p, "n.sh", "theirs\n");
                fs::permissions(p / "n.sh", fs::perms::owner_exec, fs::perm_options::add);
                sc.git(p, {"add", "n.sh"});
            },
            "Keep mode 100644",
            [](Scenario& sc, const TwoSides& t) { GG_CHECK(sc.gitOut(t.path, {"ls-tree", "HEAD", "n.sh"}).rfind("100644", 0) == 0); }},
        {"deleted-from-disk", "modify/delete",
            [](Scenario& sc, const fs::path& p) { sc.write(p, "e.sh", "x\n"); sc.git(p, {"add", "e.sh"}); },
            [](Scenario& sc, const fs::path& p) {
                sc.write(p, "e.sh", "changed\n");
                fs::permissions(p / "e.sh", fs::perms::owner_exec, fs::perm_options::add);
                sc.git(p, {"add", "e.sh"});
            },
            [](Scenario& sc, const fs::path& p) { sc.git(p, {"rm", "-q", "e.sh"}); },
            "Use a file from disk",
            [](Scenario& sc, const TwoSides& t) {
                GG_CHECK_STR_EQ(sc.read(t.path, "e.sh"), "picked\n");
                GG_CHECK(sc.gitOut(t.path, {"ls-tree", "HEAD", "e.sh"}).rfind("100755", 0) == 0); // side A's mode
            },
            "picked\n"},
        {"filtered", "filtered",
            [](Scenario& sc, const fs::path& p) {
                sc.write(p, ".gitattributes", "*.lfs filter=lfs\n");
                sc.write(p, "x.lfs", "base\n");
                sc.git(p, {"add", ".gitattributes", "x.lfs"});
            },
            [](Scenario& sc, const fs::path& p) { sc.write(p, "x.lfs", "ours\n"); sc.git(p, {"add", "x.lfs"}); },
            [](Scenario& sc, const fs::path& p) { sc.write(p, "x.lfs", "theirs\n"); sc.git(p, {"add", "x.lfs"}); },
            "Use a file from disk",
            [](Scenario& sc, const TwoSides& t) { GG_CHECK_STR_EQ(sc.read(t.path, "x.lfs"), "from disk\n"); },
            "from disk\n"},
        {"opt-out", "opt-out",
            [](Scenario& sc, const fs::path& p) {
                sc.write(p, ".gitattributes", "docs.txt gg-conflicts=false\n");
                sc.write(p, "docs.txt", "base\n");
                sc.git(p, {"add", ".gitattributes", "docs.txt"});
            },
            [](Scenario& sc, const fs::path& p) { sc.write(p, "docs.txt", "ours\n"); sc.git(p, {"add", "docs.txt"}); },
            [](Scenario& sc, const fs::path& p) { sc.write(p, "docs.txt", "theirs\n"); sc.git(p, {"add", "docs.txt"}); },
            "Take the base",
            [](Scenario& sc, const TwoSides& t) { GG_CHECK_STR_EQ(sc.read(t.path, "docs.txt"), "base\n"); }},
    };
    for (const auto& c : cases) {
        ctx->LogInfo("---- case %s", c.name);
        const TwoSides t = twoSides(s, c.name, c.base, c.ours, c.theirs);
        GG_REQUIRE(s.openRepository(t.path));
        rebaseTheirs(s, t);
        GG_REQUIRE(s.dialogOpen(kDialog));
        const ggui::Form* f = s.app.dialogs().current();
        GG_REQUIRE(f != nullptr);
        const ggui::Field* info = f->field("conflict_0");
        GG_REQUIRE(info != nullptr);
        ctx->LogInfo("pre-flight: %s", info->text.c_str());
        GG_CHECK(info->text.find("(" + std::string(c.kind) + ")") != std::string::npos);
        const ggui::Field* combo = f->field("choice_0");
        GG_REQUIRE(combo != nullptr);
        std::string option;
        for (const auto& o : combo->options)
            if (o.rfind(c.choice, 0) == 0)
                option = o;
        GG_REQUIRE(!option.empty());
        s.comboSelect((std::string("//") + kDialog + "/Resolution##choice_0").c_str(), option.c_str());
        if (c.diskFile) {
            s.write(s.root(), "chosen.txt", c.diskFile);
            s.dialogText(kDialog, "file_0", (s.root() / "chosen.txt").string());
        }
        s.dialogButton(kDialog, "Continue");
        GG_CHECK(s.waitUntil([&] { return s.revParse(t.path, "theirs~1") == t.ours; }));
        s.settle();
        c.check(s, t);
        GG_CHECK(s.statusPorcelain(t.path).empty());
        ctx->MenuClick("//##MainMenuBar/Repository/Close repository");
        ctx->Yield(3);
    }
}

GG_TEST("preflight", "conflicts are listed per commit in order; Cancel leaves .git byte-identical", "CONF-PREFLIGHT-STEP",
    "CONF-PREFLIGHT-CANCEL", "IR-MEMORY-CANCEL")
{
    // theirs has two commits, each with a binary conflict against ours.
    TwoSides t = twoSides(s, "two-steps",
        [](Scenario& sc, const fs::path& p) {
            sc.write(p, "a.bin", std::string("a\0", 2));
            sc.write(p, "b.bin", std::string("b\0", 2));
            sc.git(p, {"add", "a.bin", "b.bin"});
        },
        [](Scenario& sc, const fs::path& p) {
            sc.write(p, "a.bin", std::string("ours a\0", 7));
            sc.write(p, "b.bin", std::string("ours b\0", 7));
            sc.git(p, {"add", "a.bin", "b.bin"});
        },
        [](Scenario& sc, const fs::path& p) { sc.write(p, "a.bin", std::string("theirs a\0", 9)); sc.git(p, {"add", "a.bin"}); });
    s.write(t.path, "b.bin", std::string("theirs b\0", 9));
    s.git(t.path, {"add", "b.bin"});
    s.git(t.path, {"commit", "-q", "-m", "theirs second"});
    t.theirs = s.head(t.path);
    const auto before = s.gitDirBytes(t.path);
    GG_REQUIRE(s.openRepository(t.path));
    const std::string first = s.revParse(t.path, "theirs~1");
    s.waitUntil([&] { return s.session()->history().row(ggui::core::Oid::fromHex(first)) != nullptr; });
    s.contextMenu(("//History/**/###row_" + first).c_str(), "Rebase onto...");
    GG_REQUIRE(s.dialogOpen("Rebase onto"));
    s.dialogText("Rebase onto", "destination", "ours");
    s.dialogButton("Rebase onto", "Rebase");
    GG_REQUIRE(s.dialogOpen(kDialog));
    const ggui::Form* f = s.app.dialogs().current();
    GG_REQUIRE(f->field("conflict_0") && f->field("conflict_1"));
    GG_CHECK(f->field("conflict_0")->text.find("theirs change: a.bin") != std::string::npos);
    GG_CHECK(f->field("conflict_1")->text.find("theirs second: b.bin") != std::string::npos);
    s.dialogButton(kDialog, "Cancel");
    s.settle();
    ctx->MenuClick("//##MainMenuBar/Repository/Close repository");
    ctx->Yield(3);
    const auto after = s.gitDirBytes(t.path);
    for (const auto& [name, bytes] : after)
        if (!before.count(name) || before.at(name) != bytes)
            ctx->LogError("changed under .git: %s", name.c_str());
    for (const auto& [name, bytes] : before)
        if (!after.count(name))
            ctx->LogError("removed under .git: %s", name.c_str());
    GG_CHECK(after == before);
}

} // namespace ggtest
