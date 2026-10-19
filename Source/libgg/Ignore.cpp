#include <libgg/Ignore.hpp>

#include <fstream>
#include <set>
#include <sstream>

namespace gg::ignore {

namespace fs = std::filesystem;

namespace {

// `lineStart`: the text is at the start of the pattern line, where # and ! are special.
std::string escapeText(const std::string& text, bool lineStart)
{
    std::string out;
    for (size_t i = 0; i < text.size(); ++i) {
        const char c = text[i];
        const bool special = c == '*' || c == '?' || c == '[' || c == ']' || c == '\\';
        const bool leading = lineStart && i == 0 && (c == '#' || c == '!');
        if (special || leading)
            out += '\\';
        out += c;
    }
    // A trailing space is dropped by git unless a backslash protects it.
    std::string tail;
    while (!out.empty() && out.back() == ' ') {
        out.pop_back();
        tail.insert(0, "\\ ");
    }
    return out + tail;
}

} // namespace

std::string escape(const std::string& text) { return escapeText(text, true); }

std::string anchored(const std::string& path) { return "/" + escapeText(path, false); }

std::string extensionPattern(const std::string& path)
{
    if (path.empty() || path.back() == '/')
        return {};
    const size_t slash = path.find_last_of('/');
    const std::string name = slash == std::string::npos ? path : path.substr(slash + 1);
    // A name like ".env" has no extension: the dot starts the name.
    const size_t dot = name.find_last_of('.');
    if (dot == std::string::npos || dot == 0 || dot + 1 == name.size())
        return {};
    return "*" + escapeText(name.substr(dot), false);
}

std::string folderPattern(const std::string& path)
{
    std::string p = path;
    while (!p.empty() && p.back() == '/')
        p.pop_back();
    const size_t slash = p.find_last_of('/');
    if (slash == std::string::npos || slash == 0)
        return {};
    return "/" + escapeText(p.substr(0, slash), false) + "/";
}

size_t addLines(const fs::path& file, const std::vector<std::string>& lines, std::error_code& ec)
{
    ec.clear();
    std::string content;
    if (fs::exists(file, ec)) {
        std::ifstream in(file, std::ios::binary);
        std::ostringstream ss;
        ss << in.rdbuf();
        content = ss.str();
    } else if (ec) {
        return 0;
    }
    std::set<std::string> have;
    std::istringstream existing(content);
    for (std::string line; std::getline(existing, line);) {
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        have.insert(line);
    }
    std::string add;
    size_t count = 0;
    for (const auto& line : lines) {
        if (line.empty() || !have.insert(line).second)
            continue;
        add += line + "\n";
        ++count;
    }
    if (count == 0)
        return 0;
    if (!content.empty() && content.back() != '\n')
        add.insert(0, "\n");
    if (file.has_parent_path()) {
        fs::create_directories(file.parent_path(), ec);
        if (ec)
            return 0;
    }
    std::ofstream out(file, std::ios::binary | std::ios::app);
    out << add;
    out.flush();
    if (!out) {
        ec = std::make_error_code(std::errc::io_error);
        return 0;
    }
    return count;
}

} // namespace gg::ignore
