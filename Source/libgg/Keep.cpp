#include "libgg/Keep.hpp"

#include "libgg/GitRunner.hpp"
#include "libgg/Operation.hpp"
#include "libgg/Thread.hpp"

#include <algorithm>
#include <exception>
#include <set>

namespace gg::keep {

namespace {

using namespace gg::git2;

// A keep ref as found.
struct Existing {
    std::string name;
    bool symbolic = false;
    std::string value;                // direct: the target's id; symbolic: "ref:<target>"
    std::optional<git_oid> commit;    // what it resolves to, when that is a commit
};

bool isCommit(git_repository* repo, const git_oid& oid)
{
    git_object* raw = nullptr;
    if (git_object_lookup(&raw, repo, &oid, GIT_OBJECT_COMMIT) != 0) {
        git_error_clear();
        return false;
    }
    git_object_free(raw);
    return true;
}

// The commit `ref` ends at (symbolic refs followed, tags peeled), if any.
std::optional<git_oid> commitOf(git_repository* repo, git_reference* ref)
{
    git_reference* rawResolved = nullptr;
    if (git_reference_resolve(&rawResolved, ref) != 0) {
        git_error_clear();
        return std::nullopt; // dangling
    }
    Reference resolved(rawResolved);
    const git_oid* target = git_reference_target(resolved.get());
    git_object* rawObject = nullptr;
    if (!target || git_object_lookup(&rawObject, repo, target, GIT_OBJECT_ANY) != 0) {
        git_error_clear();
        return std::nullopt;
    }
    Object object(rawObject);
    git_object* rawPeeled = nullptr;
    if (git_object_peel(&rawPeeled, object.get(), GIT_OBJECT_COMMIT) != 0) {
        git_error_clear();
        return std::nullopt;
    }
    Object peeled(rawPeeled);
    return *git_object_id(peeled.get());
}

std::vector<Existing> existingKeepRefs(git_repository* repo)
{
    std::vector<Existing> out;
    forEachReference(repo, [&](git_reference* ref) {
        const std::string name = git_reference_name(ref);
        if (!isKeepRef(name))
            return true;
        Existing e;
        e.name = name;
        e.symbolic = git_reference_type(ref) == GIT_REFERENCE_SYMBOLIC;
        e.value = e.symbolic ? std::string("ref:") + git_reference_symbolic_target(ref) : toHex(*git_reference_target(ref));
        git_reference* rawResolved = nullptr;
        if (git_reference_resolve(&rawResolved, ref) == 0) {
            Reference resolved(rawResolved);
            if (const git_oid* target = git_reference_target(resolved.get()); target && isCommit(repo, *target))
                e.commit = *target;
        } else {
            git_error_clear();
        }
        out.push_back(std::move(e));
        return true;
    });
    std::sort(out.begin(), out.end(), [](const Existing& a, const Existing& b) { return a.name < b.name; });
    return out;
}

// Well-formed: a direct ref named for the commit it points at.
bool wellFormed(const Existing& e) { return !e.symbolic && e.commit && e.name == refName(toHex(*e.commit)); }

// A: the commits branches, remote-tracking branches and tags point at.
std::vector<git_oid> anchors(git_repository* repo)
{
    std::set<std::string> seen;
    std::vector<git_oid> out;
    forEachReference(repo, [&](git_reference* ref) {
        const std::string name = git_reference_name(ref);
        if (name.rfind("refs/heads/", 0) != 0 && name.rfind("refs/remotes/", 0) != 0 && name.rfind("refs/tags/", 0) != 0)
            return true;
        if (const auto oid = commitOf(repo, ref); oid && seen.insert(toHex(*oid)).second)
            out.push_back(*oid);
        return true;
    });
    return out;
}

bool reachableFrom(git_repository* repo, const git_oid& id, const std::vector<git_oid>& tips)
{
    if (tips.empty())
        return false;
    for (const auto& t : tips)
        if (git_oid_equal(&t, &id))
            return true;
    return git_graph_reachable_from_any(repo, &id, tips.data(), tips.size()) == 1;
}

// The ids invariant K keeps, given the candidates.
std::vector<git_oid> wanted(git_repository* repo, const std::vector<git_oid>& candidates)
{
    if (candidates.empty())
        return {};
    const std::vector<git_oid> a = anchors(repo);
    std::vector<git_oid> survivors;
    std::set<std::string> seen;
    for (const auto& c : candidates)
        if (seen.insert(toHex(c)).second && !reachableFrom(repo, c, a))
            survivors.push_back(c);
    std::vector<git_oid> tips;
    for (const auto& c : survivors) {
        bool ancestor = false;
        for (const auto& d : survivors)
            if (!git_oid_equal(&c, &d) && git_graph_descendant_of(repo, &d, &c) == 1) {
                ancestor = true;
                break;
            }
        if (!ancestor)
            tips.push_back(c);
    }
    git_error_clear(); // a graph call that failed was read as "not reachable"
    return tips;
}

} // namespace

bool isKeepRef(const std::string& name) { return name.rfind(kPrefix, 0) == 0; }

std::string refName(const std::string& id) { return kPrefix + id; }

bool rebaseLeavesBaseUnkept(git_repository* repo, const std::string& upstream, const std::string& onto, const std::string& head)
{
    const auto u = fromHex(upstream);
    const auto o = fromHex(onto);
    const auto h = fromHex(head);
    if (!u || !o || !h || git_oid_equal(&*u, &*o) || !isCommit(repo, *u) || !isCommit(repo, *o) || !isCommit(repo, *h))
        return false;
    // The tips that keep `u`: the onto commit, the anchors (branches, remote-tracking branches,
    // tags; not HEAD, the stash or other refs) and the keep refs outside the replayed range.
    std::vector<git_oid> tips = anchors(repo);
    tips.push_back(*o);
    for (const auto& e : existingKeepRefs(repo)) {
        if (!e.commit)
            continue;
        // In the replayed range: the head or an ancestor of it, and a proper descendant of `u`.
        const bool inRange = (git_oid_equal(&*e.commit, &*h) == 1 || git_graph_descendant_of(repo, &*h, &*e.commit) == 1)
            && git_oid_equal(&*e.commit, &*u) == 0 && git_graph_descendant_of(repo, &*e.commit, &*u) == 1;
        if (!inRange)
            tips.push_back(*e.commit);
    }
    // True only when libgit2 answers "no" (0): a failed graph call gives no warning.
    for (const auto& t : tips)
        if (git_oid_equal(&t, &*u))
            return false;
    const int reached = git_graph_reachable_from_any(repo, &*u, tips.data(), tips.size()); // tips holds `o`: never empty
    git_error_clear();
    return reached == 0;
}

std::vector<std::string> read(git_repository* repo)
{
    std::vector<std::string> ids;
    try {
        for (const auto& e : existingKeepRefs(repo))
            if (wellFormed(e))
                ids.push_back(toHex(*e.commit));
    } catch (const std::exception&) {
        git_error_clear();
        ids.clear();
    }
    return ids;
}

bool maintain(git_repository* repo, const std::vector<std::string>& extra, std::vector<journal::RefChange>* changes, std::string* error)
{
    assertNotUiThread("keep::maintain");
    if (changes)
        changes->clear();
    std::string removals; // the update-ref input: deletions, creations
    std::string creations;
    std::vector<journal::RefChange> done; // deletions, then creations
    size_t deleted = 0;
    bool nested = false; // a ref to delete is below a name to create: the directory must go first
    try {
        const std::vector<Existing> existing = existingKeepRefs(repo);
        std::vector<git_oid> candidates;
        for (const auto& e : existing)
            if (e.commit)
                candidates.push_back(*e.commit);
        for (const auto& hex : extra)
            if (const auto oid = fromHex(hex); oid && isCommit(repo, *oid))
                candidates.push_back(*oid);
        std::set<std::string> want;
        for (const auto& oid : wanted(repo, candidates))
            want.insert(toHex(oid));

        const std::string zero = zeroId(repo);
        std::set<std::string> present; // well-formed refs that stay
        for (const auto& e : existing) {
            if (wellFormed(e) && want.count(toHex(*e.commit)) != 0) {
                present.insert(e.name);
                continue;
            }
            // A symbolic ref is deleted itself (--no-deref); no old value to check for it.
            removals += "delete " + e.name + (e.symbolic ? "" : " " + e.value) + "\n";
            done.push_back({e.name, e.value, zero});
        }
        deleted = done.size();
        for (const auto& id : want)
            if (present.count(refName(id)) == 0) {
                creations += "create " + refName(id) + " " + id + "\n";
                done.push_back({refName(id), zero, id});
                for (size_t i = 0; i < deleted; ++i)
                    nested = nested || done[i].ref.rfind(refName(id) + "/", 0) == 0;
            }
    } catch (const std::exception& e) {
        git_error_clear();
        if (error)
            *error = std::string("could not read the keep refs: ") + e.what();
        return false;
    }
    if (removals.empty() && creations.empty())
        return true;
    const char* workdir = git_repository_workdir(repo);
    auto update = [&](std::string input) {
        RunRequest request;
        request.args = {"git", "update-ref", "--no-deref", "--stdin"};
        request.cwd = workdir ? workdir : git_repository_path(repo);
        request.input = std::move(input);
        const RunResult result = run(request);
        if (!result.ok() && error)
            *error = result.message();
        return result.ok();
    };
    // One transaction, unless a deletion has to happen before a creation can.
    if (!nested) {
        if (!update(removals + creations))
            return false;
    } else {
        if (!removals.empty() && !update(removals))
            return false;
        if (!update(creations)) {
            if (changes) {
                done.resize(deleted); // the deletions were made
                *changes = std::move(done);
            }
            return false;
        }
    }
    if (changes)
        *changes = std::move(done);
    return true;
}

} // namespace gg::keep
