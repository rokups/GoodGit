#include "libgg/Todo.hpp"

#include "libgg/Git2.hpp"
#include "libgg/Thread.hpp"

#include <algorithm>
#include <cctype>
#include <functional>
#include <stdexcept>

namespace gg::todo {

using namespace gg::git2;

namespace {

constexpr std::string_view kComment = "#"; // Git's default core.commentChar

bool isSpace(char c) { return c == ' ' || c == '\t'; }

std::string_view trimLeft(std::string_view s)
{
    while (!s.empty() && isSpace(s.front()))
        s.remove_prefix(1);
    return s;
}

std::string_view trimRight(std::string_view s)
{
    while (!s.empty() && (isSpace(s.back()) || s.back() == '\r' || s.back() == '\n'))
        s.remove_suffix(1);
    return s;
}

// The next space-separated word of `s` (removed from it).
std::string_view nextWord(std::string_view& s)
{
    s = trimLeft(s);
    size_t n = 0;
    while (n < s.size() && !isSpace(s[n]))
        ++n;
    std::string_view word = s.substr(0, n);
    s = trimLeft(s.substr(n));
    return word;
}

std::vector<std::string_view> lines(std::string_view text)
{
    std::vector<std::string_view> out;
    while (!text.empty()) {
        const size_t nl = text.find('\n');
        out.push_back(text.substr(0, nl));
        if (nl == std::string_view::npos)
            break;
        text.remove_prefix(nl + 1);
    }
    return out;
}

std::string firstLine(std::string_view message) { return std::string(message.substr(0, message.find('\n'))); }

bool startsWith(std::string_view s, std::string_view prefix) { return s.substr(0, prefix.size()) == prefix; }

bool isHex(std::string_view s)
{
    return std::all_of(s.begin(), s.end(), [](unsigned char c) { return std::isxdigit(c) != 0; });
}

std::string withNewline(std::string s)
{
    if (s.empty() || s.back() != '\n')
        s += '\n';
    return s;
}

// "fixup! ", "squash! " or "amend! " at the start of `s`: the rest in `rest`.
bool skipFixupish(std::string_view s, std::string_view& rest)
{
    for (std::string_view prefix : {"fixup! ", "squash! ", "amend! "})
        if (startsWith(s, prefix)) {
            rest = s.substr(prefix.size());
            return true;
        }
    return false;
}

std::string shortId(const std::string& id) { return id.substr(0, 10); }

} // namespace

bool Item::isCommit() const
{
    switch (action) {
    case Action::Pick:
    case Action::Reword:
    case Action::Edit:
    case Action::Squash:
    case Action::Fixup:
    case Action::Drop:
        return true;
    default:
        return false;
    }
}

// ---- Names ------------------------------------------------------------------------------------

namespace {

struct Name {
    const char* name;
    char key;
};
// Indexed by Action.
constexpr Name kNames[] = {
    {"pick", 'p'}, {"reword", 'r'}, {"edit", 'e'}, {"squash", 's'},     {"fixup", 'f'}, {"drop", 'd'},
    {"exec", 'x'}, {"break", 'b'},  {"update-ref", 'u'}, {"label", 'l'}, {"reset", 't'}, {"merge", 'm'},
};
static_assert(std::size(kNames) == static_cast<size_t>(Action::Merge) + 1);

} // namespace

const char* actionName(Action action)
{
    return kNames[static_cast<size_t>(action)].name;
}

std::optional<Action> parseAction(std::string_view word)
{
    for (size_t i = 0; i < std::size(kNames); ++i)
        if (word == kNames[i].name || (word.size() == 1 && word[0] == kNames[i].key))
            return static_cast<Action>(i);
    return std::nullopt;
}

// ---- Git's todo text --------------------------------------------------------------------------

Todo parse(std::string_view text, std::vector<ParseError>* errors)
{
    Todo todo;
    int number = 0;
    for (std::string_view raw : lines(text)) {
        ++number;
        std::string_view line = trimRight(trimLeft(raw));
        if (line.empty() || startsWith(line, kComment))
            continue;
        auto fail = [&](const std::string& message) {
            if (errors)
                errors->push_back({number, message});
        };
        std::string_view rest = line;
        const std::string_view word = nextWord(rest);
        if (word == "noop")
            continue;
        const auto action = parseAction(word);
        if (!action) {
            fail("invalid line " + std::to_string(number) + ": " + std::string(line));
            continue;
        }
        Item item;
        item.action = *action;
        if (item.isCommit()) {
            if (item.action == Action::Fixup && (startsWith(rest, "-C ") || startsWith(rest, "-c "))) {
                item.fixup = rest[1] == 'C' ? FixupMessage::Use : FixupMessage::Edit;
                rest.remove_prefix(3);
            }
            item.commit = std::string(nextWord(rest));
            if (item.commit.empty()) {
                fail(std::string("missing arguments for ") + actionName(item.action));
                continue;
            }
            // Git ≥ 2.44 writes "# subject", older versions the bare subject.
            if (startsWith(rest, kComment))
                rest = trimLeft(rest.substr(kComment.size()));
            item.subject = std::string(rest);
        } else if (item.action == Action::Break) {
            // Takes no argument.
        } else {
            if (rest.empty()) {
                fail(std::string("missing arguments for ") + actionName(item.action));
                continue;
            }
            item.arg = item.action == Action::UpdateRef ? std::string(nextWord(rest)) : std::string(rest);
        }
        todo.items.push_back(std::move(item));
    }
    return todo;
}

std::string format(const Todo& todo)
{
    if (todo.items.empty())
        return "noop\n";
    std::string out;
    for (const Item& item : todo.items) {
        out += actionName(item.action);
        if (item.isCommit()) {
            if (item.action == Action::Fixup && item.fixup != FixupMessage::None)
                out += item.fixup == FixupMessage::Use ? " -C" : " -c";
            out += ' ';
            out += item.commit;
            if (!item.subject.empty())
                out += " # " + item.subject;
        } else if (item.action != Action::Break) {
            out += ' ';
            out += item.arg;
        }
        out += '\n';
        if (item.action == Action::UpdateRef)
            out += '\n';
    }
    return out;
}

// ---- The range --------------------------------------------------------------------------------

namespace {

CommitInfo describe(git_repository* repo, const git_commit* c)
{
    CommitInfo info;
    info.id = toHex(*git_commit_id(c));
    for (unsigned i = 0; i < git_commit_parentcount(c); ++i)
        info.parents.push_back(toHex(*git_commit_parent_id(c, i)));
    info.message = commitMessage(c);
    info.subject = firstLine(info.message);
    const git_signature* author = git_commit_author(c);
    info.authorName = author->name;
    info.authorEmail = author->email;
    info.authorTime = author->when.time;
    info.authorOffset = author->when.offset;
    if (git_commit_parentcount(c) > 0) {
        Commit parent = lookupCommit(repo, *git_commit_parent_id(c, 0));
        info.empty = git_oid_equal(git_commit_tree_id(c), git_commit_tree_id(parent.get())) == 1;
    } else {
        Tree tree = commitTree(c);
        info.empty = git_tree_entrycount(tree.get()) == 0;
    }
    return info;
}

std::vector<git_oid> remoteTips(git_repository* repo)
{
    std::vector<git_oid> tips;
    forEachReference(repo, [&](git_reference* ref) {
        if (startsWith(git_reference_name(ref), "refs/remotes/") && git_reference_type(ref) == GIT_REFERENCE_DIRECT)
            tips.push_back(*git_reference_target(ref));
        return true;
    });
    return tips;
}

bool published(git_repository* repo, const std::string& id, const std::vector<git_oid>& tips)
{
    if (tips.empty())
        return false;
    const git_oid oid = *fromHex(id);
    const bool reachable = std::any_of(tips.begin(), tips.end(), [&](const git_oid& t) { return git_oid_equal(&t, &oid); })
        || git_graph_reachable_from_any(repo, &oid, tips.data(), tips.size()) == 1;
    git_error_clear();
    return reachable;
}

void addCommit(git_repository* repo, Context& context, const git_oid& oid, const std::vector<git_oid>& tips)
{
    const std::string id = toHex(oid);
    if (context.commits.count(id))
        return;
    Commit c = lookupCommit(repo, oid);
    CommitInfo info = describe(repo, c.get());
    info.published = published(repo, id, tips);
    context.commits.emplace(id, std::move(info));
}

// Non-merge commits reachable from `push` but not from `hide`, parents first (Git's order).
std::vector<git_oid> walk(git_repository* repo, const git_oid& push, const std::optional<git_oid>& hide)
{
    git_revwalk* raw = nullptr;
    check(git_revwalk_new(&raw, repo), "git_revwalk_new");
    Revwalk rw(raw);
    check(git_revwalk_sorting(rw.get(), GIT_SORT_TOPOLOGICAL | GIT_SORT_REVERSE), "git_revwalk_sorting");
    check(git_revwalk_push(rw.get(), &push), "git_revwalk_push");
    if (hide)
        check(git_revwalk_hide(rw.get(), &*hide), "git_revwalk_hide");
    std::vector<git_oid> out;
    git_oid oid;
    while (git_revwalk_next(&oid, rw.get()) == 0) {
        Commit c = lookupCommit(repo, oid);
        if (git_commit_parentcount(c.get()) <= 1)
            out.push_back(oid);
    }
    git_error_clear();
    return out;
}

// Git's patch id of a non-merge commit against its parent (nullopt for an empty change).
std::optional<std::string> patchId(git_repository* repo, const git_oid& oid)
{
    Commit c = lookupCommit(repo, oid);
    Tree tree = commitTree(c.get());
    Tree parentTree;
    if (git_commit_parentcount(c.get()) > 0) {
        Commit p = lookupCommit(repo, *git_commit_parent_id(c.get(), 0));
        parentTree = commitTree(p.get());
    }
    git_diff* rawDiff = nullptr;
    check(git_diff_tree_to_tree(&rawDiff, repo, parentTree.get(), tree.get(), nullptr), "git_diff_tree_to_tree");
    Diff diff(rawDiff);
    if (git_diff_num_deltas(diff.get()) == 0)
        return std::nullopt;
    git_oid id;
    check(git_diff_patchid(&id, diff.get(), nullptr), "git_diff_patchid");
    return toHex(id);
}

git_oid resolveOrThrow(git_repository* repo, const std::string& spec)
{
    auto oid = resolve(repo, spec);
    git_error_clear();
    if (!oid)
        throw std::runtime_error("unknown revision '" + spec + "'");
    return *oid;
}

} // namespace

Context read(git_repository* repo, const ReadOptions& options)
{
    assertNotUiThread("todo::read");
    Context context;

    // The tip and the ref that moves with it.
    std::string headRef; // this worktree's branch
    std::optional<git_oid> headId;
    {
        git_reference* raw = nullptr;
        if (git_reference_lookup(&raw, repo, "HEAD") == 0) {
            Reference head(raw);
            if (git_reference_type(head.get()) == GIT_REFERENCE_SYMBOLIC)
                headRef = git_reference_symbolic_target(head.get());
        }
        git_oid oid;
        if (git_reference_name_to_id(&oid, repo, "HEAD") == 0)
            headId = oid;
        git_error_clear();
    }
    git_oid tip;
    if (options.tip == "HEAD") {
        if (!headId)
            throw std::runtime_error("HEAD has no commits yet");
        tip = *headId;
        context.tipRef = startsWith(headRef, "refs/heads/") ? headRef : std::string();
        context.tipIsHead = true;
    } else if (git_reference_name_to_id(&tip, repo, ("refs/heads/" + options.tip).c_str()) == 0) {
        context.tipRef = "refs/heads/" + options.tip;
        context.tipIsHead = headRef == context.tipRef;
    } else {
        git_error_clear();
        tip = resolveOrThrow(repo, options.tip);
        context.tipIsHead = headRef.empty() && headId && git_oid_equal(&*headId, &tip) == 1;
    }
    context.tip = toHex(tip);

    std::optional<git_oid> upstream;
    if (!options.upstream.empty()) {
        upstream = resolveOrThrow(repo, options.upstream);
        context.upstream = toHex(*upstream);
    }
    context.onto = options.onto.empty() ? context.upstream : toHex(resolveOrThrow(repo, options.onto));

    // The listed commits: merges dropped, commits whose change is already upstream left out
    // (Git's default --no-reapply-cherry-picks).
    std::vector<git_oid> listed = walk(repo, tip, upstream);
    if (upstream && !listed.empty()) {
        std::set<std::string> upstreamIds;
        for (const auto& oid : walk(repo, *upstream, tip))
            if (auto id = patchId(repo, oid))
                upstreamIds.insert(*id);
        if (!upstreamIds.empty())
            std::erase_if(listed, [&](const git_oid& oid) {
                auto id = patchId(repo, oid);
                return id && upstreamIds.count(*id);
            });
    }
    const std::vector<git_oid> tips = remoteTips(repo);
    for (const auto& oid : listed) {
        context.range.push_back(toHex(oid));
        addCommit(repo, context, oid, tips);
    }

    // Branches in the range, and those checked out in other worktrees.
    forEachReference(repo, [&](git_reference* ref) {
        const std::string name = git_reference_name(ref);
        if (startsWith(name, "refs/heads/") && git_reference_type(ref) == GIT_REFERENCE_DIRECT) {
            const std::string target = toHex(*git_reference_target(ref));
            if (context.commits.count(target))
                context.branchesAt[target].push_back(name);
        }
        return true;
    });
    for (const auto& [ref, worktree] : branchesInOtherWorktrees(repo))
        context.checkedOutElsewhere.insert(ref);

    // The starting todo: a pick per commit, then update-ref lines for the other branches at it
    // (Git lists them in reverse name order and skips branches checked out elsewhere).
    for (const auto& id : context.range) {
        Item pick;
        pick.commit = id;
        pick.subject = context.commits.at(id).subject;
        context.initial.items.push_back(pick);
        auto it = context.branchesAt.find(id);
        if (it == context.branchesAt.end())
            continue;
        std::vector<std::string> refs = it->second;
        std::sort(refs.rbegin(), refs.rend());
        for (const auto& ref : refs) {
            if (ref == context.tipRef || context.checkedOutElsewhere.count(ref))
                continue;
            Item update;
            update.action = Action::UpdateRef;
            update.arg = ref;
            context.initial.items.push_back(update);
        }
    }
    return context;
}

std::vector<ParseError> expand(git_repository* repo, Todo& todo, Context& context)
{
    assertNotUiThread("todo::expand");
    std::vector<ParseError> problems;
    const std::vector<git_oid> tips = remoteTips(repo);
    const size_t fullSize = hexSize(oidType(repo));
    for (size_t i = 0; i < todo.items.size(); ++i) {
        Item& item = todo.items[i];
        if (!item.isCommit())
            continue;
        std::optional<git_oid> oid;
        if (item.commit.size() == fullSize && isHex(item.commit)) {
            oid = fromHex(item.commit);
        } else if (item.commit.size() >= 4 && isHex(item.commit)) {
            git_object* raw = nullptr;
            const git_oid padded = *fromHex(item.commit + std::string(fullSize - item.commit.size(), '0'));
            const int rc = git_object_lookup_prefix(&raw, repo, &padded, item.commit.size(), GIT_OBJECT_COMMIT);
            if (rc == GIT_EAMBIGUOUS) {
                problems.push_back({static_cast<int>(i + 1), "short commit id " + item.commit + " is ambiguous"});
                git_error_clear();
                continue;
            }
            if (rc == 0) {
                Object obj(raw);
                oid = *git_object_id(obj.get());
            }
        } else {
            oid = resolve(repo, item.commit);
        }
        git_error_clear();
        git_commit* rawCommit = nullptr;
        if (!oid || git_commit_lookup(&rawCommit, repo, &*oid) != 0) {
            git_error_clear();
            problems.push_back({static_cast<int>(i + 1), "could not parse '" + item.commit + "'"});
            continue;
        }
        Commit owned(rawCommit);
        item.commit = toHex(*oid);
        addCommit(repo, context, *oid, tips);
    }
    return problems;
}

Context readRemaining(git_repository* repo, std::string_view todoText, const std::string& headName)
{
    assertNotUiThread("todo::readRemaining");
    Context context;
    context.continuesHead = true;
    git_oid head;
    if (git_reference_name_to_id(&head, repo, "HEAD") != 0) {
        git_error_clear();
        throw std::runtime_error("HEAD has no commits");
    }
    context.onto = context.upstream = context.tip = toHex(head);
    const std::string name = std::string(trimRight(trimLeft(headName)));
    context.tipRef = startsWith(name, "refs/heads/") ? name : std::string();
    context.tipIsHead = true;
    std::vector<ParseError> problems;
    Todo list = parse(todoText, &problems);
    for (auto& p : expand(repo, list, context))
        problems.push_back(std::move(p));
    if (!problems.empty())
        throw std::runtime_error("cannot read the remaining todo: " + problems.front().message);
    for (const Item& item : list.items)
        if (item.isCommit() && std::find(context.range.begin(), context.range.end(), item.commit) == context.range.end())
            context.range.push_back(item.commit);
    forEachReference(repo, [&](git_reference* ref) {
        const std::string refName = git_reference_name(ref);
        if (startsWith(refName, "refs/heads/") && git_reference_type(ref) == GIT_REFERENCE_DIRECT) {
            const std::string target = toHex(*git_reference_target(ref));
            if (context.commits.count(target))
                context.branchesAt[target].push_back(refName);
        }
        return true;
    });
    context.initial = std::move(list);
    return context;
}

// ---- Autosquash -------------------------------------------------------------------------------

void autosquash(Todo& todo, const Context& context)
{
    const size_t n = todo.items.size();
    std::vector<int> next(n, -1), tail(n, -1);
    std::vector<bool> chained(n, false);
    std::vector<std::string> subjects(n);
    std::map<std::string, size_t> bySubject; // earliest row with that subject
    bool rearranged = false;
    for (size_t i = 0; i < n; ++i) {
        Item& item = todo.items[i];
        if (item.action != Action::Pick && item.action != Action::Reword && item.action != Action::Edit)
            continue;
        subjects[i] = context.commits.at(item.commit).subject; // every listed commit is in the context
        const std::string& subject = subjects[i];
        std::string_view target;
        if (skipFixupish(subject, target)) {
            while (skipFixupish(target, target)) { }
            int found = -1;
            if (auto it = bySubject.find(std::string(target)); it != bySubject.end()) {
                found = static_cast<int>(it->second);
            } else if (target.find(' ') == std::string_view::npos && target.size() >= 4 && isHex(target)) {
                for (size_t j = 0; j < i && found < 0; ++j)
                    if (todo.items[j].isCommit() && startsWith(todo.items[j].commit, target))
                        found = static_cast<int>(j);
            }
            if (found < 0)
                for (size_t j = 0; j < i && found < 0; ++j)
                    if (todo.items[j].isCommit() && !subjects[j].empty() && startsWith(subjects[j], target))
                        found = static_cast<int>(j);
            if (found >= 0) {
                rearranged = true;
                if (startsWith(subject, "fixup!")) {
                    item.action = Action::Fixup;
                } else if (startsWith(subject, "amend!")) {
                    item.action = Action::Fixup;
                    item.fixup = FixupMessage::Use;
                } else {
                    item.action = Action::Squash;
                }
                const size_t t = static_cast<size_t>(found);
                const size_t after = tail[t] < 0 ? t : static_cast<size_t>(tail[t]);
                next[i] = next[after];
                next[after] = static_cast<int>(i);
                tail[t] = static_cast<int>(i);
                chained[i] = true;
            }
        }
        bySubject.emplace(subject, i);
    }
    if (!rearranged)
        return;
    std::vector<Item> out;
    out.reserve(n);
    for (size_t i = 0; i < n; ++i) {
        if (chained[i])
            continue;
        for (int j = static_cast<int>(i); j >= 0; j = next[static_cast<size_t>(j)])
            out.push_back(todo.items[static_cast<size_t>(j)]);
    }
    todo.items = std::move(out);
}

// ---- Messages ---------------------------------------------------------------------------------

std::string cleanup(std::string_view message)
{
    std::string out;
    int pendingBlank = 0;
    for (std::string_view line : lines(message)) {
        if (startsWith(line, kComment))
            continue;
        line = trimRight(line);
        if (line.empty()) {
            ++pendingBlank;
            continue;
        }
        if (pendingBlank && !out.empty())
            out += '\n';
        pendingBlank = 0;
        out += line;
        out += '\n';
    }
    return out;
}

std::vector<Group> groups(const Todo& todo)
{
    std::vector<Group> out;
    bool open = false;     // the last group still takes followers
    bool finished = false; // an exec/break/update-ref row finished its commit: the next follower amends it
    for (size_t i = 0; i < todo.items.size(); ++i) {
        const Item& item = todo.items[i];
        switch (item.action) {
        case Action::Pick:
        case Action::Reword:
        case Action::Edit:
            out.push_back(Group{i, {}, false, std::nullopt});
            open = true;
            finished = false;
            break;
        case Action::Squash:
        case Action::Fixup: {
            const bool editor = item.action == Action::Squash || item.fixup == FixupMessage::Edit;
            if (!open) {
                out.push_back(Group{i, {}, false, std::nullopt});
                open = true;
                finished = false;
                break;
            }
            if (finished) {
                const size_t previous = out.back().first;
                out.push_back(Group{i, {}, editor, previous});
                finished = false;
                break;
            }
            out.back().followers.push_back(i);
            if (editor)
                out.back().needsEditor = true;
            break;
        }
        case Action::Exec:
        case Action::Break:
        case Action::UpdateRef:
            finished = open; // Git finishes the commit before running these (is_final_fixup)
            break;
        case Action::Label:
        case Action::Reset:
        case Action::Merge:
            open = false; // HEAD moves elsewhere
            break;
        default:
            break; // drop: as if the row were not there
        }
    }
    return out;
}

std::optional<Group> groupAt(const Todo& todo, size_t row)
{
    for (const Group& g : groups(todo))
        if (g.first == row || std::find(g.followers.begin(), g.followers.end(), row) != g.followers.end())
            return g;
    return std::nullopt;
}

namespace {

// Every commit a todo names is in its context (read()/expand() put it there).
const std::string& messageOf(const Context& context, const Item& item) { return context.commits.at(item.commit).message; }

std::string ordinal(size_t n) { return n == 1 ? "1st" : "#" + std::to_string(n); }

// Every line commented out ("# line", "#" for an empty line).
std::string commentOut(std::string_view message)
{
    std::string out;
    for (std::string_view line : lines(message)) {
        out += kComment;
        if (!line.empty()) {
            out += ' ';
            out += line;
        }
        out += '\n';
    }
    return out;
}

// The rows after the group's starting commit: its followers, or for a group that amends a
// finished commit every row of it.
std::vector<size_t> foldedRows(const Group& group)
{
    std::vector<size_t> rows;
    if (group.amends)
        rows.push_back(group.first);
    rows.insert(rows.end(), group.followers.begin(), group.followers.end());
    return rows;
}

// The first message of the group: its first commit's, or the message of the commit it amends.
std::string baseMessage(const Todo& todo, const Group& group, const Context& context)
{
    if (group.amends)
        for (const Group& g : groups(todo))
            if (g.first == *group.amends)
                return groupMessage(todo, g, context);
    return messageOf(context, todo.items[group.first]);
}

// The message a fixup-only group keeps (no editor): the last `fixup -C` commit's message without
// its `amend!` subject, else the first commit's.
std::string keptMessage(const Todo& todo, const Group& group, const Context& context)
{
    const std::vector<size_t> rows = foldedRows(group);
    for (auto it = rows.rbegin(); it != rows.rend(); ++it) {
        const Item& item = todo.items[*it];
        if (item.action != Action::Fixup || item.fixup == FixupMessage::None)
            continue;
        std::string message = messageOf(context, item);
        if (startsWith(message, "amend!")) {
            size_t pos = message.find('\n');
            pos = pos == std::string::npos ? message.size() : pos + 1;
            while (pos < message.size() && message[pos] == '\n')
                ++pos;
            message.erase(0, pos);
        }
        return message;
    }
    return baseMessage(todo, group, context);
}

} // namespace

std::string squashTemplate(const Todo& todo, const Group& group, const Context& context)
{
    struct Entry {
        std::string message;
        bool kept = true;
        bool commentSubject = false;
    };
    std::vector<Entry> entries;
    entries.push_back({baseMessage(todo, group, context), true, false});
    bool seenSquash = false;
    for (size_t row : foldedRows(group)) {
        const Item& item = todo.items[row];
        const std::string& message = messageOf(context, item);
        const bool fixupish = startsWith(message, "squash!") || startsWith(message, "fixup!");
        if (item.action == Action::Squash) {
            seenSquash = true;
            entries.push_back({message, true, fixupish || startsWith(message, "amend!")});
        } else if (item.fixup != FixupMessage::None) {
            // fixup -C/-c replaces the messages so far unless a squash already combined them.
            if (!seenSquash)
                for (auto& e : entries)
                    e.kept = false;
            entries.push_back({message, true, startsWith(message, "amend!") || (seenSquash && fixupish)});
        } else {
            entries.push_back({message, false, false});
        }
    }
    const std::string c(kComment);
    std::string out = c + " This is a combination of " + std::to_string(entries.size()) + " commits.\n";
    for (size_t k = 0; k < entries.size(); ++k) {
        const Entry& e = entries[k];
        if (k > 0)
            out += '\n';
        if (e.kept)
            out += k == 0 ? c + " This is the 1st commit message:\n\n" : c + " This is the commit message " + ordinal(k + 1) + ":\n\n";
        else
            out += c + " The " + (k == 0 ? std::string("1st commit message") : "commit message " + ordinal(k + 1)) + " will be skipped:\n\n";
        const std::string message = withNewline(e.message);
        if (!e.kept) {
            out += commentOut(message);
        } else if (e.commentSubject) {
            const size_t nl = message.find('\n');
            out += commentOut(message.substr(0, nl + 1));
            out += message.substr(nl + 1);
        } else {
            out += message;
        }
    }
    return out;
}

std::string groupMessage(const Todo& todo, const Group& group, const Context& context)
{
    const Item& first = todo.items[group.first];
    if (first.message)
        return cleanup(*first.message);
    if (group.needsEditor)
        return cleanup(squashTemplate(todo, group, context));
    if (first.action == Action::Reword && group.followers.empty())
        return cleanup(messageOf(context, first));
    return keptMessage(todo, group, context);
}

std::string editorText(const Todo& todo, const Group& group, const Context& context)
{
    const Item& first = todo.items[group.first];
    if (first.message)
        return *first.message;
    if (group.needsEditor)
        return squashTemplate(todo, group, context);
    return keptMessage(todo, group, context);
}

std::map<std::string, std::string> editorMessages(const Todo& todo)
{
    std::map<std::string, std::string> out;
    for (const Group& g : groups(todo)) {
        const Item& first = todo.items[g.first];
        if (!first.message)
            continue;
        size_t row = g.first;
        if (g.needsEditor)
            row = g.followers.empty() ? g.first : g.followers.back(); // the combined message
        else if (first.action != Action::Reword)
            continue; // Git asks for no message here
        out[todo.items[row].commit] = *first.message;
    }
    return out;
}

// ---- Validation -------------------------------------------------------------------------------

size_t unchangedPrefix(const Todo& todo, const Context& context)
{
    std::string current = context.onto;
    for (size_t i = 0; i < todo.items.size(); ++i) {
        const Item& item = todo.items[i];
        if (item.action == Action::UpdateRef)
            continue;
        if (item.action != Action::Pick || item.message)
            return i;
        const auto& parents = context.commits.at(item.commit).parents;
        if (current.empty() ? !parents.empty() : (parents.size() != 1 || parents.front() != current))
            return i;
        // A squash/fixup coming next changes this commit too (after an update-ref row it amends
        // a copy: this commit is kept for the branch).
        for (size_t j = i + 1; j < todo.items.size(); ++j) {
            const Action a = todo.items[j].action;
            if (a == Action::Squash || a == Action::Fixup)
                return i;
            if (a != Action::Drop)
                break;
        }
        current = item.commit;
    }
    return todo.items.size();
}

std::vector<Issue> validate(const Todo& todo, const Context& context)
{
    using Severity = Issue::Severity;
    using Code = Issue::Code;
    std::vector<Issue> issues;
    auto add = [&](Severity s, Code c, int row, std::string message) { issues.push_back({s, c, row, std::move(message)}); };

    bool seenCommit = false;
    std::map<std::string, size_t> firstRow; // commit → first row keeping it
    std::set<std::string> updated;
    std::set<std::string> kept;             // commits the result still contains
    for (size_t i = 0; i < todo.items.size(); ++i) {
        const Item& item = todo.items[i];
        const int row = static_cast<int>(i);
        if (item.isCommit()) {
            if ((item.action == Action::Squash || item.action == Action::Fixup) && !seenCommit && !context.continuesHead)
                add(Severity::Error, Code::SquashWithoutCommit, row,
                    std::string("cannot '") + actionName(item.action) + "' without a previous commit");
            if (item.action == Action::Drop)
                continue;
            seenCommit = true;
            kept.insert(item.commit);
            if (auto [it, fresh] = firstRow.emplace(item.commit, i); !fresh)
                add(Severity::Warning, Code::DuplicateCommit, row,
                    "commit " + shortId(item.commit) + " is also on row " + std::to_string(it->second + 1));
        } else if (item.action == Action::Exec) {
            if (trimLeft(item.arg).empty())
                add(Severity::Error, Code::EmptyExec, row, "exec needs a command");
        } else if (item.action == Action::UpdateRef) {
            if (!startsWith(item.arg, "refs/"))
                add(Severity::Error, Code::BadRef, row, "update-ref requires a fully qualified refname e.g. refs/heads/" + item.arg);
            else if (!updated.insert(item.arg).second)
                add(Severity::Error, Code::BadRef, row, "'" + item.arg + "' is already updated by an earlier row");
            else if (item.arg == context.tipRef)
                add(Severity::Error, Code::BadRef, row, "'" + item.arg + "' is the branch being rebased");
        } else if (item.action == Action::Label || item.action == Action::Reset || item.action == Action::Merge) {
            seenCommit = true;
        }
    }

    // Branches that move and would keep none of their own commits. A branch's own commits are
    // the listed commits it reaches that no other branch in the range below it reaches (for
    // stacked branches: the commits since the previous branch).
    {
        std::map<std::string, std::string> tipOf; // ref → commit
        for (const auto& [commit, refs] : context.branchesAt)
            for (const auto& ref : refs)
                tipOf[ref] = commit;
        std::set<std::string> inRange(context.range.begin(), context.range.end());
        auto reach = [&](const std::string& from) {
            std::set<std::string> seen;
            std::vector<std::string> stack{from};
            while (!stack.empty()) {
                const std::string c = stack.back();
                stack.pop_back();
                if (!inRange.count(c) || !seen.insert(c).second)
                    continue;
                for (const auto& p : context.commits.at(c).parents)
                    stack.push_back(p);
            }
            return seen;
        };
        auto check = [&](const std::string& ref, const std::string& tip, int row) {
            if (tip.empty() || !inRange.count(tip))
                return;
            std::set<std::string> own = reach(tip);
            for (const auto& [other, commit] : tipOf) {
                if (other == ref || commit == tip)
                    continue;
                const std::set<std::string> below = reach(commit);
                if (below.count(tip))
                    continue; // a branch stacked on this one
                for (const auto& c : below)
                    own.erase(c);
            }
            if (own.empty() || std::any_of(own.begin(), own.end(), [&](const std::string& c) { return kept.count(c) > 0; }))
                return;
            const std::string name = startsWith(ref, "refs/heads/") ? ref.substr(11) : ref;
            add(Severity::Warning, Code::BranchLosesCommits, row, "every commit of branch '" + name + "' is dropped");
        };
        if (!context.tipRef.empty())
            check(context.tipRef, context.tip, -1);
        for (size_t i = 0; i < todo.items.size(); ++i)
            if (todo.items[i].action == Action::UpdateRef)
                if (auto it = tipOf.find(todo.items[i].arg); it != tipOf.end())
                    check(it->first, it->second, static_cast<int>(i));
    }

    // Published commits that are rewritten or dropped.
    {
        const size_t prefix = unchangedPrefix(todo, context);
        // (Rows are never removed from the list, only dropped: every listed commit has a row.)
        for (size_t i = prefix; i < todo.items.size(); ++i) {
            const Item& item = todo.items[i];
            if (item.isCommit() && context.commits.at(item.commit).published)
                add(Severity::Warning, Code::Published, static_cast<int>(i),
                    "commit " + shortId(item.commit) + " is already on a remote");
        }
    }
    return issues;
}

bool hasErrors(const std::vector<Issue>& issues)
{
    return std::any_of(issues.begin(), issues.end(), [](const Issue& i) { return i.error(); });
}

// ---- Engine -----------------------------------------------------------------------------------

EngineChoice chooseEngine(const Todo& todo, const Options& options)
{
    if (options.runAsGitRebase)
        return {Engine::Native, "you chose to run it as git rebase"};
    if (!options.execEach.empty())
        return {Engine::Native, "exec after every commit runs through git rebase"};
    for (size_t i = 0; i < todo.items.size(); ++i) {
        const Action a = todo.items[i].action;
        const std::string row = "row " + std::to_string(i + 1);
        switch (a) {
        case Action::Edit:
        case Action::Break:
            return {Engine::Native, row + " is " + actionName(a) + ", which stops the rebase"};
        case Action::Exec:
            return {Engine::Native, row + " runs a command"};
        case Action::Label:
        case Action::Reset:
        case Action::Merge:
            return {Engine::Native, row + " is " + actionName(a) + " (--rebase-merges)"};
        default:
            break;
        }
    }
    return {Engine::InMemory, "only pick, reword, squash, fixup, drop and update-ref: rewritten in memory, one Undo"};
}

void addExecEach(Todo& todo, const std::string& command)
{
    std::vector<Item> out;
    auto isFollower = [](const Item& item) { return item.action == Action::Squash || item.action == Action::Fixup; };
    for (size_t i = 0; i < todo.items.size(); ++i) {
        const Item& item = todo.items[i];
        out.push_back(item);
        if (!item.isCommit() || item.action == Action::Drop)
            continue;
        if (i + 1 < todo.items.size() && isFollower(todo.items[i + 1]))
            continue; // after the last squash/fixup of the group
        Item exec;
        exec.action = Action::Exec;
        exec.arg = command;
        out.push_back(exec);
    }
    todo.items = std::move(out);
}

gg::rewrite::Plan toPlan(const Todo& todo, const Context& context, bool replayStops)
{
    namespace rw = gg::rewrite;
    const auto issues = validate(todo, context);
    for (const auto& issue : issues)
        if (issue.error())
            throw std::runtime_error(issue.message);
    Todo engineCheck = todo;
    if (replayStops)
        for (Item& item : engineCheck.items)
            if (item.action == Action::Edit || item.action == Action::Break || item.action == Action::Exec)
                item.action = Action::Pick; // what the engine choice looks at: in memory
    if (const auto engine = chooseEngine(engineCheck, {}); engine.engine != Engine::InMemory)
        throw std::runtime_error("this todo needs git rebase: " + engine.reason);

    rw::Plan plan;
    plan.reflogMessage = "ggui: interactive rebase";
    plan.rewriteKind = "rebase";
    plan.rebaseLike = true;
    plan.upstream = context.upstream;
    plan.keepBranches = true; // like git: only the rebased branch and update-ref lines move
    plan.keepHead = true;
    // Post-rewrite as Git reports it: the leading picks that stay as they are are skipped
    // (skip_unnecessary_picks; drop rows do not stop it) and not reported, except the last one when
    // a squash/fixup comes next; every later commit is, fast-forwarded ones as themselves.
    plan.reportUnchanged = true;
    {
        std::string base = context.onto;
        std::string last;
        size_t i = 0;
        for (; i < todo.items.size(); ++i) {
            const Item& item = todo.items[i];
            if (item.action == Action::Drop)
                continue;
            if (item.action != Action::Pick || base.empty())
                break;
            const auto& parents = context.commits.at(item.commit).parents;
            if (parents.size() != 1 || parents.front() != base)
                break;
            plan.unreported.insert(item.commit);
            base = last = item.commit;
        }
        if (i < todo.items.size() && !last.empty()
            && (todo.items[i].action == Action::Squash || todo.items[i].action == Action::Fixup))
            plan.unreported.erase(last);
    }

    const std::vector<Group> all = groups(todo);
    std::map<size_t, const Group*> byFirst;
    std::map<size_t, const Group*> lastRow; // a group's last squash/fixup row → the group
    for (const Group& g : all) {
        byFirst[g.first] = &g;
        if (!g.followers.empty())
            lastRow[g.followers.back()] = &g;
        else if (g.amends)
            lastRow[g.first] = &g;
    }

    // The step the next commit goes onto: a step key, or "=<onto>" before the first one.
    std::string current = context.onto.empty() ? std::string() : "=" + context.onto;
    bool started = false; // a commit row came
    for (size_t i = 0; i < todo.items.size(); ++i) {
        const Item& item = todo.items[i];
        const std::string key = "row:" + std::to_string(i);
        switch (item.action) {
        case Action::Pick:
        case Action::Reword:
        case Action::Edit: { // edit: only with replayStops (checked above)
            rw::Step s;
            s.kind = rw::Step::Kind::Pick;
            s.source = item.commit;
            s.key = key;
            s.sourceParents = false;
            if (!current.empty())
                s.parents = {current};
            const Group* g = byFirst.at(i);
            if (g->followers.empty() && (item.action == Action::Reword || item.message))
                s.message = groupMessage(todo, *g, context);
            plan.steps.push_back(std::move(s));
            current = key;
            started = true;
            break;
        }
        case Action::Squash:
        case Action::Fixup: {
            rw::Step s;
            s.kind = rw::Step::Kind::Squash;
            s.source = item.commit;
            s.key = key;
            if (!started && context.continuesHead) {
                // The rest of a stopped rebase: folded into HEAD as it is (amended).
                rw::Step head;
                head.kind = rw::Step::Kind::Pick;
                head.source = context.onto;
                head.key = "head";
                head.mapSource = false;
                plan.steps.push_back(std::move(head));
                s.amend = true;
                current = key;
            }
            started = true;
            if (auto it = byFirst.find(i); it != byFirst.end() && it->second->amends) {
                // After an exec/break/update-ref row: amends the finished commit, which the
                // update-ref rows before it keep pointing at (as in Git).
                s.amend = true;
                current = key;
            }
            if (auto it = lastRow.find(i); it != lastRow.end())
                s.message = groupMessage(todo, *it->second, context);
            plan.steps.push_back(std::move(s));
            break; // otherwise the group's commit stays the current step
        }
        case Action::UpdateRef:
            if (!current.empty())
                plan.refsToSteps[item.arg] = current;
            break;
        default:
            break; // drop: not replayed
        }
    }
    if (!current.empty()) {
        if (!context.tipRef.empty())
            plan.refsToSteps[context.tipRef] = current;
        else if (context.tipIsHead)
            plan.detachHeadAt = current;
    }
    return plan;
}

} // namespace gg::todo
