// The two status backends (`git status` and the libgit2 scan) must give the same StatusResult.
#include "core/Readers.hpp"
#include "shell/App.hpp"
#include "shell/Session.hpp"
#include "tests/Harness.hpp"

#include <libgg/GitRunner.hpp>

#include <algorithm>
#include <cstdlib>
#include <sstream>

namespace ggtest {

namespace {

using namespace ggui::core;

// One line that holds every compared field of an entry.
std::string describe(const StatusEntry& e)
{
    std::ostringstream o;
    o << "path='" << e.path << "' oldPath='" << e.oldPath << "' kind=" << static_cast<char>(e.kind)
      << " intentToAdd=" << e.intentToAdd << " submodule=" << e.submodule << " submoduleState=" << e.submoduleState
      << " stages=" << e.stage1 << e.stage2 << e.stage3 << " conflictDescription='" << e.conflictDescription
      << "' binary=" << e.binary;
    return o.str();
}

std::vector<std::string> describeAll(const std::vector<StatusEntry>& entries)
{
    std::vector<std::string> lines;
    for (const auto& e : entries)
        lines.push_back(describe(e));
    std::sort(lines.begin(), lines.end());
    return lines;
}

std::string listing(const std::vector<std::string>& lines)
{
    std::string out;
    for (const auto& l : lines)
        out += "    " + l + "\n";
    return out.empty() ? "    (none)\n" : out;
}

// Empty when both vectors hold the same entries (in any order). Otherwise a readable report.
std::string compareVector(const char* name, const std::vector<StatusEntry>& git, const std::vector<StatusEntry>& lg)
{
    const auto a = describeAll(git);
    const auto b = describeAll(lg);
    if (a == b)
        return {};
    return std::string(name) + ": git has " + std::to_string(a.size()) + " entries, libgit2 has "
        + std::to_string(b.size()) + "\n  git:\n" + listing(a) + "  libgit2:\n" + listing(b);
}

// Reads the status of `repo` with both backends and requires equal results.
// Returns the Git backend result so that the caller can check that the fixture holds what it should.
StatusPtr compareBackends(ImGuiTestContext* ctx, const fs::path& repo)
{
    const auto handle = gg::git2::openRepository(repo);
    const auto cancel = gg::CancelToken::none();
    const auto viaGit = readStatus(handle.get(), 1, cancel, nullptr, StatusBackend::Git);
    const auto viaLibgit2 = readStatus(handle.get(), 1, cancel, nullptr, StatusBackend::Libgit2);
    if (!viaGit || !viaLibgit2) {
        ctx->LogError("a status backend returned no result");
        return nullptr;
    }
    std::string report;
    report += compareVector("staged", viaGit->staged, viaLibgit2->staged);
    report += compareVector("unstaged", viaGit->unstaged, viaLibgit2->unstaged);
    report += compareVector("untracked", viaGit->untracked, viaLibgit2->untracked);
    report += compareVector("conflicted", viaGit->conflicted, viaLibgit2->conflicted);
    if (!report.empty())
        ctx->LogError("status backends differ:\n%s", report.c_str());
    GG_CHECK(report.empty());
    return viaGit;
}

bool has(const std::vector<StatusEntry>& entries, const std::string& path, ChangeKind kind)
{
    return std::any_of(entries.begin(), entries.end(),
        [&](const StatusEntry& e) { return e.path == path && e.kind == kind; });
}

} // namespace

GG_TEST("status_backends", "git and libgit2 status backends agree")
{
    // Fixture 1: many kinds of working tree change in one repository.
    {
        const fs::path repo = s.fixture(Recipe::Submodules);
        s.write(repo, "staged_modify.txt", "one\n");
        s.write(repo, "unstaged_modify.txt", "one\n");
        s.write(repo, "renamed_old.txt", "content that is long enough for rename detection\nsecond line\n");
        s.write(repo, "staged_delete.txt", "gone soon\n");
        s.write(repo, "unstaged_delete.txt", "gone later\n");
        s.write(repo, "type_change.txt", "plain file\n");
        s.git(repo, {"add", "."});
        s.git(repo, {"commit", "-q", "-m", "Base for the status test"});

        s.write(repo, "staged_add.txt", "new\n");
        s.git(repo, {"add", "staged_add.txt"});
        s.write(repo, "staged_modify.txt", "one\ntwo\n");
        s.git(repo, {"add", "staged_modify.txt"});
        s.write(repo, "unstaged_modify.txt", "one\ntwo\n");
        s.git(repo, {"mv", "renamed_old.txt", "renamed_new.txt"});
        s.git(repo, {"rm", "-q", "staged_delete.txt"});
        fs::remove(repo / "unstaged_delete.txt");
        s.write(repo, "nested/deeper/untracked.txt", "untracked\n");
        s.write(repo, "with space.txt", "untracked with a space\n");
        s.write(repo, "intent.txt", "intent to add\n");
        s.git(repo, {"add", "-N", "intent.txt"});
        fs::remove(repo / "type_change.txt");
        std::error_code ec;
        fs::create_symlink("unstaged_modify.txt", repo / "type_change.txt", ec);
        GG_CHECK(!ec);
        // The submodule: a new commit, a changed tracked file and an untracked file.
        s.commitFile(repo / "sub", "s4.txt", "s4\n", "Sub 4");
        s.write(repo / "sub", "s1.txt", "changed in the submodule\n");
        s.write(repo / "sub", "sub_untracked.txt", "untracked in the submodule\n");

        const auto status = compareBackends(ctx, repo);
        GG_REQUIRE(status);
        // The fixture must really hold the cases (a comparison of two empty results proves nothing).
        GG_CHECK(has(status->staged, "staged_add.txt", ChangeKind::Added));
        GG_CHECK(has(status->staged, "staged_modify.txt", ChangeKind::Modified));
        GG_CHECK(has(status->staged, "renamed_new.txt", ChangeKind::Renamed));
        GG_CHECK(has(status->staged, "staged_delete.txt", ChangeKind::Deleted));
        GG_CHECK(has(status->unstaged, "unstaged_modify.txt", ChangeKind::Modified));
        GG_CHECK(has(status->unstaged, "unstaged_delete.txt", ChangeKind::Deleted));
        GG_CHECK(has(status->unstaged, "intent.txt", ChangeKind::Added));
        GG_CHECK(has(status->unstaged, "type_change.txt", ChangeKind::TypeChanged));
        GG_CHECK(has(status->unstaged, "sub", ChangeKind::Modified));
        GG_CHECK(has(status->untracked, "nested/deeper/untracked.txt", ChangeKind::Untracked));
        GG_CHECK(has(status->untracked, "with space.txt", ChangeKind::Untracked));
    }

    // Fixture 2: a merge with conflicts of several kinds.
    {
        const fs::path repo = s.path("conflicts");
        fs::create_directories(repo);
        s.git(repo, {"init", "-q", "-b", "main"});
        s.track(repo);
        const std::string binaryBase("base\0bin\1", 9);
        const std::string binaryOurs("ours\0bin\2", 9);
        const std::string binaryTheirs("theirs\0bin\3", 11);
        s.write(repo, "uu.txt", "a\nb\nc\n");
        s.write(repo, "du.txt", "a\nb\nc\n");
        s.write(repo, "ud.txt", "a\nb\nc\n");
        s.write(repo, "bin.dat", binaryBase);
        s.git(repo, {"add", "."});
        s.git(repo, {"commit", "-q", "-m", "Base"});
        s.git(repo, {"branch", "theirs"});
        // main (ours)
        s.write(repo, "uu.txt", "a\nours\nc\n");
        s.git(repo, {"rm", "-q", "du.txt"});
        s.write(repo, "ud.txt", "a\nours\nc\n");
        s.write(repo, "bin.dat", binaryOurs);
        s.write(repo, "aa.txt", "added by ours\n");
        s.git(repo, {"add", "."});
        s.git(repo, {"commit", "-q", "-m", "Ours"});
        // theirs
        s.git(repo, {"switch", "-q", "theirs"});
        s.write(repo, "uu.txt", "a\ntheirs\nc\n");
        s.write(repo, "du.txt", "a\ntheirs\nc\n");
        s.git(repo, {"rm", "-q", "ud.txt"});
        s.write(repo, "bin.dat", binaryTheirs);
        s.write(repo, "aa.txt", "added by theirs\n");
        s.git(repo, {"add", "."});
        s.git(repo, {"commit", "-q", "-m", "Theirs"});
        s.git(repo, {"switch", "-q", "main"});
        s.gitMayFail(repo, {"merge", "theirs"});

        const auto status = compareBackends(ctx, repo);
        GG_REQUIRE(status);
        GG_CHECK(has(status->conflicted, "uu.txt", ChangeKind::Conflicted));
        GG_CHECK(has(status->conflicted, "du.txt", ChangeKind::Conflicted));
        GG_CHECK(has(status->conflicted, "ud.txt", ChangeKind::Conflicted));
        GG_CHECK(has(status->conflicted, "aa.txt", ChangeKind::Conflicted));
        GG_CHECK(has(status->conflicted, "bin.dat", ChangeKind::Conflicted));
        const auto bin = std::find_if(status->conflicted.begin(), status->conflicted.end(),
            [](const StatusEntry& e) { return e.path == "bin.dat"; });
        GG_CHECK(bin != status->conflicted.end() && bin->binary);
    }
}

GG_TEST("status_backends", "a deleted file and an intent-to-add copy of it give two backend-equal changes")
{
    // `git status --renames` pairs the deleted path with the intent-to-add path as an unstaged rename.
    const fs::path repo = s.fixture(Recipe::Linear);
    s.write(repo, "old.txt", "content that is long enough for rename detection\nsecond line\nthird line\n");
    s.git(repo, {"add", "old.txt"});
    s.git(repo, {"commit", "-q", "-m", "Add old.txt"});
    fs::remove(repo / "old.txt");
    s.write(repo, "new.txt", "content that is long enough for rename detection\nsecond line\nthird line\n");
    s.git(repo, {"add", "-N", "new.txt"});
    const auto status = compareBackends(ctx, repo);
    GG_REQUIRE(status);
    GG_CHECK(has(status->unstaged, "old.txt", ChangeKind::Deleted));
    GG_CHECK(has(status->unstaged, "new.txt", ChangeKind::Added));
    GG_CHECK(status->staged.empty());
}

GG_TEST("status_backends", "status run appears in the command log")
{
    // The libgit2 backend runs no `git status`: nothing to find.
    if (const char* backend = std::getenv("GG_STATUS_BACKEND"); backend && std::string(backend) == "libgit2")
        return;
    const fs::path repo = s.fixture(Recipe::WorkingChanges);
    GG_REQUIRE(s.openRepository(repo));
    GG_REQUIRE(s.settle());
    gg::clearCommandLog();
    s.session()->refresh();
    const auto found = [] {
        for (const auto& e : gg::commandLog())
            // The global option comes right after `git`.
            if (e.args.size() > 2 && e.args[1] == "--no-optional-locks" && e.args[2] == "status"
                && std::find(e.args.begin(), e.args.end(), "--porcelain=v2") != e.args.end())
                return true;
        return false;
    };
    GG_CHECK(s.waitUntil(found));
}

} // namespace ggtest
