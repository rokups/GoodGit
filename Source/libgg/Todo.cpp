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

bool Item::makesCommit() const
{
    return (isCommit() && action != Action::Drop) || action == Action::Merge;
}

std::vector<std::string> Item::mergeHeads() const
{
    std::vector<std::string> out;
    if (action != Action::Merge)
        return out;
    std::string_view rest = arg;
    while (!trimLeft(rest).empty())
        out.emplace_back(nextWord(rest));
    return out;
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
        } else if (item.action == Action::Reset || item.action == Action::Merge) {
            // reset <label> [# subject]; merge [-C|-c <commit>] <label>... [# subject]
            if (item.action == Action::Merge && (startsWith(rest, "-C ") || startsWith(rest, "-c "))) {
                item.fixup = rest[1] == 'C' ? FixupMessage::Use : FixupMessage::Edit;
                rest.remove_prefix(3);
                item.commit = std::string(nextWord(rest));
            }
            std::vector<std::string> names;
            if (item.action == Action::Reset && startsWith(rest, kNewRoot)) {
                names.emplace_back(kNewRoot);
                rest = trimLeft(rest.substr(std::string_view(kNewRoot).size()));
            }
            while (!rest.empty() && !startsWith(rest, kComment) && (item.action == Action::Merge || names.empty()))
                names.emplace_back(nextWord(rest));
            if (item.action == Action::Reset)
                while (!rest.empty() && !startsWith(rest, kComment))
                    nextWord(rest); // Git reads the first word only
            if (startsWith(rest, kComment))
                item.subject = std::string(trimLeft(rest.substr(kComment.size())));
            if (names.empty()) {
                fail(std::string("missing arguments for ") + actionName(item.action));
                continue;
            }
            for (size_t k = 0; k < names.size(); ++k)
                item.arg += (k ? " " : "") + names[k];
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
        } else if (item.action == Action::Merge) {
            if (item.fixup != FixupMessage::None && !item.commit.empty())
                out += (item.fixup == FixupMessage::Use ? " -C " : " -c ") + item.commit;
            out += ' ';
            out += item.arg;
            if (!item.subject.empty())
                out += " # " + item.subject;
        } else if (item.action != Action::Break) {
            out += ' ';
            out += item.arg;
            if (item.action == Action::Reset && !item.subject.empty())
                out += " # " + item.subject;
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

// Every commit reachable from `tip` and not from `upstream`, merges included, oldest first in
// Git's order (`rev-list --topo-order --reverse`, REV_SORT_IN_GRAPH_ORDER): from the tip, a
// commit's parents are taken last-ready-first once all their children are out, then reversed.
std::vector<std::string> graphOrderRange(git_repository* repo, const git_oid& tip, const std::optional<git_oid>& upstream)
{
    git_revwalk* raw = nullptr;
    check(git_revwalk_new(&raw, repo), "git_revwalk_new");
    Revwalk rw(raw);
    check(git_revwalk_push(rw.get(), &tip), "git_revwalk_push");
    if (upstream)
        check(git_revwalk_hide(rw.get(), &*upstream), "git_revwalk_hide");
    std::map<std::string, std::vector<std::string>> parentsOf;
    git_oid oid;
    while (git_revwalk_next(&oid, rw.get()) == 0) {
        Commit c = lookupCommit(repo, oid);
        auto& parents = parentsOf[toHex(oid)];
        for (unsigned i = 0; i < git_commit_parentcount(c.get()); ++i)
            parents.push_back(toHex(*git_commit_parent_id(c.get(), i)));
    }
    git_error_clear();
    std::map<std::string, int> children;
    for (const auto& [id, parents] : parentsOf)
        for (const auto& p : parents)
            if (parentsOf.count(p))
                ++children[p];
    std::vector<std::string> out;
    std::vector<std::string> stack;
    const std::string tipId = toHex(tip);
    if (parentsOf.count(tipId))
        stack.push_back(tipId);
    while (!stack.empty()) {
        const std::string id = stack.back();
        stack.pop_back();
        for (const auto& p : parentsOf.at(id))
            if (parentsOf.count(p) && --children[p] == 0)
                stack.push_back(p);
        out.push_back(id);
    }
    std::reverse(out.begin(), out.end());
    return out;
}

// Git's labels for a --rebase-merges todo (sequencer.c label_oid): a commit keeps its first label;
// names are sanitized (alphanumerics and UTF-8 kept, other runs become one "-"), made unique with
// "-2", "-3", …; commits outside the range get their unique abbreviated id.
struct Labels {
    git_repository* repo = nullptr;
    std::map<std::string, std::string> byCommit;
    std::set<std::string> used;
    size_t hexSize = 40;

    std::string abbreviation(const std::string& id) const
    {
        git_object* raw = nullptr;
        std::string out = id.substr(0, 7);
        const git_oid oid = *fromHex(id);
        if (git_object_lookup(&raw, repo, &oid, GIT_OBJECT_ANY) == 0) {
            Object obj(raw);
            git_buf buf = GIT_BUF_INIT;
            if (git_object_short_id(&buf, obj.get()) == 0)
                out.assign(buf.ptr, buf.size);
            git_buf_dispose(&buf);
        }
        git_error_clear();
        return out;
    }

    static std::string sanitize(std::string_view text)
    {
        constexpr size_t kMax = 255 - 5 - 16; // GIT_MAX_LABEL_LENGTH: NAME_MAX - ".lock" - 16
        std::string out;
        bool utf8 = true;
        for (size_t i = 0; i < text.size() && out.size() + 1 < kMax; ++i) {
            const unsigned char c = static_cast<unsigned char>(text[i]);
            if (std::isalnum(c) && c < 0x80) {
                out += static_cast<char>(c);
            } else if (c & 0x80) {
                if (!utf8) {
                    out += static_cast<char>(c);
                    continue;
                }
                const size_t len = (c & 0xe0) == 0xc0 ? 2 : (c & 0xf0) == 0xe0 ? 3 : (c & 0xf8) == 0xf0 ? 4 : 0;
                bool valid = len > 0 && i + len <= text.size();
                for (size_t k = 1; valid && k < len; ++k)
                    valid = (static_cast<unsigned char>(text[i + k]) & 0xc0) == 0x80;
                if (!valid) {
                    utf8 = false;
                    out += static_cast<char>(c);
                    continue;
                }
                if (out.size() + len > kMax)
                    break;
                out.append(text.substr(i, len));
                i += len - 1;
            } else if (!out.empty() && out.back() != '-') {
                out += '-';
            }
        }
        return out;
    }

    const std::string& label(const std::string& id, const std::string* text)
    {
        if (auto it = byCommit.find(id); it != byCommit.end())
            return it->second;
        std::string name;
        if (!text) {
            name = abbreviation(id);
            for (size_t n = name.size() + 1; used.count(name) && n <= id.size(); ++n)
                name = id.substr(0, n);
        } else {
            name = sanitize(*text);
            if (name.empty())
                name = "rev-" + abbreviation(id);
            if ((name.size() == hexSize && isHex(name)) || name == "#" || used.count(name)) {
                const std::string base = name;
                for (int k = 2;; ++k) {
                    name = base + "-" + std::to_string(k);
                    if (!used.count(name))
                        break;
                }
            }
        }
        used.insert(name);
        return byCommit[id] = name;
    }
};

// Git's one-line title of a commit (the first paragraph).
std::string oneline(git_repository* repo, const std::string& id)
{
    Commit c = lookupCommit(repo, *fromHex(id));
    const char* s = git_commit_summary(c.get());
    return s ? s : "";
}

// The todo `git rebase -i --rebase-merges` starts with (sequencer.c make_script_with_merges, the
// default no-rebase-cousins mode), without its comments and update-ref rows.
Todo mergesTodo(git_repository* repo, Context& context, const std::vector<std::string>& order,
    const std::set<std::string>& samePatch, const std::optional<git_oid>& upstream)
{
    Labels labels;
    labels.repo = repo;
    labels.hexSize = hexSize(oidType(repo));
    if (upstream) {
        labels.byCommit[toHex(*upstream)] = "onto";
        labels.used.insert("onto");
    }
    const std::set<std::string> interesting(order.begin(), order.end());
    std::map<std::string, std::vector<std::string>> parentsOf;
    for (const auto& id : order) {
        Commit c = lookupCommit(repo, *fromHex(id));
        for (unsigned i = 0; i < git_commit_parentcount(c.get()); ++i)
            parentsOf[id].push_back(toHex(*git_commit_parent_id(c.get(), i)));
    }
    // The branch a merged tip is labelled after: the last local branch there by name.
    std::map<std::string, std::string> decoration;
    forEachReference(repo, [&](git_reference* ref) {
        const std::string name = git_reference_name(ref);
        if (startsWith(name, "refs/heads/") && git_reference_type(ref) == GIT_REFERENCE_DIRECT) {
            std::string& d = decoration[toHex(*git_reference_target(ref))];
            if (name.substr(11) > d)
                d = name.substr(11);
        }
        return true;
    });

    // First phase: a row per commit; merges label the tips they merge.
    std::map<std::string, Item> rowOf;
    std::vector<std::string> tips;
    for (const auto& id : order) {
        const auto& parents = parentsOf[id];
        const std::string title = oneline(repo, id);
        if (parents.size() <= 1) {
            if (samePatch.count(id))
                continue;
            Item pick;
            pick.commit = id;
            pick.subject = context.commits.count(id) ? context.commits.at(id).subject : title;
            rowOf[id] = pick;
            continue;
        }
        std::string fromMessage = title;
        const size_t q1 = startsWith(title, "Merge ") ? title.find('\'', 6) : std::string::npos;
        const size_t q2 = q1 == std::string::npos ? q1 : title.find('\'', q1 + 1);
        if (q2 != std::string::npos) {
            fromMessage = title.substr(q1 + 1, q2 - q1 - 1);
        } else if (startsWith(title, "Merge pull request ")) {
            if (const size_t from = title.find(" from ", 19); from != std::string::npos)
                fromMessage = title.substr(from + 6);
        }
        Item merge;
        merge.action = Action::Merge;
        merge.fixup = FixupMessage::Use;
        merge.commit = id;
        merge.subject = title;
        for (size_t k = 1; k < parents.size(); ++k) {
            const std::string& p = parents[k];
            if (!interesting.count(p)) {
                const std::string& name = labels.label(p, nullptr);
                context.revisions[name] = p;
                merge.arg += (k > 1 ? " " : "") + name;
                continue;
            }
            tips.push_back(p);
            std::string text = fromMessage;
            if (auto d = decoration.find(p); d != decoration.end())
                text = d->second;
            merge.arg += (k > 1 ? " " : "") + labels.label(p, &text);
        }
        rowOf[id] = merge;
    }
    // Second phase: commits with more than one child in the range are branch points; HEAD is a tip.
    {
        std::set<std::string> childSeen;
        const std::string branchPoint = "branch-point";
        for (const auto& id : order)
            for (const auto& p : parentsOf[id])
                if (interesting.count(p) && !childSeen.insert(p).second)
                    labels.label(p, &branchPoint);
        if (!order.empty())
            tips.push_back(order.back());
    }
    // Third phase: from each tip down its first parents to what is already shown, oldest first.
    Todo out;
    Item labelOnto;
    labelOnto.action = Action::Label;
    labelOnto.arg = "onto";
    out.items.push_back(labelOnto);
    const bool rootWithOnto = !upstream && !context.onto.empty();
    std::set<std::string> shown;
    for (const auto& t : tips) {
        if (shown.count(t))
            continue;
        std::vector<std::string> list;
        std::optional<std::string> at = t;
        while (at && interesting.count(*at) && !shown.count(*at)) {
            list.insert(list.begin(), *at);
            const auto& parents = parentsOf[*at];
            if (parents.empty())
                at.reset();
            else
                at = parents.front();
        }
        Item reset;
        reset.action = Action::Reset;
        if (!at) {
            reset.arg = rootWithOnto ? "onto" : kNewRoot;
        } else {
            reset.arg = labels.label(*at, nullptr); // (labelled already unless outside the range)
            if (reset.arg != "onto")
                reset.subject = oneline(repo, *at);
            if (!interesting.count(*at) && reset.arg != "onto")
                context.revisions[reset.arg] = *at;
        }
        out.items.push_back(reset);
        for (const auto& id : list) {
            if (auto r = rowOf.find(id); r != rowOf.end())
                out.items.push_back(r->second);
            if (auto l = labels.byCommit.find(id); l != labels.byCommit.end()) {
                Item label;
                label.action = Action::Label;
                label.arg = l->second;
                out.items.push_back(label);
            }
            shown.insert(id);
        }
    }
    return out;
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
        headRef = headTarget(repo);
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
    std::set<std::string> samePatch; // listed commits whose change is already upstream
    if (upstream && !listed.empty()) {
        std::set<std::string> upstreamIds;
        for (const auto& oid : walk(repo, *upstream, tip))
            if (auto id = patchId(repo, oid))
                upstreamIds.insert(*id);
        if (!upstreamIds.empty())
            std::erase_if(listed, [&](const git_oid& oid) {
                auto id = patchId(repo, oid);
                const bool same = id && upstreamIds.count(*id);
                if (same)
                    samePatch.insert(toHex(oid));
                return same;
            });
    }
    const std::vector<git_oid> tips = remoteTips(repo);
    for (const auto& oid : listed) {
        context.range.push_back(toHex(oid));
        addCommit(repo, context, oid, tips);
    }
    // --rebase-merges: every commit of the range, merges included, in Git's order.
    const std::vector<std::string> graphOrder = graphOrderRange(repo, tip, upstream);
    for (const auto& id : graphOrder) {
        if (context.commits.count(id) || samePatch.count(id))
            continue;
        Commit c = lookupCommit(repo, *fromHex(id));
        if (git_commit_parentcount(c.get()) > 1) {
            context.merges.push_back(id);
            addCommit(repo, context, *fromHex(id), tips);
        }
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
    auto addUpdateRefs = [&](Todo& todo, const std::string& id) {
        auto it = context.branchesAt.find(id);
        if (it == context.branchesAt.end())
            return;
        std::vector<std::string> refs = it->second;
        std::sort(refs.rbegin(), refs.rend());
        for (const auto& ref : refs) {
            if (ref == context.tipRef || context.checkedOutElsewhere.count(ref))
                continue;
            Item update;
            update.action = Action::UpdateRef;
            update.arg = ref;
            todo.items.push_back(update);
        }
    };
    for (const auto& id : context.range) {
        Item pick;
        pick.commit = id;
        pick.subject = context.commits.at(id).subject;
        context.initial.items.push_back(pick);
        addUpdateRefs(context.initial, id);
    }
    for (Item& item : mergesTodo(repo, context, graphOrder, samePatch, upstream).items) {
        const bool commitRow = item.makesCommit();
        const std::string id = item.commit;
        context.initialMerges.items.push_back(std::move(item));
        if (commitRow)
            addUpdateRefs(context.initialMerges, id);
    }
    return context;
}

std::vector<ParseError> expand(git_repository* repo, Todo& todo, Context& context)
{
    assertNotUiThread("todo::expand");
    std::vector<ParseError> problems;
    const std::vector<git_oid> tips = remoteTips(repo);
    const size_t fullSize = hexSize(oidType(repo));
    // Names on reset/merge rows: refs/rewritten/<name> (a label a stopped rebase defined), else a
    // revision, as Git resolves them when no earlier label row defines them.
    auto resolveName = [&](const std::string& name) {
        if (name.empty() || name == kNewRoot || context.revisions.count(name))
            return;
        git_oid oid;
        if (git_reference_name_to_id(&oid, repo, ("refs/rewritten/" + name).c_str()) == 0) {
            context.definedLabels.insert(name);
            context.revisions[name] = toHex(oid);
            return;
        }
        git_error_clear();
        git_object* raw = nullptr;
        if (git_revparse_single(&raw, repo, (name + "^{commit}").c_str()) == 0) {
            Object obj(raw);
            context.revisions[name] = toHex(*git_object_id(obj.get()));
            addCommit(repo, context, *git_object_id(obj.get()), tips);
        }
        git_error_clear();
    };
    for (size_t i = 0; i < todo.items.size(); ++i) {
        Item& item = todo.items[i];
        if (item.action == Action::Reset)
            resolveName(item.arg);
        for (const auto& head : item.mergeHeads())
            resolveName(head);
        if (!item.isCommit() && !(item.action == Action::Merge && !item.commit.empty()))
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

namespace {

// The list git wrote (parse + expand), its commits, merges and branches, into `context`.
void readGitList(git_repository* repo, std::string_view todoText, Context& context, const char* what)
{
    std::vector<ParseError> problems;
    Todo list = parse(todoText, &problems);
    for (auto& p : expand(repo, list, context))
        problems.push_back(std::move(p));
    if (!problems.empty())
        throw std::runtime_error(std::string("cannot read ") + what + ": " + problems.front().message);
    for (const Item& item : list.items) {
        std::vector<std::string>& into = item.isCommit() ? context.range : context.merges;
        if ((item.isCommit() || (item.action == Action::Merge && !item.commit.empty()))
            && std::find(into.begin(), into.end(), item.commit) == into.end())
            into.push_back(item.commit);
    }
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
}

std::string refFromHeadName(const std::string& headName)
{
    const std::string name = std::string(trimRight(trimLeft(headName)));
    return startsWith(name, "refs/heads/") ? name : std::string();
}

} // namespace

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
    context.tipRef = refFromHeadName(headName);
    context.tipIsHead = true;
    readGitList(repo, todoText, context, "the remaining todo");
    return context;
}

Context readStarting(git_repository* repo, std::string_view todoText, const std::string& onto, const std::string& origHead,
    const std::string& headName)
{
    assertNotUiThread("todo::readStarting");
    Context context;
    const auto ontoId = resolve(repo, std::string(trimRight(trimLeft(onto))));
    const auto tipId = resolve(repo, std::string(trimRight(trimLeft(origHead))));
    git_error_clear();
    if (!ontoId || !tipId)
        throw std::runtime_error("cannot read the rebase's onto and orig-head");
    // git does not record the upstream; it only names the pre-rebase hook's argument.
    context.onto = context.upstream = toHex(*ontoId);
    context.tip = toHex(*tipId);
    context.tipRef = refFromHeadName(headName);
    context.tipIsHead = true;
    readGitList(repo, todoText, context, "git's todo");
    if (hasMergeRows(context.initial))
        context.initialMerges = context.initial;
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
        case Action::Label:
            finished = open; // Git finishes the commit before running these (is_final_fixup)
            break;
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

// Git's "#2", "#3", … (the first message has its own wording).
std::string ordinal(size_t n) { return "#" + std::to_string(n); }

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
    for (const Item& item : todo.items)
        if (item.action == Action::Merge && item.fixup == FixupMessage::Edit && !item.commit.empty() && item.message)
            out[item.commit] = *item.message;
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

std::vector<bool> unchangedRows(const Todo& todo, const Context& context)
{
    std::vector<bool> out(todo.items.size(), false);
    // HEAD while replaying: the original commit it still is ("" once it is a new commit, or unknown).
    std::string current = context.onto.empty() ? std::string("-") : context.onto; // "-" = the root
    std::map<std::string, std::string> labels;
    auto original = [&](const std::string& name) -> std::string {
        if (name == kNewRoot)
            return "-";
        if (auto it = labels.find(name); it != labels.end())
            return it->second;
        if (auto it = context.revisions.find(name); it != context.revisions.end())
            return it->second;
        return {};
    };
    auto parentsOf = [&](const std::string& id) {
        auto it = context.commits.find(id);
        return it == context.commits.end() ? std::vector<std::string>{std::string()} : it->second.parents;
    };
    for (size_t i = 0; i < todo.items.size(); ++i) {
        const Item& item = todo.items[i];
        switch (item.action) {
        case Action::Label:
            labels[item.arg] = current;
            break;
        case Action::Reset:
            current = original(item.arg);
            break;
        case Action::Pick: {
            bool same = !current.empty() && !item.message
                && parentsOf(item.commit) == (current == "-" ? std::vector<std::string>{} : std::vector<std::string>{current});
            // A squash/fixup coming next changes this commit too.
            for (size_t j = i + 1; same && j < todo.items.size(); ++j) {
                const Action a = todo.items[j].action;
                if (a == Action::Squash || a == Action::Fixup)
                    same = false;
                if (a != Action::Drop)
                    break;
            }
            out[i] = same;
            current = same ? item.commit : std::string();
            break;
        }
        case Action::Merge: {
            std::vector<std::string> parents{current};
            for (const auto& head : item.mergeHeads())
                parents.push_back(original(head));
            const bool same = item.fixup == FixupMessage::Use && !item.commit.empty() && !current.empty()
                && current != "-" && parents == parentsOf(item.commit);
            out[i] = same;
            current = same ? item.commit : std::string();
            break;
        }
        case Action::Reword:
        case Action::Edit:
        case Action::Squash:
        case Action::Fixup:
            current.clear();
            break;
        default:
            break; // drop, exec, break, update-ref: HEAD stays
        }
    }
    return out;
}

namespace {

// A label name Git accepts (refs/rewritten/<name> is a valid ref name).
bool validLabel(const std::string& name)
{
    int valid = 0;
    const bool ok = git_reference_name_is_valid(&valid, ("refs/rewritten/" + name).c_str()) == 0 && valid;
    git_error_clear();
    return ok;
}

} // namespace

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
            if (item.action == Action::Merge && !item.commit.empty() && item.fixup != FixupMessage::None)
                kept.insert(item.commit);
        }
    }

    // Labels (--rebase-merges): names Git can use, defined before a reset/merge row uses them.
    {
        std::map<std::string, size_t> definedAt; // label → first row defining it
        for (size_t i = 0; i < todo.items.size(); ++i)
            if (todo.items[i].action == Action::Label)
                definedAt.emplace(todo.items[i].arg, i);
        std::set<std::string> defined(context.definedLabels.begin(), context.definedLabels.end());
        auto use = [&](const std::string& name, int row) {
            if (name == kNewRoot || defined.count(name))
                return;
            const auto later = definedAt.find(name);
            const bool revision = context.revisions.count(name) > 0;
            if (later != definedAt.end()) {
                if (revision)
                    add(Severity::Warning, Code::LabelDefinedLater, row,
                        "label '" + name + "' is defined on row " + std::to_string(later->second + 1) + ", further down: here it is "
                            + shortId(context.revisions.at(name)));
                else
                    add(Severity::Error, Code::LabelDefinedLater, row,
                        "label '" + name + "' is used before row " + std::to_string(later->second + 1) + " defines it");
            } else if (!revision) {
                add(Severity::Error, Code::UnknownLabel, row, "unknown label '" + name + "'");
            }
        };
        for (size_t i = 0; i < todo.items.size(); ++i) {
            const Item& item = todo.items[i];
            const int row = static_cast<int>(i);
            if (item.action == Action::Label) {
                const std::string name = std::string(trimRight(trimLeft(item.arg)));
                if (name.empty())
                    add(Severity::Error, Code::BadLabel, row, "label needs a name");
                else if (name != item.arg || !validLabel(name))
                    add(Severity::Error, Code::BadLabel, row, "'" + item.arg + "' is not a valid label");
                else
                    defined.insert(name);
            } else if (item.action == Action::Reset) {
                if (trimLeft(item.arg).empty())
                    add(Severity::Error, Code::BadLabel, row, "reset needs a label");
                else
                    use(item.arg, row);
            } else if (item.action == Action::Merge) {
                const auto heads = item.mergeHeads();
                if (heads.empty())
                    add(Severity::Error, Code::BadLabel, row, "merge needs a label to merge");
                for (const auto& head : heads)
                    use(head, row);
            }
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
        if (hasMergeRows(todo))
            inRange.insert(context.merges.begin(), context.merges.end());
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
        const std::vector<bool> unchanged = unchangedRows(todo, context);
        // (Rows are never removed from the list, only dropped: every listed commit has a row.)
        for (size_t i = 0; i < todo.items.size(); ++i) {
            const Item& item = todo.items[i];
            const bool named = item.isCommit() || (item.action == Action::Merge && !item.commit.empty());
            if (named && !unchanged[i] && context.commits.count(item.commit) && context.commits.at(item.commit).published)
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
            return {Engine::Native, row + " is " + actionName(a) + ": --rebase-merges lists are replayed by git rebase"};
        default:
            break;
        }
    }
    return {Engine::InMemory, "only pick, reword, squash, fixup, drop and update-ref: rewritten in memory, one Undo"};
}

bool hasMergeRows(const Todo& todo)
{
    return std::any_of(todo.items.begin(), todo.items.end(), [](const Item& i) {
        return i.action == Action::Label || i.action == Action::Reset || i.action == Action::Merge;
    });
}

void addExecEach(Todo& todo, const std::string& command)
{
    std::vector<Item> out;
    auto isFollower = [](const Item& item) { return item.action == Action::Squash || item.action == Action::Fixup; };
    for (size_t i = 0; i < todo.items.size(); ++i) {
        const Item& item = todo.items[i];
        out.push_back(item);
        if (!item.makesCommit())
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
    // (The preview replays every row the native engine runs: edit, exec, break, label, reset, merge.)
    if (const auto engine = chooseEngine(todo, {}); !replayStops && engine.engine != Engine::InMemory)
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
    std::map<std::string, std::string> labels; // label → what it names (as `current`)
    auto target = [&](const std::string& name, size_t row) -> std::string {
        if (name == kNewRoot)
            return {};
        if (auto it = labels.find(name); it != labels.end())
            return it->second;
        if (auto it = context.revisions.find(name); it != context.revisions.end())
            return "=" + it->second;
        throw std::runtime_error("row " + std::to_string(row + 1) + ": unknown label '" + name + "'");
    };
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
            if (auto it = byFirst.find(i); it != byFirst.end() && started && !it->second->amends)
                throw NoPreview("row " + std::to_string(i + 1) + " is " + actionName(item.action)
                    + " right after a reset or merge row: the result shows once Start has run it");
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
        case Action::Label:
            labels[item.arg] = current;
            break;
        case Action::Reset:
            current = target(item.arg, i);
            started = true;
            break;
        case Action::Merge: {
            if (current.empty())
                throw NoPreview("row " + std::to_string(i + 1) + " merges onto a new root commit");
            rw::Step s;
            s.kind = rw::Step::Kind::Merge;
            s.key = key;
            s.sourceParents = false;
            s.gitMerge = true;
            s.parents = {current};
            const auto heads = item.mergeHeads();
            for (const auto& head : heads) {
                const std::string t = target(head, i);
                if (t.empty())
                    throw NoPreview("row " + std::to_string(i + 1) + " merges a new root commit");
                s.parents.push_back(t);
            }
            if (!item.commit.empty() && item.fixup != FixupMessage::None) {
                s.source = item.commit; // its message and author
                if (item.fixup == FixupMessage::Edit) {
                    s.forceNew = true; // Git never fast-forwards a merge whose message it edits
                    if (item.message)
                        s.message = cleanup(*item.message);
                }
            } else {
                // Git's message: the text after "#" on the row, else "Merge branch '<labels>'".
                s.message = !item.subject.empty() ? item.subject + "\n"
                                                  : std::string("Merge ") + (heads.size() > 1 ? "branches" : "branch") + " '" + item.arg + "'\n";
                s.forceNew = true;
                s.mapSource = false;
            }
            plan.steps.push_back(std::move(s));
            current = key;
            started = true;
            break;
        }
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
