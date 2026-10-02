// Syntax highlighting settings shared by the text editor views.
#pragma once

#include <TextEditor.h>

#include <string>

namespace ggui {

// The language of a file by its extension; null for files without syntax highlighting.
const TextEditor::Language* languageFor(const std::string& path);

// The text editor's palette for the current theme (whitespace is the theme's dim color).
TextEditor::Palette editorPalette();

} // namespace ggui
