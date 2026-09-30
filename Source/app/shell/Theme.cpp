#include "shell/Theme.hpp"

#include <imgui_internal.h>

#include <algorithm>
#include <cmath>

extern "C" {
extern const unsigned char ggui_font_material_symbols[];
unsigned long long ggui_font_material_symbols_size_value(void);
extern const unsigned char ggui_font_noto_sans_mono[];
unsigned long long ggui_font_noto_sans_mono_size_value(void);
extern const unsigned char ggui_font_jetbrains_mono[];
unsigned long long ggui_font_jetbrains_mono_size_value(void);
}

namespace ggui {

namespace {

constexpr float kBaseFontSize = 15.0f;

ImFont* addFont(const unsigned char* data, unsigned long long size, const char* name)
{
    ImGuiIO& io = ImGui::GetIO();
    ImFontConfig cfg;
    cfg.FontDataOwnedByAtlas = false;
    ImStrncpy(cfg.Name, name, IM_ARRAYSIZE(cfg.Name));
    ImFont* font = io.Fonts->AddFontFromMemoryTTF(const_cast<unsigned char*>(data), static_cast<int>(size),
        kBaseFontSize, &cfg);
    static const ImWchar iconRanges[] = {ICON_MIN_MS, ICON_MAX_16_MS, 0};
    ImFontConfig icons;
    icons.FontDataOwnedByAtlas = false;
    icons.MergeMode = true;
    icons.PixelSnapH = true;
    // Material Symbols sit low against the text: this offset (scaled with the size by ImGui)
    // centres an icon on a capital letter (UI-ICON-ALIGN measures it).
    icons.GlyphOffset = ImVec2(0.0f, 2.0f);
    icons.GlyphMinAdvanceX = kBaseFontSize;
    io.Fonts->AddFontFromMemoryTTF(const_cast<unsigned char*>(ggui_font_material_symbols),
        static_cast<int>(ggui_font_material_symbols_size_value()), kBaseFontSize, &icons, iconRanges);
    return font;
}

ImU32 rgb(int r, int g, int b, int a = 255) { return IM_COL32(r, g, b, a); }

float luminance(ImU32 c)
{
    auto channel = [](unsigned v) {
        const float s = static_cast<float>(v) / 255.0f;
        return s <= 0.03928f ? s / 12.92f : std::pow((s + 0.055f) / 1.055f, 2.4f);
    };
    return 0.2126f * channel((c >> IM_COL32_R_SHIFT) & 0xFF) + 0.7152f * channel((c >> IM_COL32_G_SHIFT) & 0xFF)
        + 0.0722f * channel((c >> IM_COL32_B_SHIFT) & 0xFF);
}

// `over` composited on the opaque `under`.
ImU32 over(ImU32 top, ImU32 under)
{
    const ImVec4 t = ImGui::ColorConvertU32ToFloat4(top);
    const ImVec4 u = ImGui::ColorConvertU32ToFloat4(under);
    return ImGui::ColorConvertFloat4ToU32(ImVec4(t.x * t.w + u.x * (1 - t.w), t.y * t.w + u.y * (1 - t.w),
        t.z * t.w + u.z * (1 - t.w), 1.0f));
}

} // namespace

float contrastRatio(ImU32 a, ImU32 b)
{
    const float la = luminance(a), lb = luminance(b);
    return (std::max(la, lb) + 0.05f) / (std::min(la, lb) + 0.05f);
}

ImU32 readableOn(ImU32 fg, ImU32 bg, float minRatio)
{
    const ImVec4 target = luminance(bg) < 0.18f ? ImVec4(1, 1, 1, 1) : ImVec4(0, 0, 0, 1);
    const ImVec4 from = ImGui::ColorConvertU32ToFloat4(fg);
    ImU32 c = fg;
    for (int step = 1; step <= 20 && contrastRatio(c, bg) < minRatio; ++step) {
        const float t = static_cast<float>(step) / 20.0f;
        c = ImGui::ColorConvertFloat4ToU32(ImVec4(from.x + (target.x - from.x) * t, from.y + (target.y - from.y) * t,
            from.z + (target.z - from.z) * t, from.w));
    }
    return c;
}

ThemeManager& theme()
{
    static ThemeManager instance;
    return instance;
}

SectionHeaderColors::SectionHeaderColors()
{
    const Palette& p = theme().palette();
    ImGui::PushStyleColor(ImGuiCol_Header, p.sectionHeader);
    ImGui::PushStyleColor(ImGuiCol_HeaderHovered, p.sectionHeaderHovered);
    ImGui::PushStyleColor(ImGuiCol_HeaderActive, p.sectionHeaderActive);
}

SectionHeaderColors::~SectionHeaderColors()
{
    ImGui::PopStyleColor(3);
}

void ThemeManager::loadFonts()
{
    m_ui = addFont(ggui_font_noto_sans_mono, ggui_font_noto_sans_mono_size_value(), "NotoSansMono");
    m_mono = addFont(ggui_font_jetbrains_mono, ggui_font_jetbrains_mono_size_value(), "JetBrainsMono");
    ImGui::GetIO().FontDefault = m_ui;
}

void ThemeManager::apply(Theme t, float scale)
{
    m_theme = t;
    m_scale = scale;
    ImGuiStyle style;
    if (t == Theme::Dark)
        ImGui::StyleColorsDark(&style);
    else
        ImGui::StyleColorsLight(&style);

    // Material-style 8dp spacing grid with Blender's crisp, low rounding.
    style.WindowRounding = 4.0f;
    style.ChildRounding = 4.0f;
    style.FrameRounding = 4.0f;
    style.TabRounding = 4.0f;
    style.PopupRounding = 4.0f;
    style.GrabRounding = 4.0f;
    style.ScrollbarRounding = 6.0f;
    style.WindowPadding = ImVec2(8, 8);
    style.FramePadding = ImVec2(8, 3);
    style.ItemSpacing = ImVec2(8, 4);
    style.ItemInnerSpacing = ImVec2(6, 4);
    style.CellPadding = ImVec2(6, 2);
    style.IndentSpacing = 16.0f;
    style.ScrollbarSize = 12.0f;
    style.WindowBorderSize = 1.0f;
    style.ChildBorderSize = 1.0f;
    style.PopupBorderSize = 1.0f;
    style.FrameBorderSize = 1.0f;
    style.TabBarBorderSize = 1.0f;
    style.FontSizeBase = kBaseFontSize;
    style.ScaleAllSizes(scale);
    style.FontScaleMain = scale;

    // Blender 4.x's active-item orange, reused for both themes. The rest of the chrome (selection,
    // grabs, focus, links' neighbours) is neutral grey: no blue.
    const ImU32 accentOrange = rgb(255, 175, 41); // #ffaf29

    auto setColor = [&](ImGuiCol idx, ImU32 col) { style.Colors[idx] = ImGui::ColorConvertU32ToFloat4(col); };

    if (t == Theme::Dark) {
        setColor(ImGuiCol_Text, rgb(230, 230, 230));
        setColor(ImGuiCol_TextDisabled, rgb(140, 140, 140));
        setColor(ImGuiCol_WindowBg, rgb(48, 48, 48));
        setColor(ImGuiCol_ChildBg, rgb(43, 43, 43));
        setColor(ImGuiCol_PopupBg, rgb(24, 24, 24, 250));
        setColor(ImGuiCol_Border, rgb(61, 61, 61));
        setColor(ImGuiCol_BorderShadow, rgb(0, 0, 0, 0));
        setColor(ImGuiCol_FrameBg, rgb(29, 29, 29));
        setColor(ImGuiCol_FrameBgHovered, rgb(35, 35, 35));
        setColor(ImGuiCol_FrameBgActive, rgb(42, 42, 42));
        setColor(ImGuiCol_TitleBg, rgb(38, 38, 38));
        setColor(ImGuiCol_TitleBgActive, rgb(48, 48, 48));
        setColor(ImGuiCol_TitleBgCollapsed, rgb(32, 32, 32, 200));
        setColor(ImGuiCol_MenuBarBg, rgb(38, 38, 38));
        setColor(ImGuiCol_ScrollbarBg, rgb(43, 43, 43));
        setColor(ImGuiCol_ScrollbarGrab, rgb(92, 92, 92));
        setColor(ImGuiCol_ScrollbarGrabHovered, rgb(110, 110, 110));
        setColor(ImGuiCol_ScrollbarGrabActive, rgb(128, 128, 128));
        setColor(ImGuiCol_CheckMark, rgb(230, 230, 230));
        setColor(ImGuiCol_SliderGrab, rgb(150, 150, 150));
        setColor(ImGuiCol_SliderGrabActive, rgb(195, 195, 195));
        setColor(ImGuiCol_Button, rgb(84, 84, 84));
        setColor(ImGuiCol_ButtonHovered, rgb(101, 101, 101));
        setColor(ImGuiCol_ButtonActive, rgb(130, 130, 130)); // pressed: lighter grey, not blue
        // Header/hover/press are neutral Material state layers (open menus, plain rows); selectable()
        // paints selected rows with the stronger (still neutral) selection colours instead.
        setColor(ImGuiCol_Header, rgb(255, 255, 255, 34));
        setColor(ImGuiCol_HeaderHovered, rgb(255, 255, 255, 22));
        setColor(ImGuiCol_HeaderActive, rgb(255, 255, 255, 50));
        setColor(ImGuiCol_Separator, rgb(61, 61, 61));
        setColor(ImGuiCol_SeparatorHovered, rgb(120, 120, 120));
        setColor(ImGuiCol_SeparatorActive, rgb(165, 165, 165));
        setColor(ImGuiCol_ResizeGrip, rgb(61, 61, 61, 50));
        setColor(ImGuiCol_ResizeGripHovered, rgb(120, 120, 120));
        setColor(ImGuiCol_ResizeGripActive, rgb(165, 165, 165));
        setColor(ImGuiCol_InputTextCursor, rgb(230, 230, 230));
        setColor(ImGuiCol_Tab, rgb(35, 35, 35));
        setColor(ImGuiCol_TabHovered, rgb(72, 72, 72));
        setColor(ImGuiCol_TabSelected, rgb(61, 61, 61));
        setColor(ImGuiCol_TabSelectedOverline, accentOrange);
        setColor(ImGuiCol_TabDimmed, rgb(30, 30, 30));
        setColor(ImGuiCol_TabDimmedSelected, rgb(50, 50, 50));
        setColor(ImGuiCol_TabDimmedSelectedOverline, rgb(0, 0, 0, 0));
        setColor(ImGuiCol_DockingPreview, rgb(210, 210, 210, 100));
        setColor(ImGuiCol_DockingEmptyBg, rgb(48, 48, 48));
        setColor(ImGuiCol_PlotLines, rgb(170, 170, 170));
        setColor(ImGuiCol_PlotLinesHovered, rgb(215, 215, 215));
        setColor(ImGuiCol_PlotHistogram, accentOrange);
        setColor(ImGuiCol_PlotHistogramHovered, rgb(255, 195, 100));
        setColor(ImGuiCol_TableHeaderBg, rgb(38, 38, 38));
        setColor(ImGuiCol_TableBorderStrong, rgb(61, 61, 61));
        setColor(ImGuiCol_TableBorderLight, rgb(45, 45, 45));
        setColor(ImGuiCol_TableRowBg, rgb(0, 0, 0, 0));
        setColor(ImGuiCol_TableRowBgAlt, rgb(43, 43, 43));
        setColor(ImGuiCol_TextLink, accentOrange);
        setColor(ImGuiCol_TextSelectedBg, rgb(255, 255, 255, 70));
        setColor(ImGuiCol_TreeLines, rgb(61, 61, 61, 120));
        setColor(ImGuiCol_DragDropTarget, accentOrange);
        setColor(ImGuiCol_DragDropTargetBg, rgb(255, 175, 41, 50));
        setColor(ImGuiCol_UnsavedMarker, accentOrange);
        setColor(ImGuiCol_NavCursor, rgb(230, 230, 230));
        setColor(ImGuiCol_NavWindowingHighlight, rgb(230, 230, 230, 180));
        setColor(ImGuiCol_NavWindowingDimBg, rgb(0, 0, 0, 120));
        setColor(ImGuiCol_ModalWindowDimBg, rgb(0, 0, 0, 140));
    } else {
        setColor(ImGuiCol_Text, rgb(26, 26, 26));
        setColor(ImGuiCol_TextDisabled, rgb(120, 120, 120));
        setColor(ImGuiCol_WindowBg, rgb(188, 188, 188));
        setColor(ImGuiCol_ChildBg, rgb(212, 212, 212));
        setColor(ImGuiCol_PopupBg, rgb(235, 235, 235, 250));
        setColor(ImGuiCol_Border, rgb(140, 140, 140));
        setColor(ImGuiCol_BorderShadow, rgb(0, 0, 0, 0));
        setColor(ImGuiCol_FrameBg, rgb(230, 230, 230));
        setColor(ImGuiCol_FrameBgHovered, rgb(240, 240, 240));
        setColor(ImGuiCol_FrameBgActive, rgb(255, 255, 255));
        setColor(ImGuiCol_TitleBg, rgb(188, 188, 188));
        setColor(ImGuiCol_TitleBgActive, rgb(212, 212, 212));
        setColor(ImGuiCol_TitleBgCollapsed, rgb(180, 180, 180, 200));
        setColor(ImGuiCol_MenuBarBg, rgb(188, 188, 188));
        setColor(ImGuiCol_ScrollbarBg, rgb(212, 212, 212));
        setColor(ImGuiCol_ScrollbarGrab, rgb(160, 160, 160));
        setColor(ImGuiCol_ScrollbarGrabHovered, rgb(144, 144, 144));
        setColor(ImGuiCol_ScrollbarGrabActive, rgb(128, 128, 128));
        setColor(ImGuiCol_CheckMark, rgb(26, 26, 26));
        setColor(ImGuiCol_SliderGrab, rgb(110, 110, 110));
        setColor(ImGuiCol_SliderGrabActive, rgb(70, 70, 70));
        setColor(ImGuiCol_Button, rgb(230, 230, 230));
        setColor(ImGuiCol_ButtonHovered, rgb(240, 240, 240));
        setColor(ImGuiCol_ButtonActive, rgb(205, 205, 205)); // pressed: darker grey, not blue
        setColor(ImGuiCol_Header, rgb(0, 0, 0, 30));
        setColor(ImGuiCol_HeaderHovered, rgb(0, 0, 0, 18));
        setColor(ImGuiCol_HeaderActive, rgb(0, 0, 0, 45));
        setColor(ImGuiCol_Separator, rgb(160, 160, 160));
        setColor(ImGuiCol_SeparatorHovered, rgb(120, 120, 120));
        setColor(ImGuiCol_SeparatorActive, rgb(90, 90, 90));
        setColor(ImGuiCol_ResizeGrip, rgb(160, 160, 160, 60));
        setColor(ImGuiCol_ResizeGripHovered, rgb(120, 120, 120));
        setColor(ImGuiCol_ResizeGripActive, rgb(90, 90, 90));
        setColor(ImGuiCol_InputTextCursor, rgb(26, 26, 26));
        setColor(ImGuiCol_Tab, rgb(200, 200, 200));
        setColor(ImGuiCol_TabHovered, rgb(205, 205, 205));
        setColor(ImGuiCol_TabSelected, rgb(230, 230, 230));
        setColor(ImGuiCol_TabSelectedOverline, rgb(224, 140, 16));
        setColor(ImGuiCol_TabDimmed, rgb(205, 205, 205));
        setColor(ImGuiCol_TabDimmedSelected, rgb(220, 220, 220));
        setColor(ImGuiCol_TabDimmedSelectedOverline, rgb(0, 0, 0, 0));
        setColor(ImGuiCol_DockingPreview, rgb(50, 50, 50, 100));
        setColor(ImGuiCol_DockingEmptyBg, rgb(188, 188, 188));
        setColor(ImGuiCol_PlotLines, rgb(100, 100, 100));
        setColor(ImGuiCol_PlotLinesHovered, rgb(50, 50, 50));
        setColor(ImGuiCol_PlotHistogram, accentOrange);
        setColor(ImGuiCol_PlotHistogramHovered, rgb(255, 195, 100));
        setColor(ImGuiCol_TableHeaderBg, rgb(200, 200, 200));
        setColor(ImGuiCol_TableBorderStrong, rgb(160, 160, 160));
        setColor(ImGuiCol_TableBorderLight, rgb(180, 180, 180));
        setColor(ImGuiCol_TableRowBg, rgb(0, 0, 0, 0));
        setColor(ImGuiCol_TableRowBgAlt, rgb(212, 212, 212));
        setColor(ImGuiCol_TextLink, rgb(170, 100, 0));
        setColor(ImGuiCol_TextSelectedBg, rgb(0, 0, 0, 60));
        setColor(ImGuiCol_TreeLines, rgb(160, 160, 160, 120));
        setColor(ImGuiCol_DragDropTarget, accentOrange);
        setColor(ImGuiCol_DragDropTargetBg, rgb(255, 175, 41, 60));
        setColor(ImGuiCol_UnsavedMarker, accentOrange);
        setColor(ImGuiCol_NavCursor, rgb(26, 26, 26));
        setColor(ImGuiCol_NavWindowingHighlight, rgb(26, 26, 26, 150));
        setColor(ImGuiCol_NavWindowingDimBg, rgb(255, 255, 255, 120));
        setColor(ImGuiCol_ModalWindowDimBg, rgb(0, 0, 0, 90));
    }
    ImGui::GetStyle() = style;

    Palette& p = m_palette;
    if (t == Theme::Dark) {
        // Soft pastels: calm on the #2b2b2b backdrop, and a conflict (saturated red) still stands
        // out among them. Lane 0 (the main line) is blue; red is last.
        const ImU32 lanes[8] = {rgb(137, 180, 250), rgb(166, 218, 149), rgb(250, 179, 135), rgb(203, 166, 247),
            rgb(148, 226, 213), rgb(249, 226, 175), rgb(245, 194, 231), rgb(243, 139, 168)};
        std::copy(lanes, lanes + 8, p.lanes);
        p.branch = rgb(84, 104, 132); // muted slate: must not read as a selected (blue) item
        p.branchCurrent = rgb(255, 175, 41); // Blender's active-item orange for the current branch
        p.remote = rgb(63, 176, 176);
        p.tag = rgb(196, 140, 50);
        p.head = rgb(255, 175, 41); // same orange emphasis for a detached HEAD
        p.worktree = rgb(150, 110, 220);
        p.stash = rgb(120, 120, 120);
        p.badgeText = rgb(240, 240, 240);
        p.unpublished = rgb(255, 205, 130);
        p.conflict = rgb(255, 60, 90);
        p.conflictFill = rgb(190, 50, 60);
        p.sectionHeader = rgb(61, 61, 61);
        p.sectionHeaderHovered = rgb(72, 72, 72);
        p.sectionHeaderActive = rgb(84, 84, 84);
        p.added = rgb(139, 220, 0);
        p.removed = rgb(255, 90, 106);
        p.addedBg = rgb(47, 61, 39, 140);
        p.removedBg = rgb(67, 40, 43, 140);
        p.hunkHeader = rgb(120, 160, 220);
        p.lineNumber = rgb(120, 120, 120);
        p.dim = rgb(140, 140, 140);
        p.error = rgb(255, 255, 255);
        p.errorBg = rgb(160, 40, 50);
        p.warning = rgb(255, 175, 41);
        p.selection = rgb(255, 255, 255, 64);  // neutral: clearly above hover (22) and plain rows
        p.selectionHovered = rgb(255, 255, 255, 88);
        p.staged = rgb(139, 220, 0);
        p.unstaged = rgb(230, 175, 60);
        p.untracked = rgb(150, 160, 170);
    } else {
        // The same hues, deep enough to read on a light backdrop but still soft.
        const ImU32 lanes[8] = {rgb(86, 130, 210), rgb(96, 160, 100), rgb(215, 130, 80), rgb(140, 110, 210),
            rgb(60, 160, 150), rgb(185, 150, 50), rgb(205, 110, 170), rgb(210, 95, 110)};
        std::copy(lanes, lanes + 8, p.lanes);
        p.branch = rgb(84, 104, 132);
        p.branchCurrent = rgb(210, 130, 20);
        p.remote = rgb(20, 120, 120);
        p.tag = rgb(150, 110, 40);
        p.head = rgb(210, 130, 20);
        p.worktree = rgb(110, 70, 160);
        p.stash = rgb(110, 110, 110);
        p.badgeText = rgb(255, 255, 255);
        p.unpublished = rgb(170, 100, 10);
        p.conflict = rgb(200, 30, 45);
        p.conflictFill = rgb(190, 40, 50);
        p.sectionHeader = rgb(170, 170, 170);
        p.sectionHeaderHovered = rgb(160, 160, 160);
        p.sectionHeaderActive = rgb(150, 150, 150);
        p.added = rgb(50, 130, 10);
        p.removed = rgb(190, 40, 50);
        p.addedBg = rgb(210, 235, 195, 200);
        p.removedBg = rgb(245, 210, 205, 200);
        p.hunkHeader = rgb(40, 90, 170);
        p.lineNumber = rgb(130, 130, 130);
        p.dim = rgb(120, 120, 120);
        p.error = rgb(255, 255, 255);
        p.errorBg = rgb(190, 50, 55);
        p.warning = rgb(190, 120, 10);
        p.selection = rgb(0, 0, 0, 64);
        p.selectionHovered = rgb(0, 0, 0, 86);
        p.staged = rgb(50, 130, 10);
        p.unstaged = rgb(190, 120, 10);
        p.untracked = rgb(110, 120, 130);
    }
    p.branchText = p.branch;
    p.branchCurrentText = p.branchCurrent;
    p.remoteText = p.remote;
    p.tagText = p.tag;
    p.errorText = p.conflict;

    // Readable text: every colour drawn as text keeps its contrast against the backgrounds
    // text sits on (windows, and fields and table rows over them).
    const ImU32 window = ImGui::GetColorU32(ImGuiCol_WindowBg, 1.0f);
    const ImU32 backgrounds[] = {window, over(ImGui::GetColorU32(ImGuiCol_FrameBg), window),
        over(ImGui::GetColorU32(ImGuiCol_TableRowBgAlt), window), over(ImGui::GetColorU32(ImGuiCol_Header), window),
        over(ImGui::GetColorU32(ImGuiCol_PopupBg), window)};
    auto readable = [&](ImU32& c, float ratio) {
        for (int pass = 0; pass < 2; ++pass)
            for (ImU32 bg : backgrounds)
                c = readableOn(c, bg, ratio);
    };
    for (ImU32* c : {&p.branchText, &p.branchCurrentText, &p.remoteText, &p.tagText, &p.errorText, &p.unpublished,
             &p.conflict, &p.added, &p.removed, &p.hunkHeader, &p.warning, &p.staged, &p.unstaged})
        readable(*c, kTextContrast);
    for (ImU32* c : {&p.lineNumber, &p.dim, &p.untracked})
        readable(*c, kDimContrast);
    // Badge fills against their text.
    for (ImU32* c : {&p.branch, &p.branchCurrent, &p.remote, &p.tag, &p.head, &p.worktree, &p.stash, &p.conflictFill})
        *c = readableOn(*c, p.badgeText, kTextContrast);
    ImGuiStyle& live = ImGui::GetStyle();
    ImU32 disabled = ImGui::ColorConvertFloat4ToU32(live.Colors[ImGuiCol_TextDisabled]);
    readable(disabled, kDimContrast);
    live.Colors[ImGuiCol_TextDisabled] = ImGui::ColorConvertU32ToFloat4(disabled);
}

} // namespace ggui
