// Screenshots for the project README, taken on a real repository: Dear ImGui
// (https://github.com/ocornut/imgui) with its master and docking branches, merges and tags. Shows the
// main view, the row context menu, the interactive rebase panel, the Light theme and the
// side-by-side diff. Written to <artifacts>/screens/readme-*.png.
// Manual: not part of the suite, run it with --test=readme.
//
// Needs a full clone of imgui (with its remote-tracking branches) in the environment variable
// GGUI_README_REPO. The test never touches it: it clones it into the test's temp directory, pins
// the copy to a fixed commit (kPinned) and makes the working-tree change there.
#include "panels/ChangesPanel.hpp"
#include "panels/DiffPanel.hpp"
#include "panels/HistoryPanel.hpp"
#include "panels/RebasePanel.hpp"
#include "shell/App.hpp"
#include "shell/Session.hpp"
#include "shell/Theme.hpp"
#include "tests/Harness.hpp"

#include <cstdlib>

namespace ggtest {

namespace {

void shot(Scenario& s, const std::string& name)
{
    s.ctx->Yield(3);
    s.screenshot(name);
}

// The imgui commit the copy is pinned to (master at the time the screenshots were made).
constexpr const char* kPinned = "46ff5a7aa";
// Commit shown in the main, context menu, Light and side-by-side shots: "ImGuiTextFilter: reworked
// filter to make space an AND operator" (imgui.cpp, imgui.h, imgui_demo.cpp, CHANGELOG).
constexpr const char* kShown = "d5c68c301";
constexpr const char* kShownFile = "imgui.cpp";
// Rebase shot: a local branch of four commits on top of master; the rebase starts at its first
// commit and the second one is turned into a fixup.
constexpr const char* kBranch = "glfw-ignore-empty-scroll";
// Remote branches kept in the copy besides master (the graph stays readable).
constexpr const char* kKeepRemotes[] = {"docking", "features/shadows"};

struct Imgui {
    fs::path path;
    std::string shown, rebaseFirst, rebaseSquash;
};

// Clones the user's imgui repository into the test directory (no hard links, nothing shared), pins
// master to kPinned, keeps a few remote-tracking branches and leaves two small edits in the working
// tree (one staged, one not). Returns an empty path when the clone is not available.
Imgui prepareImgui(Scenario& s)
{
    Imgui r;
    const char* env = std::getenv("GGUI_README_REPO");
    if (!env || !*env) {
        s.ctx->LogError("readme: set GGUI_README_REPO to a full clone of https://github.com/ocornut/imgui");
        return r;
    }
    const fs::path source = fs::path(env);
    std::error_code ec;
    if (!fs::exists(source / ".git", ec) && !fs::exists(source / "HEAD", ec)) {
        s.ctx->LogError("readme: GGUI_README_REPO=%s is not a git repository", env);
        return r;
    }
    const fs::path dest = s.path("imgui");
    s.git(s.root(), {"clone", "-q", "--no-hardlinks", source.string(), dest.string()});
    // A clone only gets the source's local branches: bring over its remote-tracking ones too.
    s.git(dest, {"fetch", "-q", "--no-tags", source.string(), "+refs/remotes/origin/*:refs/remotes/origin/*"});
    s.git(dest, {"config", "user.name", "Rokas Kupstys"});
    s.git(dest, {"config", "user.email", "rokupstys@gmail.com"});
    s.git(dest, {"reset", "-q", "--hard", kPinned});
    // Keep master, docking and a feature branch; drop the remaining remote-tracking branches.
    for (const std::string& ref : s.refs(dest)) {
        const std::string name = ref.substr(0, ref.find(' '));
        const std::string prefix = "refs/remotes/origin/";
        if (name.rfind(prefix, 0) != 0)
            continue;
        const std::string branch = name.substr(prefix.size());
        bool keep = branch == "master" || branch == "HEAD";
        for (const char* k : kKeepRemotes)
            keep = keep || branch == k;
        if (!keep)
            s.git(dest, {"update-ref", "-d", name});
    }
    s.git(dest, {"remote", "set-url", "origin", "https://github.com/ocornut/imgui.git"});
    r.path = dest;
    r.shown = s.revParse(dest, kShown);

    auto edit = [&](const std::string& rel, const std::string& from, const std::string& to) {
        std::string text = s.read(dest, rel);
        const size_t at = text.find(from);
        if (at == std::string::npos) {
            s.ctx->LogError("readme: anchor not found in %s", rel.c_str());
            return;
        }
        text.replace(at, from.size(), to);
        s.write(dest, rel, text);
    };

    // A local branch of four commits on top of master, for the rebase shot (nothing of it is on
    // a remote, so the rebase panel has no warnings).
    auto commit = [&](const std::string& message, int minute) {
        gg::RunRequest req;
        req.args = {"git", "commit", "-q", "-a", "-m", message};
        req.cwd = dest;
        const std::string date = "2026-09-30T10:" + std::string(minute < 10 ? "0" : "") + std::to_string(minute) + ":00+0200";
        req.env.emplace_back("GIT_AUTHOR_DATE", date);
        req.env.emplace_back("GIT_COMMITTER_DATE", date);
        if (!gg::run(req).ok())
            s.ctx->LogError("readme: commit '%s' failed", message.c_str());
        return s.head(dest);
    };
    s.git(dest, {"switch", "-q", "-c", kBranch});
    edit("backends/imgui_impl_glfw.cpp",
        "    ImGuiIO& io = ImGui::GetIO(bd->Context);\n    io.AddMouseWheelEvent((float)xoffset, (float)yoffset);",
        "    ImGuiIO& io = ImGui::GetIO(bd->Context);\n    if (xoffset == 0.0 && yoffset == 0.0)\n        return; // Ignore empty scroll events\n    io.AddMouseWheelEvent((float)xoffset, (float)yoffset);");
    commit("Backends: GLFW: ignore empty scroll events.", 12);
    edit("backends/imgui_impl_glfw.cpp", "return; // Ignore empty scroll events", "return; // Some platforms send (0,0) scroll events: nothing to forward");
    r.rebaseSquash = commit("Backends: GLFW: reworded a comment.", 14);
    edit("backends/imgui_impl_glfw.cpp",
        "// (minor and older changes stripped away, please see git history for details)\n",
        "// (minor and older changes stripped away, please see git history for details)\n//  2026-09-30: Inputs: ignore scroll events with no offset.\n");
    commit("Backends: GLFW: added a changelog entry.", 21);
    edit("docs/CHANGELOG.txt", "Other Changes:\n\n",
        "Other Changes:\n\n- Backends: GLFW: empty scroll events (0,0) are no longer forwarded to\n  io.AddMouseWheelEvent().\n");
    commit("Docs: changelog entry for the GLFW scroll change.", 25);
    r.rebaseFirst = s.revParse(dest, std::string(kBranch) + "~3");
    s.git(dest, {"switch", "-q", "master"});

    // Working tree: a staged edit in docs/TODO.txt, an unstaged one in imgui.cpp.
    std::string todo = s.read(dest, "docs/TODO.txt");
    if (!todo.empty() && todo.back() != '\n')
        todo += '\n';
    s.write(dest, "docs/TODO.txt", todo + " - backends: glfw: repaint while the window is being resized (glfwSetWindowRefreshCallback).\n");
    s.git(dest, {"add", "docs/TODO.txt"});
    edit("imgui.cpp", "const char* ImGui::GetVersion()\n{\n    return IMGUI_VERSION;",
        "// Returns the version string, e.g. \"1.92.4 WIP\" (see IMGUI_VERSION in imgui.h).\nconst char* ImGui::GetVersion()\n{\n    return IMGUI_VERSION;");
    s.track(dest);
    return r;
}

std::string rowRef(const std::string& hex) { return "//History/**/###row_" + hex; }

bool selectCommit(Scenario& s, const std::string& hex)
{
    const std::string row = rowRef(hex);
    if (!s.waitUntil([&] { return s.itemExists(row.c_str()); }))
        return false;
    for (int attempt = 0; attempt < 3; ++attempt) {
        s.ctx->ItemClick(row.c_str());
        if (s.waitUntil([&] { return s.session()->selection().id.hex() == hex; }, 3.0f))
            return true;
    }
    return false;
}

void setTheme(Scenario& s, const char* theme)
{
    s.app.openSettings();
    s.ctx->Yield(2);
    s.ctx->ItemClick("//Settings/##settings_tabs/General");
    s.ctx->SetRef("Settings");
    s.comboSelect("//Settings/##settings_tabs/General/Theme##theme", theme);
    s.ctx->SetRef("");
    s.ctx->Yield(2);
    s.ctx->WindowClose("//Settings");
    s.ctx->Yield(3);
}

} // namespace

GG_MANUAL_TEST("readme", "screenshots of the imgui repository for the README")
{
    const Imgui t = prepareImgui(s);
    GG_REQUIRE(!t.path.empty());
    GG_REQUIRE(s.openRepository(t.path));
    GG_REQUIRE(s.waitUntil([&] { return s.session()->changes().rows().size() == 2; }));
    // The history of ~10k commits loads in the background: wait for all of it.
    GG_REQUIRE(s.waitUntil([&] { return s.itemExists(rowRef(t.shown).c_str()); }, 120.0f));
    s.settle();
    s.waitIdle();

    const std::string diffFile = s.child("//Changes", "##files") + "/" + Scenario::escapeRef(kShownFile)
        + "/###file_" + Scenario::escapeRef(kShownFile);
    auto showMain = [&] {
        GG_REQUIRE(selectCommit(s, t.shown));
        s.settle();
        GG_REQUIRE(s.waitUntil([&] { return s.itemExists(diffFile.c_str()); }));
        ctx->ItemClick(diffFile.c_str());
        s.showPanel("Diff");
        s.settle();
        GG_REQUIRE(s.waitUntil([&] {
            const auto& d = s.session()->diff().diff();
            return d && !d->files.empty();
        }));
        ctx->MouseMove("//###Toolbar");
        ctx->Yield(5);
    };

    // 1. Main view, dark.
    showMain();
    shot(s, "readme-main");

    // 2. Row context menu.
    ctx->ItemClick(rowRef(t.shown).c_str(), ImGuiMouseButton_Right);
    ctx->Yield(5);
    shot(s, "readme-context-menu");
    ctx->PopupCloseAll();
    ctx->Yield(3);

    // 4. Light theme, same view.
    setTheme(s, "Light");
    showMain();
    shot(s, "readme-light");
    setTheme(s, "Dark");

    // 3. Interactive rebase of the last commits of master, with one of them turned into a fixup.
    GG_REQUIRE(selectCommit(s, t.rebaseFirst));
    ctx->KeyPress(ImGuiKey_I);
    GG_REQUIRE(s.waitUntil([&] { return s.session()->rebase().isOpen() && s.session()->rebase().context() != nullptr; }));
    s.comboSelect(("//Interactive rebase/**/###ir_action_" + t.rebaseSquash).c_str(), "fixup");
    s.settle();
    ctx->MouseMove("//###Toolbar");
    ctx->Yield(5);
    shot(s, "readme-rebase");
    ctx->ItemClick("//Interactive rebase/###ir_cancel");
    ctx->Yield(3);

    // 5. Side-by-side diff, with the Diff panel widened (dragging the splitter next to it).
    showMain();
    s.comboSelect("//Diff/##diff_view", "Side by side");
    const ImVec2 origin = ImGui::GetMainViewport()->Pos;
    ctx->MouseMoveToPos(ImVec2(origin.x + 1074, origin.y + 400));
    ctx->MouseDown(0);
    ctx->MouseMoveToPos(ImVec2(origin.x + 870, origin.y + 400));
    ctx->MouseUp(0);
    s.settle();
    ctx->MouseMove("//###Toolbar");
    ctx->Yield(5);
    shot(s, "readme-diff-sbs");
}

} // namespace ggtest
