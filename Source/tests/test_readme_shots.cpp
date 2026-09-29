// Screenshots for the project README: a realistic small repository ("tinyedit", a terminal text
// editor) with merges, branches, tags and working-tree changes, shown in the main view, the row
// context menu, the interactive rebase panel, the Light theme and the side-by-side diff. Written
// to <artifacts>/screens/readme-*.png.
// Manual: not part of the suite, run it with --test=readme.
#include "panels/ChangesPanel.hpp"
#include "panels/DiffPanel.hpp"
#include "panels/HistoryPanel.hpp"
#include "panels/RebasePanel.hpp"
#include "shell/App.hpp"
#include "shell/Session.hpp"
#include "shell/Theme.hpp"
#include "tests/Harness.hpp"

namespace ggtest {

namespace {

void shot(Scenario& s, const std::string& name)
{
    s.ctx->Yield(3);
    s.screenshot(name);
}

struct Who {
    const char* name;
    const char* email;
};
constexpr Who kMaya{"Maya Chen", "maya@tinyedit.dev"};
constexpr Who kTomas{"Tomas Berg", "tomas@tinyedit.dev"};
constexpr Who kPriya{"Priya Nair", "priya@tinyedit.dev"};
constexpr Who kJonas{"Jonas Keller", "jonas@tinyedit.dev"};

// 2026-08-24T00:00:00Z: day 0 of the history.
constexpr long long kDay0 = 1787529600;

// Builds the tinyedit repository step by step with fixed authors and dates.
class Builder {
public:
    Builder(Scenario& s, fs::path repo) : m_s(s), m_repo(std::move(repo)) {}

    void put(const std::string& rel, const std::string& text) { m_s.write(m_repo, rel, text); }

    void sub(const std::string& rel, const std::string& from, const std::string& to)
    {
        std::string text = m_s.read(m_repo, rel);
        const size_t at = text.find(from);
        if (at == std::string::npos) {
            m_s.ctx->LogError("readme fixture: anchor not found in %s: %s", rel.c_str(), from.c_str());
            const bool found = false;
            IM_CHECK_NO_RET(found);
            return;
        }
        text.replace(at, from.size(), to);
        m_s.write(m_repo, rel, text);
    }

    void insertBefore(const std::string& rel, const std::string& anchor, const std::string& text)
    {
        sub(rel, anchor, text + anchor);
    }

    void append(const std::string& rel, const std::string& text) { put(rel, m_s.read(m_repo, rel) + text); }

    gg::RunResult run(const Who& who, int day, int hour, std::vector<std::string> args)
    {
        gg::RunRequest r;
        r.args.emplace_back("git");
        for (auto& a : args)
            r.args.push_back(std::move(a));
        r.cwd = m_repo;
        const std::string date = "@" + std::to_string(kDay0 + 86400LL * day + 3600LL * hour + 60LL * ((day * 7 + hour * 13) % 60)) + " +0000";
        r.env.emplace_back("GIT_AUTHOR_NAME", std::string(who.name));
        r.env.emplace_back("GIT_AUTHOR_EMAIL", std::string(who.email));
        r.env.emplace_back("GIT_COMMITTER_NAME", std::string(who.name));
        r.env.emplace_back("GIT_COMMITTER_EMAIL", std::string(who.email));
        r.env.emplace_back("GIT_AUTHOR_DATE", date);
        r.env.emplace_back("GIT_COMMITTER_DATE", date);
        auto result = gg::run(r);
        if (!result.ok()) {
            m_s.ctx->LogError("readme fixture step failed: %s", result.message().c_str());
            const bool ok = false;
            IM_CHECK_NO_RET(ok);
        }
        return result;
    }

    std::string commit(const Who& who, int day, int hour, const std::string& message)
    {
        run(who, day, hour, {"add", "-A"});
        run(who, day, hour, {"commit", "-q", "-m", message});
        return m_s.head(m_repo);
    }

    void merge(const Who& who, int day, int hour, const std::string& branch)
    {
        run(who, day, hour, {"merge", "-q", "--no-ff", "-m", "Merge branch '" + branch + "'", branch});
    }

    void tag(const Who& who, int day, int hour, const std::string& name)
    {
        run(who, day, hour, {"tag", "-a", "-m", "tinyedit " + name.substr(1), name});
    }

    void branch(const std::string& name, const std::string& from = "main")
    {
        m_s.git(m_repo, {"switch", "-q", "-c", name, from});
    }
    void switchTo(const std::string& name) { m_s.git(m_repo, {"switch", "-q", name}); }
    void deleteBranch(const std::string& name) { m_s.git(m_repo, {"branch", "-q", "-d", name}); }

private:
    Scenario& m_s;
    fs::path m_repo;
};

struct Tinyedit {
    fs::path path;
    std::string clampCommit;   // "Clamp cursor when deleting the last line": the commit shown in the shots
    std::string searchMerge;   // stays expanded
    std::string crlfMerge;     // stays collapsed
    std::string syntaxMerge;
    std::string undoTip;       // feature/undo-tree
    std::string undoFirst;     // first commit of feature/undo-tree
    std::string squashCommit;  // second commit of feature/undo-tree
    std::string mainTip;
};

Tinyedit buildTinyedit(Scenario& s)
{
    Tinyedit t;
    t.path = s.path("tinyedit");
    fs::create_directories(t.path);
    s.git(s.root(), {"init", "-q", "-b", "main", t.path.string()});
    Builder b(s, t.path);

    // ---- day 0..5: the basics -------------------------------------------------------------
    b.put(".gitignore", "build/\n.cache/\ncompile_commands.json\n");
    b.put("README.md", R"cpp(# tinyedit

A small terminal text editor written in C++20. It keeps the text in a gap buffer and only
redraws what changed.

## Building

    cmake -S . -B build
    cmake --build build

## Usage

    tinyedit [file]
)cpp");
    b.put("CMakeLists.txt", R"cpp(cmake_minimum_required(VERSION 3.20)
project(tinyedit VERSION 0.0.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)

file(GLOB SOURCES src/*.cpp)
add_executable(tinyedit ${SOURCES})
)cpp");
    b.put("src/main.cpp", R"cpp(#include <cstdio>

int main() {
    std::puts("tinyedit 0.0.1");
    return 0;
}
)cpp");
    b.commit(kMaya, 0, 10, "Initial commit");

    b.put("src/buffer.hpp", R"cpp(#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace tinyedit {

// A gap buffer: the text lives in one array with a movable hole at the cursor.
class Buffer {
public:
    explicit Buffer(std::size_t capacity = 256);

    void insert(char c);
    void erase_before();
    void move_to(std::size_t pos);

    std::size_t size() const { return data_.size() - (gap_end_ - gap_start_); }
    std::size_t cursor() const { return gap_start_; }
    char at(std::size_t pos) const;
    std::string str() const;

private:
    void grow(std::size_t min_gap);

    std::vector<char> data_;
    std::size_t gap_start_ = 0;
    std::size_t gap_end_ = 0;
};

}  // namespace tinyedit
)cpp");
    b.put("src/buffer.cpp", R"cpp(#include "buffer.hpp"

#include <algorithm>

namespace tinyedit {

Buffer::Buffer(std::size_t capacity) : data_(capacity), gap_end_(capacity) {}

void Buffer::grow(std::size_t min_gap) {
    const std::size_t gap = gap_end_ - gap_start_;
    if (gap >= min_gap) return;
    const std::size_t extra = std::max(min_gap, data_.size());
    data_.insert(data_.begin() + static_cast<std::ptrdiff_t>(gap_end_), extra, '\0');
    gap_end_ += extra;
}

void Buffer::insert(char c) {
    grow(1);
    data_[gap_start_++] = c;
}

void Buffer::erase_before() {
    if (gap_start_ > 0) --gap_start_;
}

void Buffer::move_to(std::size_t pos) {
    pos = std::min(pos, size());
    while (gap_start_ > pos) data_[--gap_end_] = data_[--gap_start_];
    while (gap_start_ < pos) data_[gap_start_++] = data_[gap_end_++];
}

char Buffer::at(std::size_t pos) const {
    return pos < gap_start_ ? data_[pos] : data_[pos + (gap_end_ - gap_start_)];
}

std::string Buffer::str() const {
    std::string out;
    out.reserve(size());
    for (std::size_t i = 0; i < size(); ++i) out.push_back(at(i));
    return out;
}

}  // namespace tinyedit
)cpp");
    b.commit(kMaya, 1, 14, "Add gap buffer");

    b.put("src/terminal.hpp", R"cpp(#pragma once

#include <termios.h>

namespace tinyedit {

enum class KeyCode {
    Char,
    Left,
    Right,
    Up,
    Down,
    Home,
    End,
    Backspace,
    CtrlQ,
    None,
};

struct Key {
    KeyCode code = KeyCode::None;
    char ch = 0;
};

// Puts the terminal into raw mode for as long as the object lives.
class Terminal {
public:
    Terminal();
    ~Terminal();

    Key read_key();
    int rows() const { return rows_; }
    int cols() const { return cols_; }

private:
    termios saved_{};
    int rows_ = 24;
    int cols_ = 80;
};

}  // namespace tinyedit
)cpp");
    b.put("src/terminal.cpp", R"cpp(#include "terminal.hpp"

#include <sys/ioctl.h>
#include <unistd.h>

namespace tinyedit {

Terminal::Terminal() {
    tcgetattr(STDIN_FILENO, &saved_);
    termios raw = saved_;
    raw.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
    raw.c_oflag &= ~OPOST;
    raw.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);
    raw.c_cc[VMIN] = 0;
    raw.c_cc[VTIME] = 1;
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);

    winsize ws{};
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_col > 0) {
        rows_ = ws.ws_row;
        cols_ = ws.ws_col;
    }
}

Terminal::~Terminal() {
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &saved_);
}

Key Terminal::read_key() {
    char c = 0;
    if (read(STDIN_FILENO, &c, 1) != 1) return {};
    if (c == 17) return {KeyCode::CtrlQ, c};
    if (c == 127) return {KeyCode::Backspace, c};
    if (c == '\x1b') {
        char seq[2] = {0, 0};
        if (read(STDIN_FILENO, &seq[0], 1) != 1 || read(STDIN_FILENO, &seq[1], 1) != 1) return {};
        if (seq[0] == '[') {
            switch (seq[1]) {
                case 'D': return {KeyCode::Left, 0};
                case 'C': return {KeyCode::Right, 0};
                case 'A': return {KeyCode::Up, 0};
                case 'B': return {KeyCode::Down, 0};
                case 'H': return {KeyCode::Home, 0};
                case 'F': return {KeyCode::End, 0};
            }
        }
        return {};
    }
    return {KeyCode::Char, c};
}

}  // namespace tinyedit
)cpp");
    b.commit(kMaya, 2, 16, "Add raw terminal mode wrapper");

    b.put("src/file_io.hpp", R"cpp(#pragma once

#include <string>

#include "buffer.hpp"

namespace tinyedit {

bool load_file(const std::string& path, Buffer& buf);
bool save_file(const std::string& path, const Buffer& buf);

}  // namespace tinyedit
)cpp");
    b.put("src/file_io.cpp", R"cpp(#include "file_io.hpp"

#include <fstream>
#include <iterator>

namespace tinyedit {

bool load_file(const std::string& path, Buffer& buf) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return false;
    const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    for (char c : text) buf.insert(c);
    buf.move_to(0);
    return true;
}

bool save_file(const std::string& path, const Buffer& buf) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) return false;
    out << buf.str();
    return static_cast<bool>(out);
}

}  // namespace tinyedit
)cpp");
    b.put("src/main.cpp", R"cpp(#include <cstdio>

#include "buffer.hpp"
#include "file_io.hpp"

int main(int argc, char** argv) {
    tinyedit::Buffer buf;
    if (argc > 1 && !tinyedit::load_file(argv[1], buf)) {
        std::fprintf(stderr, "tinyedit: cannot open %s\n", argv[1]);
        return 1;
    }
    std::printf("%zu bytes\n", buf.size());
    return 0;
}
)cpp");
    b.commit(kTomas, 3, 11, "Read and write files");

    b.put("src/render.hpp", R"cpp(#pragma once

#include <cstddef>

#include "buffer.hpp"
#include "terminal.hpp"

namespace tinyedit {

void render_viewport(const Buffer& buf, const Terminal& term, std::size_t top);

}  // namespace tinyedit
)cpp");
    b.put("src/render.cpp", R"cpp(#include "render.hpp"

#include <cstdio>
#include <string>

namespace tinyedit {

void render_viewport(const Buffer& buf, const Terminal& term, std::size_t top) {
    std::string out = "\x1b[H";
    std::size_t line = 0, shown = 0;
    std::string row;
    for (std::size_t i = 0; i < buf.size() && shown + 1 < static_cast<std::size_t>(term.rows()); ++i) {
        const char c = buf.at(i);
        if (c == '\n') {
            if (line++ >= top) {
                out += row + "\x1b[K\r\n";
                ++shown;
            }
            row.clear();
        } else if (line >= top) {
            row.push_back(c);
        }
    }
    out += row;
    out += "\x1b[K";
    std::fwrite(out.data(), 1, out.size(), stdout);
}

}  // namespace tinyedit
)cpp");
    b.put("src/main.cpp", R"cpp(#include <cstdio>

#include "buffer.hpp"
#include "file_io.hpp"
#include "render.hpp"
#include "terminal.hpp"

int main(int argc, char** argv) {
    tinyedit::Buffer buf;
    if (argc > 1 && !tinyedit::load_file(argv[1], buf)) {
        std::fprintf(stderr, "tinyedit: cannot open %s\n", argv[1]);
        return 1;
    }
    tinyedit::Terminal term;
    tinyedit::render_viewport(buf, term, 0);
    return 0;
}
)cpp");
    b.commit(kMaya, 4, 15, "Render text viewport");

    b.put("src/input.hpp", R"cpp(#pragma once

#include "buffer.hpp"
#include "terminal.hpp"

namespace tinyedit {

// Applies one key press to the buffer. Returns false when the user asked to quit.
bool handle_key(Buffer& buf, Key key);

}  // namespace tinyedit
)cpp");
    b.put("src/input.cpp", R"cpp(#include "input.hpp"

namespace tinyedit {

bool handle_key(Buffer& buf, Key key) {
    switch (key.code) {
        case KeyCode::Left:
            buf.move_left();
            break;
        case KeyCode::Right:
            buf.move_right();
            break;
        case KeyCode::Home:
            buf.move_home();
            break;
        case KeyCode::End:
            buf.move_end();
            break;
        case KeyCode::Backspace:
            buf.erase_before();
            break;
        case KeyCode::Char:
            buf.insert(key.ch);
            break;
        case KeyCode::CtrlQ:
            return false;
        default:
            break;
    }
    return true;
}

}  // namespace tinyedit
)cpp");
    b.sub("src/main.cpp", "    tinyedit::render_viewport(buf, term, 0);\n    return 0;\n",
        "    for (;;) {\n        tinyedit::render_viewport(buf, term, 0);\n        if (!tinyedit::handle_key(buf, term.read_key())) break;\n    }\n    return 0;\n");
    b.sub("src/main.cpp", "#include \"file_io.hpp\"\n", "#include \"file_io.hpp\"\n#include \"input.hpp\"\n");
    b.commit(kPriya, 5, 13, "Handle arrow keys and Home/End");

    // ---- fix/crlf, side by side with main ---------------------------------------------------
    b.branch("fix/crlf");
    b.sub("src/buffer.hpp", "// A gap buffer:", "enum class LineEnding { LF, CRLF };\n\n// A gap buffer:");
    b.sub("src/buffer.hpp", "    std::string str() const;\n",
        "    std::string str() const;\n\n    LineEnding line_ending() const { return ending_; }\n    void set_line_ending(LineEnding e) { ending_ = e; }\n");
    b.sub("src/buffer.hpp", "    std::size_t gap_end_ = 0;\n", "    std::size_t gap_end_ = 0;\n    LineEnding ending_ = LineEnding::LF;\n");
    b.sub("src/file_io.cpp", "    for (char c : text) buf.insert(c);\n",
        "    const bool crlf = text.find(\"\\r\\n\") != std::string::npos;\n    buf.set_line_ending(crlf ? LineEnding::CRLF : LineEnding::LF);\n    for (char c : text) buf.insert(c);\n");
    b.commit(kTomas, 6, 10, "Detect CRLF line endings on load");
    b.sub("src/render.cpp", "        } else if (line >= top) {\n",
        "        } else if (c == '\\r') {\n            continue;  // part of a CRLF line ending, not a column\n        } else if (line >= top) {\n");
    b.commit(kTomas, 7, 9, "Fix cursor jump on CRLF files");

    b.switchTo("main");
    b.sub("src/render.hpp", "void render_viewport(const Buffer& buf, const Terminal& term, std::size_t top);\n",
        "void render_viewport(const Buffer& buf, const Terminal& term, std::size_t top);\nvoid render_status(const Buffer& buf, const Terminal& term, const std::string& name);\n");
    b.sub("src/render.hpp", "#include <cstddef>\n", "#include <cstddef>\n#include <string>\n");
    b.insertBefore("src/render.cpp", "}  // namespace tinyedit", R"cpp(void render_status(const Buffer& buf, const Terminal& term, const std::string& name) {
    char text[128];
    std::snprintf(text, sizeof text, " %s  %zu bytes  pos %zu", name.c_str(), buf.size(), buf.cursor());
    std::string bar = "\x1b[" + std::to_string(term.rows()) + ";1H\x1b[7m" + text;
    bar.resize(static_cast<std::size_t>(term.cols()) + 4, ' ');
    bar += "\x1b[m";
    std::fwrite(bar.data(), 1, bar.size(), stdout);
}

)cpp");
    b.commit(kMaya, 7, 15, "Render status line");

    b.put("tests/buffer_test.cpp", R"cpp(#include <cassert>

#include "../src/buffer.hpp"

int main() {
    tinyedit::Buffer buf(4);
    for (char c : std::string("hello world")) buf.insert(c);
    assert(buf.str() == "hello world");

    buf.move_to(5);
    buf.insert(',');
    assert(buf.str() == "hello, world");

    buf.erase_before();
    assert(buf.str() == "hello world");
    return 0;
}
)cpp");
    b.append("CMakeLists.txt", "\nenable_testing()\nadd_executable(buffer_test tests/buffer_test.cpp src/buffer.cpp)\nadd_test(NAME buffer_test COMMAND buffer_test)\n");
    b.commit(kTomas, 8, 16, "Add buffer unit tests");

    b.merge(kMaya, 9, 10, "fix/crlf");
    t.crlfMerge = s.head(t.path);
    b.deleteBranch("fix/crlf");

    // ---- v0.1.0 -----------------------------------------------------------------------------
    b.sub("CMakeLists.txt", "VERSION 0.0.1", "VERSION 0.1.0");
    b.append("README.md", "\n## Status\n\nVersion 0.1.0: open, edit and save small text files.\n");
    b.commit(kMaya, 10, 17, "Bump version to 0.1.0");
    b.tag(kMaya, 10, 17, "v0.1.0");

    b.sub("src/terminal.hpp", "    CtrlQ,\n", "    CtrlQ,\n    CtrlS,\n");
    b.sub("src/terminal.cpp", "    if (c == 17) return {KeyCode::CtrlQ, c};\n",
        "    if (c == 17) return {KeyCode::CtrlQ, c};\n    if (c == 19) return {KeyCode::CtrlS, c};\n");
    b.sub("src/input.cpp", "        case KeyCode::CtrlQ:\n", "        case KeyCode::CtrlS:\n            save_current(buf);\n            break;\n        case KeyCode::CtrlQ:\n");
    b.commit(kPriya, 12, 11, "Add Ctrl+S save binding");

    b.sub("src/render.cpp", "void render_viewport(", R"cpp(// Line number, right-aligned in a four-column gutter.
static std::string format_gutter(std::size_t line) {
    char num[16];
    std::snprintf(num, sizeof num, "\x1b[90m%4zu\x1b[m ", line);
    return num;
}

void render_viewport()cpp");
    b.sub("src/render.cpp", "                out += row + \"\\x1b[K\\r\\n\";", "                out += format_gutter(line) + row + \"\\x1b[K\\r\\n\";");
    b.commit(kMaya, 13, 15, "Add line number gutter");

    b.append("README.md", R"cpp(
## Key bindings

| Key        | Action                     |
|------------|----------------------------|
| Arrows     | Move the cursor            |
| Home / End | Start / end of the line    |
| Ctrl+S     | Save                       |
| Ctrl+Q     | Quit                       |
)cpp");
    b.commit(kJonas, 14, 10, "Document key bindings in README");

    // ---- feature/search and feature/syntax-highlight in parallel ------------------------------
    b.branch("feature/search");
    b.put("src/search.hpp", R"cpp(#pragma once

#include <cstddef>
#include <optional>
#include <string>

#include "buffer.hpp"

namespace tinyedit {

// Finds the next occurrence of `needle` at or after `from`, wrapping around at the end.
std::optional<std::size_t> find_next(const Buffer& buf, const std::string& needle, std::size_t from);
bool in_match(const Buffer& buf, std::size_t pos);

}  // namespace tinyedit
)cpp");
    b.put("src/search.cpp", R"cpp(#include "search.hpp"

namespace tinyedit {

namespace {

bool matches_at(const Buffer& buf, const std::string& needle, std::size_t pos) {
    for (std::size_t i = 0; i < needle.size(); ++i) {
        if (buf.at(pos + i) != needle[i]) return false;
    }
    return true;
}

}  // namespace

std::optional<std::size_t> find_next(const Buffer& buf, const std::string& needle, std::size_t from) {
    if (needle.empty() || needle.size() > buf.size()) return std::nullopt;
    const std::size_t last = buf.size() - needle.size();
    for (std::size_t n = 0; n <= last; ++n) {
        const std::size_t pos = (from + n) % (last + 1);
        if (matches_at(buf, needle, pos)) return pos;
    }
    return std::nullopt;
}

}  // namespace tinyedit
)cpp");
    b.commit(kPriya, 15, 10, "Add substring search over the buffer");

    b.branch("feature/syntax-highlight");
    b.put("src/highlight.hpp", R"cpp(#pragma once

#include <string>

namespace tinyedit {

// Wraps keywords, numbers and comments in ANSI colour codes.
std::string colorize(const std::string& line);

}  // namespace tinyedit
)cpp");
    b.put("src/highlight.cpp", R"cpp(#include "highlight.hpp"

#include <array>
#include <cctype>
#include <string_view>

namespace tinyedit {

namespace {

constexpr std::array<std::string_view, 8> kKeywords = {
    "if", "else", "for", "while", "return", "class", "struct", "const",
};

bool is_keyword(std::string_view word) {
    for (auto k : kKeywords) {
        if (k == word) return true;
    }
    return false;
}

}  // namespace

std::string colorize(const std::string& line) {
    std::string out;
    std::size_t i = 0;
    while (i < line.size()) {
        if (std::isalpha(static_cast<unsigned char>(line[i])) || line[i] == '_') {
            std::size_t j = i;
            while (j < line.size() && (std::isalnum(static_cast<unsigned char>(line[j])) || line[j] == '_')) ++j;
            const std::string word = line.substr(i, j - i);
            out += is_keyword(word) ? "\x1b[35m" + word + "\x1b[m" : word;
            i = j;
        } else {
            out.push_back(line[i++]);
        }
    }
    return out;
}

}  // namespace tinyedit
)cpp");
    b.commit(kJonas, 15, 14, "Add tokenizer for C++ keywords");

    b.switchTo("feature/search");
    b.sub("src/render.hpp", "#include \"buffer.hpp\"\n", "#include \"buffer.hpp\"\n#include \"search.hpp\"\n");
    b.insertBefore("src/render.cpp", "void render_status(", R"cpp(// Matches are drawn in reverse video.
static std::string mark_match(char c, bool hit) {
    return hit ? std::string("\x1b[7m") + c + "\x1b[m" : std::string(1, c);
}

)cpp");
    b.sub("src/render.cpp", "            row.push_back(c);\n", "            row += mark_match(c, in_match(buf, i));\n");
    b.commit(kPriya, 16, 15, "Highlight search matches in the viewport");

    b.switchTo("feature/syntax-highlight");
    b.sub("src/render.cpp", "#include \"render.hpp\"\n", "#include \"render.hpp\"\n\n#include \"highlight.hpp\"\n");
    b.sub("src/render.cpp", "format_gutter(line) + row + ", "format_gutter(line) + colorize(row) + ");
    b.commit(kJonas, 17, 9, "Colorize tokens in the viewport");

    b.switchTo("feature/search");
    b.sub("src/terminal.hpp", "enum class KeyCode {\n    Char,\n", "enum class KeyCode {\n    Char,\n    CtrlF,\n");
    b.sub("src/terminal.cpp", "    if (c == 19)", "    if (c == 6) return {KeyCode::CtrlF, c};\n    if (c == 19)");
    b.sub("src/input.cpp", "        case KeyCode::Backspace:\n",
        "        case KeyCode::CtrlF:\n            search_prompt(buf);\n            break;\n        case KeyCode::Backspace:\n");
    b.commit(kPriya, 17, 16, "Add Ctrl+F prompt and n/N navigation");

    b.switchTo("main");
    b.sub("src/render.cpp", "    std::fwrite(out.data(), 1, out.size(), stdout);\n}\n\nvoid render_status(",
        "    static std::string last_frame;\n    if (out == last_frame) return;  // nothing changed: skip the redraw\n    last_frame = out;\n    std::fwrite(out.data(), 1, out.size(), stdout);\n}\n\nvoid render_status(");
    b.commit(kMaya, 18, 11, "Avoid flicker by skipping identical frames");

    b.merge(kMaya, 19, 10, "feature/search");
    t.searchMerge = s.head(t.path);
    b.deleteBranch("feature/search");

    b.switchTo("feature/syntax-highlight");
    b.sub("src/terminal.hpp", "    Backspace,\n", "    Backspace,\n    F2,\n");
    b.sub("src/input.cpp", "        case KeyCode::Char:\n", "        case KeyCode::F2:\n            toggle_highlighting();\n            break;\n        case KeyCode::Char:\n");
    b.commit(kJonas, 19, 15, "Toggle highlighting with F2");

    b.switchTo("main");
    b.sub("src/terminal.cpp", "Terminal::Terminal() {\n    tcgetattr", "static void on_winch(int) {\n    g_resized = 1;\n}\n\nTerminal::Terminal() {\n    signal(SIGWINCH, on_winch);\n    tcgetattr");
    b.sub("src/terminal.cpp", "#include <sys/ioctl.h>\n", "#include <csignal>\n#include <sys/ioctl.h>\n");
    b.sub("src/terminal.cpp", "namespace tinyedit {\n", "namespace tinyedit {\n\nstatic volatile std::sig_atomic_t g_resized = 0;\n");
    b.commit(kTomas, 20, 14, "Handle terminal resize");

    b.merge(kMaya, 21, 10, "feature/syntax-highlight");
    t.syntaxMerge = s.head(t.path);

    // ---- v0.2.0 -----------------------------------------------------------------------------
    b.sub("CMakeLists.txt", "VERSION 0.1.0", "VERSION 0.2.0");
    b.sub("README.md", "Version 0.1.0: open, edit and save small text files.",
        "Version 0.2.0: open, edit and save small text files, with incremental search and syntax highlighting.");
    b.commit(kMaya, 22, 12, "Bump version to 0.2.0");
    b.tag(kMaya, 22, 12, "v0.2.0");

    b.branch("feature/undo-tree");
    b.put("src/undo.hpp", R"cpp(#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace tinyedit {

// One reversible edit: `text` was inserted (or removed) at `pos`.
struct EditRecord {
    enum class Kind { Insert, Erase };
    Kind kind;
    std::size_t pos;
    std::string text;
};

}  // namespace tinyedit
)cpp");
    t.undoFirst = b.commit(kJonas, 22, 16, "Add undo record types");

    b.switchTo("main");
    b.sub("src/file_io.cpp", "    if (!in) return false;\n", "    if (!in) return false;\n    if (in.peek() == std::ifstream::traits_type::eof()) return true;  // empty file: nothing to load\n");
    b.commit(kTomas, 23, 10, "Fix crash when opening an empty file");

    b.switchTo("feature/undo-tree");
    b.put("src/undo.cpp", R"cpp(#include "undo.hpp"

namespace tinyedit {

// Consecutive single-character inserts merge into one undo step, like most editors do.
bool can_merge(const EditRecord& last, const EditRecord& next) {
    return last.kind == EditRecord::Kind::Insert && next.kind == EditRecord::Kind::Insert &&
           next.pos == last.pos + last.text.size() && next.text.size() == 1 && next.text != "\n";
}

}  // namespace tinyedit
)cpp");
    t.squashCommit = b.commit(kJonas, 24, 11, "Group consecutive typing into one undo step");

    b.switchTo("main");
    b.sub("src/buffer.hpp", "    void erase_before();\n", "    void erase_before();\n    void erase_at();\n");
    b.sub("src/buffer.cpp", "void Buffer::move_to(", "void Buffer::erase_at() {\n    if (gap_end_ < data_.size()) ++gap_end_;\n}\n\nvoid Buffer::move_to(");
    b.sub("src/buffer.hpp", "    void move_to(std::size_t pos);\n", "    void move_to(std::size_t pos);\n    void move_by(std::ptrdiff_t delta);  // clamped to the text\n");
    b.sub("src/buffer.cpp", "char Buffer::at(", "void Buffer::move_by(std::ptrdiff_t delta) {\n    const auto target = static_cast<std::ptrdiff_t>(gap_start_) + delta;\n    move_to(target < 0 ? 0 : static_cast<std::size_t>(target));\n}\n\nchar Buffer::at(");
    b.sub("src/input.cpp", "            buf.move_right();\n", "            buf.move_by(1);\n");
    b.sub("src/terminal.hpp", "    Backspace,\n", "    Backspace,\n    Delete,\n");
    b.sub("src/input.cpp", "        case KeyCode::Char:\n", "        case KeyCode::Delete:\n            buf.erase_at();\n            break;\n        case KeyCode::Char:\n");
    b.sub("tests/buffer_test.cpp", "    buf.erase_before();\n    assert(buf.str() == \"hello world\");\n",
        "    buf.erase_before();\n    assert(buf.str() == \"hello world\");\n\n    // Deleting the last character must leave the cursor inside the text.\n    buf.move_to(buf.size());\n    buf.erase_at();\n    buf.move_by(5);\n    assert(buf.cursor() <= buf.size());\n");
    t.clampCommit = b.commit(kPriya, 24, 15, "Clamp cursor when deleting the last line");

    b.switchTo("feature/undo-tree");
    b.sub("src/undo.hpp", "}  // namespace tinyedit", R"cpp(// Undo history as a tree: undoing and then typing starts a new branch instead of
// throwing the undone edits away.
struct UndoNode {
    EditRecord edit;
    int parent = -1;
    std::vector<int> children;
};

}  // namespace tinyedit)cpp");
    b.commit(kJonas, 25, 12, "Add branching undo tree");

    b.switchTo("main");
    b.sub("src/render.cpp", "        } else if (c == '\\r') {\n",
        "        } else if (c == '\\t') {\n            row.append(4, ' ');  // tabs are four columns wide\n        } else if (c == '\\r') {\n");
    b.commit(kMaya, 26, 10, "Expand tabs to spaces when rendering");
    t.mainTip = s.head(t.path);

    b.switchTo("feature/undo-tree");
    b.sub("src/undo.cpp", "}  // namespace tinyedit", "// TODO: hook up Ctrl+Z / Ctrl+Y and walk the tree.\nvoid undo_step(UndoNode*) {}\n\n}  // namespace tinyedit");
    t.undoTip = b.commit(kJonas, 26, 17, "WIP: wire undo tree to Ctrl+Z");
    b.switchTo("main");

    // ---- origin: history up to v0.2.0 and the merged branches is published ------------------
    const fs::path bare = s.path("tinyedit-origin.git");
    s.git(s.root(), {"init", "-q", "--bare", "-b", "main", bare.string()});
    s.git(t.path, {"remote", "add", "origin", bare.string()});
    s.git(t.path, {"push", "-q", "origin", s.revParse(t.path, "v0.2.0^{commit}") + ":refs/heads/main",
                      "refs/heads/feature/syntax-highlight", "refs/tags/v0.1.0", "refs/tags/v0.2.0"});
    s.git(t.path, {"branch", "-q", "--set-upstream-to=origin/main", "main"});
    s.git(t.path, {"branch", "-q", "--set-upstream-to=origin/feature/syntax-highlight", "feature/syntax-highlight"});
    // Show a plausible URL in the Remotes panel rather than the temp path (nothing fetches).
    s.git(t.path, {"remote", "set-url", "origin", "https://github.com/tinyedit/tinyedit.git"});

    // ---- working tree: one staged, one unstaged change --------------------------------------
    b.sub("src/render.cpp", "\" %s  %zu bytes  pos %zu\", name.c_str(), buf.size(), buf.cursor());",
        "\" %s  %zu bytes  Ln %zu, Col %zu\", name.c_str(), buf.size(), buf.line() + 1, buf.column() + 1);");
    s.git(t.path, {"add", "src/render.cpp"});
    b.sub("src/input.cpp", "        case KeyCode::Home:\n",
        "        case KeyCode::PageUp:\n            buf.move_to(page_up(buf, buf.cursor()));\n            break;\n        case KeyCode::PageDown:\n            buf.move_to(page_down(buf, buf.cursor()));\n            break;\n        case KeyCode::Home:\n");
    s.track(t.path);
    return t;
}

std::string rowRef(const std::string& hex) { return "//History/**/###row_" + hex; }

bool selectCommit(Scenario& s, const std::string& hex)
{
    const std::string row = rowRef(hex);
    if (!s.waitUntil([&] { return s.itemExists(row.c_str()); }))
        return false;
    for (int attempt = 0; attempt < 3; ++attempt) {
        s.ctx->ItemClick(row.c_str());
        if (s.waitUntil([&] { return s.session()->selection().id.hex() == hex; }, 3.0f))
            return true;
    }
    return false;
}

void setTheme(Scenario& s, const char* theme)
{
    s.app.openSettings();
    s.ctx->Yield(2);
    s.ctx->ItemClick("//Settings/##settings_tabs/General");
    s.ctx->SetRef("Settings");
    s.comboSelect("//Settings/##settings_tabs/General/Theme##theme", theme);
    s.ctx->SetRef("");
    s.ctx->Yield(2);
    s.ctx->WindowClose("//Settings");
    s.ctx->Yield(3);
}

} // namespace

GG_MANUAL_TEST("readme", "screenshots of a realistic repository for the README", "HIST-GRAPH")
{
    const Tinyedit t = buildTinyedit(s);
    GG_REQUIRE(s.openRepository(t.path));
    GG_REQUIRE(s.waitUntil([&] { return s.session()->changes().rows().size() == 2; }));
    GG_REQUIRE(s.waitUntil([&] { return s.itemExists(rowRef(t.clampCommit).c_str()); }));
    // One merge expanded (its side history visible), the others collapsed.
    GG_REQUIRE(s.expandMerge(t.searchMerge));
    s.settle();

    const std::string diffFile = s.child("//Changes", "##files") + "/" + Scenario::escapeRef("src/input.cpp")
        + "/###file_" + Scenario::escapeRef("src/input.cpp");
    auto showMain = [&] {
        GG_REQUIRE(selectCommit(s, t.clampCommit));
        s.settle();
        GG_REQUIRE(s.waitUntil([&] { return s.itemExists(diffFile.c_str()); }));
        ctx->ItemClick(diffFile.c_str());
        s.showPanel("Diff");
        s.settle();
        GG_REQUIRE(s.waitUntil([&] {
            const auto& d = s.session()->diff().diff();
            return d && !d->files.empty();
        }));
        ctx->MouseMove("//##Toolbar");
        ctx->Yield(5);
    };

    // 1. Main view, dark.
    showMain();
    shot(s, "readme-main");

    // 2. Row context menu.
    ctx->ItemClick(rowRef(t.clampCommit).c_str(), ImGuiMouseButton_Right);
    ctx->Yield(5);
    shot(s, "readme-context-menu");
    ctx->PopupCloseAll();
    ctx->Yield(3);

    // 4. Light theme, same view.
    setTheme(s, "Light");
    showMain();
    shot(s, "readme-light");
    setTheme(s, "Dark");

    // 3. Interactive rebase of the unmerged branch.
    GG_REQUIRE(selectCommit(s, t.undoFirst));
    ctx->KeyPress(ImGuiKey_I);
    GG_REQUIRE(s.waitUntil([&] { return s.session()->rebase().isOpen() && s.session()->rebase().context() != nullptr; }));
    s.comboSelect(("//Interactive rebase/**/###ir_action_" + t.squashCommit).c_str(), "fixup");
    s.settle();
    ctx->MouseMove("//##Toolbar");
    ctx->Yield(5);
    shot(s, "readme-rebase");
    ctx->ItemClick("//Interactive rebase/###ir_cancel");
    ctx->Yield(3);

    // 5. Side-by-side diff, with the Diff panel widened (dragging the splitter next to it).
    showMain();
    s.comboSelect("//Diff/##diff_view", "Side by side");
    const ImVec2 origin = ImGui::GetMainViewport()->Pos;
    ctx->MouseMoveToPos(ImVec2(origin.x + 1074, origin.y + 400));
    ctx->MouseDown(0);
    ctx->MouseMoveToPos(ImVec2(origin.x + 870, origin.y + 400));
    ctx->MouseUp(0);
    s.settle();
    ctx->MouseMove("//##Toolbar");
    ctx->Yield(5);
    shot(s, "readme-diff-sbs");
}

} // namespace ggtest
