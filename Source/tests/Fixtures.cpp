// Scripted repository recipes built with plain git (product spec §8.3).
#include "tests/Harness.hpp"

namespace ggtest {

const char* recipeName(Recipe r)
{
    switch (r) {
    case Recipe::Empty: return "empty";
    case Recipe::Linear: return "linear";
    case Recipe::Merges: return "merges";
    case Recipe::ManyRefs: return "many-refs";
    case Recipe::LinkedWorktrees: return "worktrees";
    case Recipe::WithRemote: return "with-remote";
    case Recipe::Bare: return "bare";
    case Recipe::Sha256: return "sha256";
    case Recipe::Unborn: return "unborn";
    case Recipe::MidMerge: return "mid-merge";
    case Recipe::MidRebase: return "mid-rebase";
    case Recipe::MidRebaseApply: return "mid-rebase-apply";
    case Recipe::MidCherryPick: return "mid-cherry-pick";
    case Recipe::MidRevert: return "mid-revert";
    case Recipe::Bisecting: return "bisecting";
    case Recipe::Submodules: return "submodules";
    case Recipe::Lfs: return "lfs";
    case Recipe::TextEdgeCases: return "text-edge-cases";
    case Recipe::Conflicted2: return "conflicted-2";
    case Recipe::ConflictedN: return "conflicted-n";
    case Recipe::WorkingChanges: return "working-changes";
    case Recipe::Stashes: return "stashes";
    }
    return "?";
}

std::vector<Recipe> allRecipes()
{
    return {Recipe::Empty, Recipe::Linear, Recipe::Merges, Recipe::ManyRefs, Recipe::LinkedWorktrees,
        Recipe::WithRemote, Recipe::Bare, Recipe::Sha256, Recipe::Unborn, Recipe::MidMerge, Recipe::MidRebase,
        Recipe::MidRebaseApply, Recipe::MidCherryPick, Recipe::MidRevert, Recipe::Bisecting, Recipe::Submodules,
        Recipe::Lfs, Recipe::TextEdgeCases, Recipe::Conflicted2, Recipe::ConflictedN, Recipe::WorkingChanges,
        Recipe::Stashes};
}

namespace {

void linear(Scenario& s, const fs::path& repo, int count, const std::string& prefix = "f")
{
    for (int i = 1; i <= count; ++i)
        s.commitFile(repo, prefix + std::to_string(i) + ".txt", "line " + std::to_string(i) + "\n",
            "Add " + prefix + std::to_string(i));
}

// base: f.txt = "a\nb\nc\n"; branch "ours" changes b→ours, branch "theirs" changes b→theirs.
void conflictSetup(Scenario& s, const fs::path& repo)
{
    s.commitFile(repo, "f.txt", "a\nb\nc\n", "Base");
    s.git(repo, {"branch", "theirs"});
    s.commitFile(repo, "f.txt", "a\nours\nc\n", "Ours");
    s.git(repo, {"switch", "-q", "theirs"});
    s.commitFile(repo, "f.txt", "a\ntheirs\nc\n", "Theirs");
    s.git(repo, {"switch", "-q", "main"});
}

} // namespace

fs::path Scenario::fixture(Recipe recipe, const std::string& name)
{
    const fs::path repo = m_root / (name.empty() ? std::string(recipeName(recipe)) : name);
    fs::create_directories(repo);
    auto init = [&](std::vector<std::string> extra = {}) {
        std::vector<std::string> args{"init", "-q", "-b", "main"};
        args.insert(args.end(), extra.begin(), extra.end());
        args.push_back(repo.string());
        git(m_root, args);
    };
    switch (recipe) {
    case Recipe::Empty:
        init();
        break;
    case Recipe::Unborn:
        init();
        write(repo, "untracked.txt", "hello\n");
        break;
    case Recipe::Linear:
        init();
        linear(*this, repo, 5);
        break;
    case Recipe::Merges:
        init();
        commitFile(repo, "base.txt", "base\n", "Base");
        commitFile(repo, "main1.txt", "m1\n", "Main 1");
        git(repo, {"switch", "-q", "-c", "feature", "HEAD~1"});
        commitFile(repo, "feature1.txt", "f1\n", "Feature 1");
        commitFile(repo, "feature2.txt", "f2\n", "Feature 2");
        git(repo, {"switch", "-q", "main"});
        commitFile(repo, "main2.txt", "m2\n", "Main 2");
        git(repo, {"merge", "-q", "--no-ff", "-m", "Merge feature", "feature"});
        git(repo, {"switch", "-q", "-c", "topic", "HEAD~1"});
        commitFile(repo, "topic.txt", "t\n", "Topic");
        git(repo, {"switch", "-q", "main"});
        break;
    case Recipe::ManyRefs: {
        init();
        linear(*this, repo, 20);
        std::string stdinText;
        for (int i = 0; i < 200; ++i)
            stdinText += "create refs/heads/branch-" + std::to_string(i) + " HEAD~" + std::to_string(i % 20) + "\n";
        // update-ref --stdin needs object ids, resolve first.
        std::string resolved;
        for (int i = 0; i < 20; ++i)
            resolved += revParse(repo, "HEAD~" + std::to_string(i)) + "\n";
        const auto ids = gg::splitLines(resolved);
        stdinText.clear();
        for (int i = 0; i < 200; ++i)
            stdinText += "create refs/heads/branch-" + std::to_string(i) + " " + ids[static_cast<size_t>(i % 20)] + "\n";
        for (int i = 0; i < 50; ++i)
            stdinText += "create refs/tags/light-" + std::to_string(i) + " " + ids[static_cast<size_t>(i % 20)] + "\n";
        git(repo, {"update-ref", "--stdin"}, stdinText);
        for (int i = 0; i < 50; ++i)
            git(repo, {"tag", "-a", "-m", "Annotated " + std::to_string(i), "annotated-" + std::to_string(i),
                          ids[static_cast<size_t>(i % 20)]});
        break;
    }
    case Recipe::LinkedWorktrees: {
        init();
        linear(*this, repo, 3);
        const fs::path wt1 = m_root / (repo.filename().string() + "-wt1");
        const fs::path wt2 = m_root / (repo.filename().string() + "-wt2");
        const fs::path wt3 = m_root / (repo.filename().string() + "-wt3");
        git(repo, {"worktree", "add", "-q", "-b", "wt1", wt1.string()});
        commitFile(wt1, "wt1.txt", "wt1\n", "Worktree 1 commit");
        git(repo, {"worktree", "add", "-q", "-b", "wt2", wt2.string()});
        git(repo, {"worktree", "lock", "--reason", "test lock", wt2.string()});
        git(repo, {"worktree", "add", "-q", "--detach", wt3.string(), "HEAD~1"});
        removeAll(wt3); // stale
        track(wt1);
        break;
    }
    case Recipe::WithRemote: {
        const fs::path origin = m_root / (repo.filename().string() + "-origin.git");
        const fs::path other = m_root / (repo.filename().string() + "-other");
        git(m_root, {"init", "-q", "--bare", "-b", "main", origin.string()});
        git(m_root, {"clone", "-q", "file://" + origin.generic_string(), other.string()});
        linear(*this, other, 3, "o");
        git(other, {"push", "-q", "origin", "main"});
        removeAll(repo);
        git(m_root, {"clone", "-q", "file://" + origin.generic_string(), repo.string()});
        commitFile(other, "remote-only.txt", "r\n", "Remote only");
        git(other, {"push", "-q", "origin", "main"});
        commitFile(repo, "local-only.txt", "l\n", "Local only");
        git(repo, {"fetch", "-q", "origin"});
        track(origin);
        break;
    }
    case Recipe::Bare: {
        const fs::path src = m_root / (repo.filename().string() + "-src");
        fs::create_directories(src);
        git(m_root, {"init", "-q", "-b", "main", src.string()});
        linear(*this, src, 4);
        removeAll(repo);
        git(m_root, {"clone", "-q", "--bare", src.string(), repo.string()});
        break;
    }
    case Recipe::Sha256:
        init({"--object-format=sha256"});
        linear(*this, repo, 4);
        git(repo, {"tag", "v1", "HEAD~1"});
        break;
    case Recipe::MidMerge:
        init();
        conflictSetup(*this, repo);
        gitMayFail(repo, {"merge", "theirs"});
        break;
    case Recipe::MidRebase: {
        init();
        conflictSetup(*this, repo);
        gg::RunRequest r;
        r.args = {"git", "rebase", "-i", "theirs"};
        r.cwd = repo;
        r.env.emplace_back("GIT_SEQUENCE_EDITOR", "true");
        gg::run(r);
        break;
    }
    case Recipe::MidRebaseApply:
        init();
        conflictSetup(*this, repo);
        gitMayFail(repo, {"rebase", "--apply", "theirs"});
        break;
    case Recipe::MidCherryPick:
        init();
        conflictSetup(*this, repo);
        gitMayFail(repo, {"cherry-pick", "theirs"});
        break;
    case Recipe::MidRevert:
        init();
        commitFile(repo, "f.txt", "1\n", "One");
        commitFile(repo, "f.txt", "2\n", "Two");
        commitFile(repo, "f.txt", "3\n", "Three");
        gitMayFail(repo, {"revert", "--no-edit", "HEAD~1"});
        break;
    case Recipe::Bisecting:
        init();
        linear(*this, repo, 6);
        git(repo, {"bisect", "start", "HEAD", "HEAD~5"});
        break;
    case Recipe::Submodules: {
        const fs::path sub = m_root / (repo.filename().string() + "-sub");
        fs::create_directories(sub);
        git(m_root, {"init", "-q", "-b", "main", sub.string()});
        linear(*this, sub, 2, "s");
        init();
        commitFile(repo, "readme.txt", "super\n", "Super");
        git(repo, {"-c", "protocol.file.allow=always", "submodule", "add", "-q", sub.string(), "sub"});
        git(repo, {"commit", "-q", "-m", "Add submodule"});
        commitFile(sub, "s3.txt", "s3\n", "Sub 3");
        git(repo / "sub", {"-c", "protocol.file.allow=always", "pull", "-q", "origin", "main"});
        git(repo, {"commit", "-q", "-am", "Update submodule"});
        track(sub);
        break;
    }
    case Recipe::Lfs:
        init();
        git(repo, {"config", "filter.testlfs.clean", "base64"});
        git(repo, {"config", "filter.testlfs.smudge", "base64 -d"});
        git(repo, {"config", "filter.testlfs.required", "true"});
        commitFile(repo, ".gitattributes", "*.bin filter=testlfs\n", "Attributes");
        commitFile(repo, "data.bin", "payload one\n", "Add filtered file");
        commitFile(repo, "data.bin", "payload two\n", "Change filtered file");
        break;
    case Recipe::TextEdgeCases:
        init();
        commitFile(repo, "crlf.txt", "one\r\ntwo\r\nthree\r\n", "CRLF file");
        commitFile(repo, "noeol.txt", "no final newline", "No final newline");
        commitFile(repo, "binary.bin", std::string("bin\0ary\x01\x02", 9), "Binary file");
        commitFile(repo, "markers.md", "Example:\n<<<<<<< ours\nx\n=======\ny\n>>>>>>> theirs\n", "Marker-like text");
        write(repo, "crlf.txt", "one\r\nTWO\r\nthree\r\n");
        write(repo, "noeol.txt", "no final newline, changed");
        break;
    case Recipe::Conflicted2:
        init();
        commitFile(repo, "conflict.txt", "top\nx=0\nbottom\n", "Base");
        commitFile(repo, "conflict.txt",
            "top\n<<<<<<< side 1\nx=1\n||||||| base\nx=0\n=======\nx=2\n>>>>>>> side 2\nbottom\n",
            "Conflicted commit");
        commitFile(repo, "other.txt", "descendant\n", "Descendant keeps the conflict");
        break;
    case Recipe::ConflictedN:
        init();
        commitFile(repo, "conflict.txt", "x=0\n", "Base");
        commitFile(repo, "conflict.txt",
            "<<<<<<< gg 3-sided conflict\n+++++++ side 1\nx=3\n------- base 1\nx=0\n+++++++ side 2\nx=1\n"
            "------- base 2\nx=0\n+++++++ side 3\nx=2\n>>>>>>> end of conflict\n",
            "Three-sided conflict");
        break;
    case Recipe::WorkingChanges:
        init();
        write(repo, "a.txt", "a\n");
        write(repo, "b.txt", "b\n");
        write(repo, "c.txt", "c\n");
        write(repo, "d.txt", "d unique content for rename detection\n");
        git(repo, {"add", "."});
        git(repo, {"commit", "-q", "-m", "Base"});
        write(repo, "a.txt", "a staged\n");
        git(repo, {"add", "a.txt"});
        write(repo, "b.txt", "b unstaged\n");
        write(repo, "c.txt", "c staged\n");
        git(repo, {"add", "c.txt"});
        write(repo, "c.txt", "c staged\nc unstaged\n");
        git(repo, {"mv", "d.txt", "e.txt"});
        write(repo, "u.txt", "untracked\n");
        write(repo, "n.txt", "intent to add\n");
        git(repo, {"add", "-N", "n.txt"});
        break;
    case Recipe::Stashes:
        init();
        commitFile(repo, "a.txt", "a\n", "Base");
        commitFile(repo, "b.txt", "b\n", "Second");
        write(repo, "a.txt", "a changed\n");
        git(repo, {"stash", "push", "-q", "-m", "worktree change"});
        write(repo, "a.txt", "a staged\n");
        git(repo, {"add", "a.txt"});
        write(repo, "b.txt", "b unstaged\n");
        git(repo, {"stash", "push", "-q", "-m", "index and worktree"});
        write(repo, "new.txt", "untracked\n");
        write(repo, "b.txt", "b again\n");
        git(repo, {"stash", "push", "-q", "-u", "-m", "with untracked"});
        break;
    }
    track(repo);
    return repo;
}

} // namespace ggtest
