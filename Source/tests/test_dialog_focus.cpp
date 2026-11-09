// Dialog buttons (ui-spec §9): the confirm button has the accent color and the first keyboard focus.
// A destructive dialog gives the first keyboard focus to Cancel.
#include "shell/App.hpp"
#include "shell/Dialogs.hpp"
#include "tests/Harness.hpp"

#include <imgui_internal.h>

namespace ggtest {

namespace {

bool g_confirmEnabled = true;

ggui::Form confirmForm(const char* title, bool danger, bool* confirmed)
{
    ggui::Form f;
    f.title = title;
    f.message = "Do it?";
    f.buttons.push_back({"Confirm", [confirmed](ggui::Form&) { *confirmed = true; }});
    f.buttons.back().danger = danger;
    f.buttons.back().enabled = [](const ggui::Form&) { return g_confirmEnabled; };
    f.buttons.push_back({"Cancel", {}});
    return f;
}

} // namespace

GG_TEST("dialog_focus", "the confirm button has the first focus, or Cancel has it when the action is destructive")
{
    ImGuiContext& g = *ImGui::GetCurrentContext();
    bool confirmed = false;
    s.app.dialogs().open(confirmForm("Focus normal", false, &confirmed));
    GG_REQUIRE(s.dialogOpen("Focus normal"));
    ctx->Yield(3);
    GG_CHECK(g.NavId == ctx->ItemInfo("//Focus normal/Confirm").ID);
    GG_CHECK(g.NavId != ctx->ItemInfo("//Focus normal/Cancel").ID);
    // Enter presses the focused Confirm button.
    ctx->KeyPress(ImGuiKey_Enter);
    GG_CHECK(s.waitUntil([&] { return confirmed; }, 3.0f));
    GG_CHECK(!s.app.dialogs().anyOpen());

    g_confirmEnabled = true;
    confirmed = false;
    s.app.dialogs().open(confirmForm("Focus danger", true, &confirmed));
    GG_REQUIRE(s.dialogOpen("Focus danger"));
    ctx->Yield(3);
    GG_CHECK(g.NavId == ctx->ItemInfo("//Focus danger/Cancel").ID);
    GG_CHECK(g.NavId != ctx->ItemInfo("//Focus danger/Confirm").ID);
    // Enter activates the focused button: Cancel closes the dialog and the action does not run.
    ctx->KeyPress(ImGuiKey_Enter);
    GG_CHECK(s.waitUntil([&] { return !s.app.dialogs().anyOpen(); }, 3.0f));
    GG_CHECK(!confirmed);

    // A confirm button that is disabled on the first frame takes the focus when it becomes enabled.
    g_confirmEnabled = false;
    s.app.dialogs().open(confirmForm("Focus late", false, &confirmed));
    GG_REQUIRE(s.dialogOpen("Focus late"));
    ctx->Yield(3);
    GG_CHECK(g.NavId != ctx->ItemInfo("//Focus late/Confirm").ID);
    g_confirmEnabled = true;
    ctx->Yield(3);
    GG_CHECK(g.NavId == ctx->ItemInfo("//Focus late/Confirm").ID);
    s.dialogButton("Focus late", "Cancel");
}

} // namespace ggtest
