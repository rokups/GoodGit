// Fonts (embedded NotoSansMono / JetBrainsMono / Material Symbols), dark/light style, UI scale.
#pragma once

#include "shell/Settings.hpp"

#include <imgui.h>

#include <IconsMaterialSymbols.h>

namespace ggui {

// Colours used as text are adjusted when a theme is applied so they keep a readable contrast
// against the window background; badge fills keep one against badgeText.
struct Palette {
    ImU32 lanes[8];
    ImU32 branch, branchCurrent, remote, tag, head, worktree, stash; // badge fills
    ImU32 branchText, branchCurrentText, remoteText, tagText;        // the same, as text colours
    ImU32 badgeText;
    ImU32 unpublished;      // text colour of commits not on any remote
    ImU32 conflict;         // conflict as text/graph colour
    ImU32 conflictFill;     // conflict badge fill (against badgeText)
    ImU32 sectionHeader, sectionHeaderHovered, sectionHeaderActive; // neutral CollapsingHeader/TreeNode bars
    ImU32 added, removed, addedBg, removedBg, hunkHeader, lineNumber;
    ImU32 dim;
    ImU32 error, errorBg, warning;  // error: text on errorBg
    ImU32 errorText;                 // an error as text on the panel
    ImU32 selection;
    ImU32 selectionHovered; // a selected list row under the mouse (hover alone is a neutral overlay)
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

// Neutral grey for collapsing section bars and tree nodes (selection has its own stronger highlight): construct
// around the CollapsingHeader/TreeNode call.
struct SectionHeaderColors {
    SectionHeaderColors();
    ~SectionHeaderColors();
    SectionHeaderColors(const SectionHeaderColors&) = delete;
    SectionHeaderColors& operator=(const SectionHeaderColors&) = delete;
};

// WCAG contrast ratio of two colours (1 to 21; alpha ignored).
float contrastRatio(ImU32 a, ImU32 b);
// `fg` moved towards white or black (whichever `bg` is further from) until its contrast with `bg`
// reaches `minRatio`.
ImU32 readableOn(ImU32 fg, ImU32 bg, float minRatio);
// Minimum contrasts: body text and coloured text; dimmed/disabled text.
constexpr float kTextContrast = 4.5f;
constexpr float kDimContrast = 3.5f;

} // namespace ggui
