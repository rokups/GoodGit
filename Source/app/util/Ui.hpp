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
// Indeterminate spinner of the given radius.
void spinner(const char* id, float radius);
// Opens a directory or file with the desktop's default handler (off the UI thread).
void openInFileManager(const std::filesystem::path& path);
// Help marker "(?)" with a tooltip.
void helpMarker(const char* text);
// Formats "YYYY-MM-DD HH:MM".
std::string dateText(std::int64_t unixSeconds);

} // namespace ggui
