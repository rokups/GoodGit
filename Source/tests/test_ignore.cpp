// Ignore patterns (libgg/Ignore.hpp) and the Ignore submenu of the Changes file menu (§4.4).
#include "panels/ChangesPanel.hpp"
#include "shell/Session.hpp"
#include "tests/Harness.hpp"

#include <libgg/Ignore.hpp>

#include <algorithm>

namespace ggtest {

namespace {

std::string fileRef(Scenario& s, const char* group, const std::string& path)
{
    return s.child("//Changes", "##files") + "/" + group + "/" + Scenario::escapeRef(path) + "/###file_"
        + Scenario::escapeRef(path);
}

bool hasRow(Scenario& s, const std::string& path)
{
    const auto& rows = s.session()->changes().rows();
    return std::any_of(rows.begin(), rows.end(), [&](const auto& r) { return r.path == path; });
}

} // namespace

GG_TEST("ignore", "pattern builders escape special characters")
{
    GG_CHECK_STR_EQ(gg::ignore::anchored("a/b.txt"), "/a/b.txt");
    GG_CHECK_STR_EQ(gg::ignore::anchored("a[1]*?.txt"), "/a\\[1\\]\\*\\?.txt");
    GG_CHECK_STR_EQ(gg::ignore::anchored("#x"), "/#x");
    GG_CHECK_STR_EQ(gg::ignore::escape("#x"), "\\#x");
    GG_CHECK_STR_EQ(gg::ignore::escape("!x y "), "\\!x y\\ ");
    GG_CHECK_STR_EQ(gg::ignore::escape("a  "), "a\\ \\ ");
    GG_CHECK_STR_EQ(gg::ignore::extensionPattern("dir/file.tar.gz"), "*.gz");
    GG_CHECK_STR_EQ(gg::ignore::extensionPattern("dir/.env"), "");
    GG_CHECK_STR_EQ(gg::ignore::extensionPattern("Makefile"), "");
    GG_CHECK_STR_EQ(gg::ignore::extensionPattern("dir/a."), "");
    GG_CHECK_STR_EQ(gg::ignore::folderPattern("a/b/c.txt"), "/a/b/");
    GG_CHECK_STR_EQ(gg::ignore::folderPattern("c.txt"), "");
}

GG_TEST("ignore", "addLines creates the file, appends and skips lines that exist")
{
    const fs::path dir = s.root() / "ignore-lines";
    std::error_code ec;
    // A missing file and a missing folder are created.
    GG_CHECK(gg::ignore::addLines(dir / "info" / "exclude", {"/a", "/b", "/a"}, ec) == 2);
    GG_CHECK(!ec);
    GG_CHECK_STR_EQ(s.read(dir, "info/exclude"), "/a\n/b\n");
    // A file without a trailing newline gets one before the new line.
    s.write(dir, ".gitignore", "*.o\r\n/keep");
    GG_CHECK(gg::ignore::addLines(dir / ".gitignore", {"*.o", "/keep", "/new"}, ec) == 1);
    GG_CHECK_STR_EQ(s.read(dir, ".gitignore"), "*.o\r\n/keep\n/new\n");
    GG_CHECK(gg::ignore::addLines(dir / ".gitignore", {"/new"}, ec) == 0);
    GG_CHECK_STR_EQ(s.read(dir, ".gitignore"), "*.o\r\n/keep\n/new\n");
}

GG_TEST("ignore", "Ignore this file writes the .gitignore and the row disappears")
{
    const fs::path repo = s.fixture(Recipe::WorkingChanges);
    GG_REQUIRE(s.openRepository(repo));
    GG_REQUIRE(s.waitUntil([&] { return hasRow(s, "u.txt"); }));
    // A tracked row has no Ignore item.
    ctx->ItemClick(fileRef(s, "Unstaged", "b.txt").c_str(), ImGuiMouseButton_Right);
    ctx->Yield(2);
    GG_CHECK(ctx->ItemExists("//$FOCUSED/Stage"));
    GG_CHECK(!ctx->ItemExists("//$FOCUSED/Ignore/Ignore this file"));
    ctx->KeyPress(ImGuiKey_Escape);
    s.contextMenu(fileRef(s, "Untracked", "u.txt").c_str(), "Ignore/Ignore this file");
    GG_CHECK(s.waitUntil([&] { return !hasRow(s, "u.txt"); }, 20.0f));
    GG_CHECK_STR_EQ(s.read(repo, ".gitignore"), "/u.txt\n");
    s.settle();
    GG_CHECK(!ctx->ItemExists(fileRef(s, "Untracked", "u.txt").c_str()));
}

GG_TEST("ignore", "Ignore all by extension, folder and .git/info/exclude")
{
    const fs::path repo = s.fixture(Recipe::WorkingChanges);
    s.write(repo, "sub/x.log", "x\n");
    s.write(repo, "v.txt", "v\n");
    GG_REQUIRE(s.openRepository(repo));
    GG_REQUIRE(s.waitUntil([&] { return hasRow(s, "sub/x.log") && hasRow(s, "v.txt"); }));
    s.contextMenu(fileRef(s, "Untracked", "sub/x.log").c_str(), "Ignore/Ignore all *.log");
    GG_CHECK(s.waitUntil([&] { return !hasRow(s, "sub/x.log"); }, 20.0f));
    GG_CHECK_STR_EQ(s.read(repo, ".gitignore"), "*.log\n");
    s.write(repo, "sub/y.dat", "y\n");
    GG_REQUIRE(s.waitUntil([&] { return hasRow(s, "sub/y.dat"); }));
    s.contextMenu(fileRef(s, "Untracked", "sub/y.dat").c_str(), "Ignore/Ignore folder");
    GG_CHECK(s.waitUntil([&] { return !hasRow(s, "sub/y.dat"); }, 20.0f));
    GG_CHECK_STR_EQ(s.read(repo, ".gitignore"), "*.log\n/sub/\n");
    s.contextMenu(fileRef(s, "Untracked", "v.txt").c_str(), "Ignore/Add to .git\\/info\\/exclude");
    GG_CHECK(s.waitUntil([&] { return !hasRow(s, "v.txt"); }, 20.0f));
    GG_CHECK(s.read(repo, ".git/info/exclude").ends_with("/v.txt\n"));
}

} // namespace ggtest
