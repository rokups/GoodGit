// Small housekeeping checks of the shared UI code (GG-124).
#include "tests/Harness.hpp"
#include "util/Ui.hpp"

#include <imgui_internal.h>

#include <string>

namespace ggtest {

GG_TEST("uihousekeeping", "scroll records of gone windows are pruned")
{
    int n = 0;
    ctx->Test->GuiFunc = [&](ImGuiTestContext*) {
        // A new window each frame; each one is gone in the next frame.
        const std::string name = "ScrollProbe" + std::to_string(n++);
        ImGui::SetNextWindowPos(ImVec2(10, 10));
        ImGui::Begin(name.c_str(), nullptr, ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoDocking);
        ImGui::End();
    };
    ctx->Yield(400);
    ctx->Test->GuiFunc = nullptr;
    GG_CHECK(n >= 400);
    GG_CHECK(ggui::scrollRecordCount() < 200);
}

} // namespace ggtest
