#include "shell/Theme.hpp"

#include <imgui_internal.h>

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

} // namespace

ThemeManager& theme()
{
    static ThemeManager instance;
    return instance;
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
    style.WindowRounding = 3.0f;
    style.FrameRounding = 3.0f;
    style.TabRounding = 3.0f;
    style.PopupRounding = 3.0f;
    style.GrabRounding = 3.0f;
    style.ScrollbarRounding = 3.0f;
    style.WindowPadding = ImVec2(6, 6);
    style.FramePadding = ImVec2(6, 3);
    style.ItemSpacing = ImVec2(6, 4);
    style.CellPadding = ImVec2(4, 1);
    style.IndentSpacing = 14.0f;
    style.FontSizeBase = kBaseFontSize;
    style.ScaleAllSizes(scale);
    style.FontScaleMain = scale;
    ImGui::GetStyle() = style;

    Palette& p = m_palette;
    if (t == Theme::Dark) {
        const ImU32 lanes[8] = {rgb(86, 156, 214), rgb(214, 157, 86), rgb(106, 190, 106), rgb(204, 102, 204),
            rgb(220, 90, 90), rgb(90, 200, 200), rgb(200, 200, 90), rgb(160, 130, 230)};
        std::copy(lanes, lanes + 8, p.lanes);
        p.branch = rgb(46, 110, 60);
        p.branchCurrent = rgb(60, 150, 80);
        p.remote = rgb(60, 80, 130);
        p.tag = rgb(130, 100, 40);
        p.head = rgb(150, 60, 60);
        p.worktree = rgb(90, 70, 130);
        p.stash = rgb(90, 90, 90);
        p.badgeText = rgb(240, 240, 240);
        p.unpublished = rgb(255, 200, 120);
        p.conflict = rgb(255, 90, 90);
        p.added = rgb(120, 210, 120);
        p.removed = rgb(240, 120, 120);
        p.addedBg = rgb(40, 80, 40, 140);
        p.removedBg = rgb(90, 40, 40, 140);
        p.hunkHeader = rgb(120, 160, 220);
        p.lineNumber = rgb(120, 120, 120);
        p.dim = rgb(140, 140, 140);
        p.error = rgb(255, 255, 255);
        p.errorBg = rgb(150, 40, 40);
        p.warning = rgb(235, 175, 60);
        p.selection = rgb(60, 90, 140, 160);
        p.staged = rgb(120, 210, 120);
        p.unstaged = rgb(230, 190, 90);
        p.untracked = rgb(150, 150, 150);
    } else {
        const ImU32 lanes[8] = {rgb(30, 100, 180), rgb(190, 110, 20), rgb(40, 140, 40), rgb(160, 50, 160),
            rgb(190, 40, 40), rgb(20, 140, 140), rgb(140, 140, 20), rgb(110, 80, 190)};
        std::copy(lanes, lanes + 8, p.lanes);
        p.branch = rgb(70, 150, 90);
        p.branchCurrent = rgb(40, 130, 60);
        p.remote = rgb(80, 110, 180);
        p.tag = rgb(170, 130, 50);
        p.head = rgb(180, 70, 70);
        p.worktree = rgb(120, 90, 170);
        p.stash = rgb(120, 120, 120);
        p.badgeText = rgb(255, 255, 255);
        p.unpublished = rgb(170, 90, 0);
        p.conflict = rgb(200, 30, 30);
        p.added = rgb(20, 130, 20);
        p.removed = rgb(180, 30, 30);
        p.addedBg = rgb(200, 240, 200, 200);
        p.removedBg = rgb(250, 210, 210, 200);
        p.hunkHeader = rgb(40, 90, 170);
        p.lineNumber = rgb(140, 140, 140);
        p.dim = rgb(120, 120, 120);
        p.error = rgb(255, 255, 255);
        p.errorBg = rgb(190, 50, 50);
        p.warning = rgb(190, 120, 0);
        p.selection = rgb(170, 200, 240, 200);
        p.staged = rgb(20, 130, 20);
        p.unstaged = rgb(170, 110, 0);
        p.untracked = rgb(110, 110, 110);
    }
}

} // namespace ggui
