// Fonts (embedded NotoSansMono / JetBrainsMono / Material Symbols), dark/light style, UI scale.
#pragma once

#include "shell/Settings.hpp"

#include <imgui.h>

#include <IconsMaterialSymbols.h>

namespace ggui {

struct Palette {
    ImU32 lanes[8];
    ImU32 branch, branchCurrent, remote, tag, head, worktree, stash;
    ImU32 badgeText;
    ImU32 unpublished;      // text colour of commits not on any remote
    ImU32 conflict;
    ImU32 added, removed, addedBg, removedBg, hunkHeader, lineNumber;
    ImU32 dim;
    ImU32 error, errorBg, warning;
    ImU32 selection;
    ImU32 staged, unstaged, untracked;
};

class ThemeManager {
public:
    // Loads fonts once (call after the ImGui context exists).
    void loadFonts();
    // Applies theme and scale to the ImGui style.
    void apply(Theme theme, float scale);

    const Palette& palette() const { return m_palette; }
    ImFont* uiFont() const { return m_ui; }
    ImFont* monoFont() const { return m_mono; }
    Theme theme() const { return m_theme; }
    float scale() const { return m_scale; }

private:
    ImFont* m_ui = nullptr;
    ImFont* m_mono = nullptr;
    Palette m_palette{};
    Theme m_theme = Theme::Dark;
    float m_scale = 1.0f;
};

ThemeManager& theme();

} // namespace ggui
