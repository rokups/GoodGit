// Small UI helpers shared by the shell and panels.
#pragma once

#include <core/Types.hpp>

#include <imgui.h>

#include <filesystem>
#include <string>

namespace ggui {

bool containsNoCase(const std::string& haystack, const std::string& needle);
// "main → origin/main ↑1 ↓2" style summary of a recent repository.
std::string summaryText(const core::RepoSummary& s);
// A disabled menu item with a tooltip explaining why.
void disabledMenuItem(const char* label, const char* reason, const char* shortcut = nullptr);
// A small coloured badge (label may contain ##id); hover and clicks go to what it sits on.
void drawBadge(const char* label, ImU32 color, bool outlined = false);
// Plain text registered as an item (tests find it; tooltips and context menus attach to it) with
// no hover or click highlight: for text whose click does nothing. label may contain ##id.
void plainText(const char* label);
// A full commit ID: the first `shortLen` characters in the text colour, the rest dimmed.
// Registered like plainText under `id` when given.
void idText(const std::string& hex, size_t shortLen, const char* id = nullptr);
// A tooltip that starts with a full commit ID (drawn as idText) followed by more lines.
void idTooltip(const std::string& hex, size_t shortLen, const std::string& rest);
// "Copy ID" copies the short ID, or the full ID while Shift is held.
void copyId(const std::string& shortId, const std::string& fullId);
// A "Copy ID"-style menu item that says so; returns true when clicked (after copying).
bool copyIdMenuItem(const char* label, const std::string& shortId, const std::string& fullId, bool enabled = true);
inline constexpr const char* kCopyIdHint = "Shift: full ID";
// Indeterminate spinner of the given radius.
void spinner(const char* id, float radius);
// Opens a directory or file with the desktop's default handler (off the UI thread).
void openInFileManager(const std::filesystem::path& path);
// Help marker "(?)" with a tooltip.
void helpMarker(const char* text);
// Formats "YYYY-MM-DD HH:MM".
std::string dateText(std::int64_t unixSeconds);

} // namespace ggui
