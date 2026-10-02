#include "util/SyntaxHighlight.hpp"

#include "shell/Theme.hpp"

#include <algorithm>
#include <filesystem>

namespace ggui {

namespace fs = std::filesystem;

const TextEditor::Language* languageFor(const std::string& path)
{
    using L = TextEditor::Language;
    static const std::pair<const char*, const L* (*)()> kByExtension[] = {
        {".c", L::C},        {".cc", L::Cpp},     {".cpp", L::Cpp},     {".cxx", L::Cpp},        {".h", L::Cpp},
        {".hh", L::Cpp},     {".hpp", L::Cpp},    {".hxx", L::Cpp},     {".inl", L::Cpp},        {".cs", L::Cs},
        {".lua", L::Lua},    {".py", L::Python},  {".glsl", L::Glsl},   {".vert", L::Glsl},      {".frag", L::Glsl},
        {".hlsl", L::Hlsl},  {".json", L::Json},  {".md", L::Markdown}, {".markdown", L::Markdown}, {".sql", L::Sql},
    };
    std::string ext = fs::path(path).extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    for (const auto& [e, language] : kByExtension)
        if (ext == e)
            return language();
    return nullptr;
}

TextEditor::Palette editorPalette()
{
    auto palette = theme().theme() == Theme::Light ? TextEditor::GetLightPalette() : TextEditor::GetDarkPalette();
    palette[static_cast<size_t>(TextEditor::Color::whitespace)] = theme().palette().dim; // hunk rows, dimmed
    return palette;
}

} // namespace ggui
