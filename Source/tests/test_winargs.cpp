// Windows command lines: quoting, length limit, script checks (GG-132). The code is pure and runs on all platforms.
#include "libgg/GitRunner.hpp"
#include "libgg/Launch.hpp"
#include "tests/Harness.hpp"

#include <string>
#include <vector>

namespace ggtest {

namespace {

// Test-side parser for the CommandLineToArgvW rules.
std::vector<std::string> parseWindowsLine(const std::string& line)
{
    std::vector<std::string> out;
    size_t i = 0;
    while (true) {
        while (i < line.size() && line[i] == ' ')
            ++i;
        if (i >= line.size())
            return out;
        std::string arg;
        bool quoted = false;
        while (i < line.size() && (quoted || line[i] != ' ')) {
            size_t backslashes = 0;
            while (i < line.size() && line[i] == '\\') {
                ++backslashes;
                ++i;
            }
            if (i < line.size() && line[i] == '"') {
                arg.append(backslashes / 2, '\\');
                if (backslashes % 2) {
                    arg.push_back('"');
                } else {
                    quoted = !quoted;
                }
                ++i;
            } else {
                arg.append(backslashes, '\\');
                if (i < line.size() && (quoted || line[i] != ' ')) {
                    arg.push_back(line[i]);
                    ++i;
                }
            }
        }
        out.push_back(arg);
    }
}

} // namespace

GG_TEST("winargs", "quoteWindowsArg follows the CommandLineToArgvW rules")
{
    GG_CHECK_STR_EQ(gg::quoteWindowsArg(""), "\"\"");
    GG_CHECK_STR_EQ(gg::quoteWindowsArg("word"), "word");
    GG_CHECK_STR_EQ(gg::quoteWindowsArg("a b"), "\"a b\"");
    GG_CHECK_STR_EQ(gg::quoteWindowsArg("x\"y"), "\"x\\\"y\"");
    GG_CHECK_STR_EQ(gg::quoteWindowsArg("a\\"), "a\\");
    GG_CHECK_STR_EQ(gg::quoteWindowsArg("a b\\"), "\"a b\\\\\"");
    GG_CHECK_STR_EQ(gg::quoteWindowsArg("a\\\"b"), "\"a\\\\\\\"b\"");
}

GG_TEST("winargs", "quoted arguments parse back unchanged")
{
    const std::vector<std::string> args{"git", "", "a b", "x\"y", "a\\", "a b\\", "a\\\"b", "\\\\\"",
        "C:\\Program Files\\x\\", "plain", "\t", "a\\\\b c"};
    std::string line;
    for (const auto& a : args)
        line += (line.empty() ? "" : " ") + gg::quoteWindowsArg(a);
    const std::vector<std::string> back = parseWindowsLine(line);
    GG_CHECK(back == args);
}

GG_TEST("winargs", "command line limit is 32766 characters")
{
    GG_CHECK(gg::windowsCommandLineFits(0));
    GG_CHECK(gg::windowsCommandLineFits(32766));
    GG_CHECK(!gg::windowsCommandLineFits(32767));
    GG_CHECK(!gg::windowsCommandLineFits(40000));
}

GG_TEST("winargs", "script path and arguments are checked")
{
    GG_CHECK(gg::isSafeScriptPath("C:\\Users\\me\\upd.cmd"));
    GG_CHECK(!gg::isSafeScriptPath("C:\\Users\\50%off\\upd.cmd"));
    GG_CHECK(!gg::isSafeScriptPath("C:\\Users\\a\"b\\upd.cmd"));
    GG_CHECK(gg::isSafeScriptArg("12345"));
    for (const char* bad : {"", "a b", "a\"b", "a&b", "a%b", "a=b", "a,b", "a;b", "a^b"})
        GG_CHECK(!gg::isSafeScriptArg(bad));
}

GG_TEST("winargs", "batch file names are detected in any case")
{
    for (const char* name : {"a.cmd", "a.BAT", "x.Cmd", ".bat"})
        GG_CHECK(gg::isBatchFileName(name));
    for (const char* name : {"git", "git.exe", "cmd", "a.batx", "bat"})
        GG_CHECK(!gg::isBatchFileName(name));
    GG_CHECK(gg::findInPath("tool.cmd").empty());
}

} // namespace ggtest
