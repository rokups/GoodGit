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
}

GG_TEST("winargs", "batch command line quotes every argument")
{
    std::string error;
    const std::string line = gg::batchShellArgs("C:\\My Tools\\code.cmd", {"--diff", "C:\\a b\\x.txt", "y"}, error);
    GG_CHECK_STR_EQ(line, "/d /e:on /v:off /s /c \"\"C:\\My Tools\\code.cmd\" \"--diff\" \"C:\\a b\\x.txt\" \"y\"\"");
    GG_CHECK(error.empty());
    GG_CHECK_STR_EQ(gg::batchShellArgs("a.bat", {}, error), "/d /e:on /v:off /s /c \"\"a.bat\"\"");
}

namespace {

// The encoded form of one argument inside the batch command line.
std::string encodeBatchArg(const std::string& arg, std::string& error)
{
    const std::string line = gg::batchShellArgs("a.cmd", {arg}, error);
    const std::string head = "/d /e:on /v:off /s /c \"\"a.cmd\" ";
    if (line.size() < head.size() + 1)
        return {};
    return line.substr(head.size(), line.size() - head.size() - 1);
}

} // namespace

GG_TEST("winargs", "batch command line encodes percent signs and quotes")
{
    std::string error;
    GG_CHECK_STR_EQ(encodeBatchArg("a\tb", error), "\"a\tb\""); // a tab is plain text, as in --format=%(refname)\t...
    GG_CHECK(error.empty());
    GG_CHECK_STR_EQ(encodeBatchArg("--format=%H", error), "\"--format=%%cd:~,%H\"");
    GG_CHECK_STR_EQ(encodeBatchArg("%(refname)", error), "\"%%cd:~,%(refname)\"");
    GG_CHECK_STR_EQ(encodeBatchArg("a\"b", error), "\"a\"\"b\"");
    GG_CHECK_STR_EQ(encodeBatchArg("a\\\"b", error), "\"a\\\\\"\"b\"");
    GG_CHECK_STR_EQ(encodeBatchArg("C:\\a\\", error), "\"C:\\a\\\\\"");
    GG_CHECK_STR_EQ(encodeBatchArg("C:\\a\\b", error), "\"C:\\a\\b\"");
    GG_CHECK_STR_EQ(encodeBatchArg("& | < > ^ !", error), "\"& | < > ^ !\"");
    GG_CHECK_STR_EQ(encodeBatchArg("", error), "\"\"");
    GG_CHECK(error.empty());
}

GG_TEST("winargs", "batch command line refuses control characters and bad script paths")
{
    for (const char* bad : {"a\nb", "a\rb", "a\x01"
                                                    "b"}) {
        std::string error;
        GG_CHECK(gg::batchShellArgs("a.cmd", {"ok", bad}, error).empty());
        GG_CHECK(error.find("cannot pass an argument with") == 0);
    }
    std::string error;
    GG_CHECK(gg::batchShellArgs("a.cmd", {std::string("a\0b", 3)}, error).empty());
    GG_CHECK(!error.empty());
    for (const char* path : {"C:\\50%\\a.cmd", "C:\\a\"b\\a.cmd", "a\n.cmd"}) {
        error.clear();
        GG_CHECK(gg::batchShellArgs(path, {}, error).empty());
        GG_CHECK(!error.empty());
    }
}

GG_TEST("winargs", "program candidates follow the extension order")
{
    const std::vector<std::string> bare{"code.exe", "code.cmd", "code.bat"};
    GG_CHECK(gg::programCandidates("code", true) == bare);
    const auto named = gg::programCandidates("code.CMD", true);
    GG_CHECK_EQ(named.size(), size_t(4));
    GG_CHECK_STR_EQ(named.back(), "code.CMD");
    GG_CHECK_STR_EQ(gg::programCandidates("tool.exe", true).back(), "tool.exe");
    GG_CHECK_EQ(gg::programCandidates("ggui", false).size(), size_t(1));
}

} // namespace ggtest
