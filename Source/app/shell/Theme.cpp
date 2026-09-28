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
    p.branchText = p.branch;
    p.branchCurrentText = p.branchCurrent;
    p.remoteText = p.remote;
    p.tagText = p.tag;
    p.errorText = p.conflict;

    // Readable text (UF-50): every colour drawn as text keeps its contrast against the backgrounds
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
    for (ImU32* c : {&p.branch, &p.branchCurrent, &p.remote, &p.tag, &p.head, &p.worktree, &p.stash})
        *c = readableOn(*c, p.badgeText, kTextContrast);
    ImGuiStyle& live = ImGui::GetStyle();
    ImU32 disabled = ImGui::ColorConvertFloat4ToU32(live.Colors[ImGuiCol_TextDisabled]);
    readable(disabled, kDimContrast);
    live.Colors[ImGuiCol_TextDisabled] = ImGui::ColorConvertU32ToFloat4(disabled);
}

} // namespace ggui
