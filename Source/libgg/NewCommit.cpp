#include "libgg/NewCommit.hpp"

#include "libgg/GitRunner.hpp"
#include "libgg/Thread.hpp"

#include <filesystem>

namespace gg {

namespace fs = std::filesystem;
using namespace gg::git2;

NewCommitResult newCommit(git_repository* repo, const NewCommitOptions& options)
{
    assertNotUiThread("newCommit");
    NewCommitResult result;
    const bool bare = git_repository_is_bare(repo) == 1;
    const fs::path cwd = bare ? fs::path(git_repository_path(repo)) : fs::path(git_repository_workdir(repo));

    std::vector<Commit> parents;
    std::vector<std::string> specs = options.parents;
    const bool unborn = git_repository_head_unborn(repo) == 1;
    if (specs.empty() && !unborn)
        specs.push_back("HEAD");
    for (const auto& spec : specs) {
        git_object* raw = nullptr;
        if (git_revparse_single(&raw, repo, spec.c_str()) != 0) {
            result.error = "unknown revision '" + spec + "'";
            git_error_clear();
            return result;
        }
        Object obj(raw);
        git_object* peeled = nullptr;
        if (git_object_peel(&peeled, obj.get(), GIT_OBJECT_COMMIT) != 0) {
            result.error = "'" + spec + "' is not a commit";
            git_error_clear();
            return result;
        }
        parents.emplace_back(reinterpret_cast<git_commit*>(peeled));
    }

    // Tree: the first parent's (empty tree for a root commit).
    Tree tree;
    if (!parents.empty()) {
        tree = commitTree(parents.front().get());
    } else {
        git_treebuilder* rawBuilder = nullptr;
        check(git_treebuilder_new(&rawBuilder, repo, nullptr), "git_treebuilder_new");
        TreeBuilder builder(rawBuilder);
        git_oid emptyTree;
        check(git_treebuilder_write(&emptyTree, builder.get()), "git_treebuilder_write");
        tree = lookupTree(repo, emptyTree);
    }

    git_signature* rawAuthor = nullptr;
    git_signature* rawCommitter = nullptr;
    if (git_signature_default_from_env(&rawAuthor, &rawCommitter, repo) != 0) {
        result.error = "please set user.name and user.email (" + lastErrorMessage() + ")";
        git_error_clear();
        return result;
    }
    Signature author(rawAuthor);
    Signature committer(rawCommitter);
    std::vector<const git_commit*> parentPtrs;
    for (const auto& p : parents)
        parentPtrs.push_back(p.get());
    std::string message = options.message;
    if (!message.empty() && message.back() != '\n')
        message.push_back('\n');
    git_oid id;
    check(git_commit_create(&id, repo, nullptr, author.get(), committer.get(), nullptr, message.c_str(), tree.get(),
              parentPtrs.size(), parentPtrs.data()),
        "git_commit_create");
    result.commit = toHex(id);

    // Move HEAD through git.
    std::string headTarget;
    {
        headTarget = git2::headTarget(repo);
    }
    std::string headCommit;
    if (!unborn) {
        git_oid h;
        if (git_reference_name_to_id(&h, repo, "HEAD") == 0)
            headCommit = toHex(h);
        git_error_clear();
    }
    const bool onHead = parents.empty() ? unborn : toHex(*git_commit_id(parents.front().get())) == headCommit;
    RunResult r;
    const std::string branchRef = options.branch.empty() ? std::string() : "refs/heads/" + options.branch;
    if (!options.detach && !branchRef.empty() && branchRef != headTarget) {
        // Another branch at the first parent: it advances, then HEAD follows it.
        git_oid tip;
        const bool have = !parents.empty() && git_reference_name_to_id(&tip, repo, branchRef.c_str()) == 0;
        git_error_clear();
        if (!have || toHex(tip) != toHex(*git_commit_id(parents.front().get()))) {
            result.error = "branch '" + options.branch + "' is not at the first parent";
            return result;
        }
        const std::string old = toHex(tip);
        r = git(cwd, {"update-ref", "--create-reflog", "-m", "gg new", branchRef, result.commit, old});
        if (r.ok()) {
            r = bare ? git(cwd, {"symbolic-ref", "HEAD", branchRef}) : git(cwd, {"switch", "--quiet", options.branch});
            if (!r.ok()) {
                const std::string why = r.message();
                git(cwd, {"update-ref", "-m", "gg new (undo)", branchRef, old, result.commit});
                result.error = why;
                return result;
            }
            result.movedBranch = branchRef;
        }
    } else if (!options.detach && !headTarget.empty() && onHead) {
        const std::string old = unborn ? std::string(hexSize(oidType(repo)), '0') : headCommit;
        r = git(cwd, {"update-ref", "--create-reflog", "-m", "gg new", headTarget, result.commit, old});
        result.movedBranch = headTarget;
    } else if (bare) {
        r = git(cwd, {"update-ref", "--no-deref", "-m", "gg new", "HEAD", result.commit});
    } else {
        r = git(cwd, {"switch", "--quiet", "--detach", result.commit});
    }
    if (!r.ok()) {
        result.error = r.message();
        return result;
    }
    result.ok = true;
    return result;
}

} // namespace gg
