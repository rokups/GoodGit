#include "panels/RebasePanel.hpp"

#include "panels/Graph.hpp"
#include "shell/Actions.hpp"
#include "shell/App.hpp"
#include "shell/Session.hpp"
#include "shell/Theme.hpp"
#include "util/Ui.hpp"

#include <core/Types.hpp>
#include <libgg/Git2.hpp>
#include <libgg/GitRunner.hpp>

#include "shell/Widgets.hpp"

#include <IconsMaterialSymbols.h>
#include <imgui.h>
#include <imgui_internal.h>
#include <imgui_stdlib.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <map>
#include <span>
#include <sstream>
#include <stdexcept>

namespace ggui {

namespace todo = gg::todo;
using todo::Action;
using todo::FixupMessage;

namespace {

// Action combo entries (fixup -C / -c are fixup with a message choice).
struct ActionEntry {
    const char* label;
    Action action;
    FixupMessage fixup;
};
constexpr ActionEntry kActions[] = {
    {"pick", Action::Pick, FixupMessage::None},     {"reword", Action::Reword, FixupMessage::None},
    {"edit", Action::Edit, FixupMessage::None},     {"squash", Action::Squash, FixupMessage::None},
    {"fixup", Action::Fixup, FixupMessage::None},   {"fixup -C", Action::Fixup, FixupMessage::Use},
    {"fixup -c", Action::Fixup, FixupMessage::Edit}, {"drop", Action::Drop, FixupMessage::None},
};
// A merge row made from a merge: recreate it with its message (-C), edit that message (-c), or
// Git's "Merge branch '…'" (merge).
constexpr ActionEntry kMergeActions[] = {
    {"merge -C", Action::Merge, FixupMessage::Use},
    {"merge -c", Action::Merge, FixupMessage::Edit},
    {"merge", Action::Merge, FixupMessage::None},
};

std::string actionLabel(const todo::Item& item)
{
    // (Only fixup and merge rows carry a message choice.)
    std::string name = todo::actionName(item.action);
    if (item.action == Action::Merge && item.commit.empty())
        return name;
    if (item.fixup == FixupMessage::Use)
        return name + " -C";
    if (item.fixup == FixupMessage::Edit)
        return name + " -c";
    return name;
}

// A merge row's inline message editor: `merge -c` of a merge.
bool editsMergeMessage(const todo::Item& item)
{
    return item.action == Action::Merge && item.fixup == FixupMessage::Edit && !item.commit.empty();
}

std::string shortHex(const std::string& id, size_t n) { return id.substr(0, n); }

std::string branchName(const std::string& ref) { return ref.rfind("refs/heads/", 0) == 0 ? ref.substr(11) : ref; }

// HEAD when it is (or descends from) `commit`, else the first local branch (by name) that does.
std::string tipContaining(git_repository* repo, const std::string& commit)
{
    const git_oid target = *gg::git2::fromHex(commit);
    auto contains = [&](const git_oid& tip) {
        const bool yes = git_oid_equal(&tip, &target) == 1 || git_graph_descendant_of(repo, &tip, &target) == 1;
        git_error_clear();
        return yes;
    };
    git_oid head;
    if (git_reference_name_to_id(&head, repo, "HEAD") == 0 && contains(head))
        return "HEAD";
    git_error_clear();
    std::vector<std::pair<std::string, git_oid>> branches;
    gg::git2::forEachReference(repo, [&](git_reference* ref) {
        const std::string name = git_reference_name(ref);
        if (name.rfind("refs/heads/", 0) == 0 && git_reference_type(ref) == GIT_REFERENCE_DIRECT)
            branches.emplace_back(name.substr(11), *git_reference_target(ref));
        return true;
    });
    std::sort(branches.begin(), branches.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
    for (const auto& [name, tip] : branches)
        if (contains(tip))
            return name;
    throw std::runtime_error("commit " + commit.substr(0, 10) + " is not on HEAD or a local branch");
}

// What identifies a squash group for its typed message: its rows' actions and commits.
std::map<std::string, std::string> groupSignatures(const todo::Todo& t)
{
    std::map<std::string, std::string> out;
    for (const auto& g : todo::groups(t)) {
        std::string sig = actionLabel(t.items[g.first]) + " " + t.items[g.first].commit + "\n";
        for (size_t row : g.followers)
            sig += actionLabel(t.items[row]) + " " + t.items[row].commit + "\n";
        out[t.items[g.first].commit] = sig;
    }
    return out;
}

} // namespace

RebasePanel::RebasePanel(Session& session) : m_session(session) { }

RebasePanel::~RebasePanel()
{
    // The repository closes while git waits for the list: git gets nothing (Cancel).
    if (m_sequence && m_sequence->done)
        m_sequence->done(std::nullopt);
}

// ---- opening ------------------------------------------------------------------------------------

void RebasePanel::open(Request request)
{
    if (editingForGit()) {
        // git waits for the list shown: another todo would leave it without an answer.
        m_focus = true;
        m_session.app().notify(App::Notice::Warning, "Todo editor in use",
            "git rebase -i waits for the list in the todo editor: Save or Cancel it first.");
        return;
    }
    m_open = true;
    m_focus = true;
    m_state = State{};
    m_undo.clear();
    m_redo.clear();
    m_selection.clear();
    m_anchor.reset();
    m_options = todo::Options{};
    m_onto.clear();
    m_issues.clear();
    m_engine = {};
    cancelPreview();
    m_preview.reset();
    m_previewGraph.clear();
    m_previewNote.clear();
    m_previewsRequested = m_previewsShown = 0;
    m_remaining = request.remaining;
    m_remainingText.clear();
    m_sequence = request.sequence;
    read(request, false);
}

void RebasePanel::close()
{
    if (m_sequence) {
        // Cancel or closing the panel: git gets nothing back.
        auto done = std::move(m_sequence->done);
        m_sequence.reset();
        if (done)
            done(std::nullopt);
    }
    m_open = false;
    ++m_generation;
    m_state = State{};
    m_undo.clear();
    m_redo.clear();
    m_selection.clear();
    m_textEditBefore.reset();
    cancelPreview();
    m_preview.reset();
    m_previewGraph.clear();
}

void RebasePanel::read(const Request& request, bool keepTodo)
{
    struct Result {
        std::shared_ptr<const todo::Context> context;
        todo::ReadOptions used;
        std::string remainingText;
    };
    auto result = std::make_shared<Result>();
    const std::uint64_t generation = ++m_generation;
    if (!keepTodo) {
        // A new list: nothing of the previous one (its range, its base) may be used until this
        // read arrives; the panel shows it is reading. (Otherwise Enter in Onto meanwhile
        // re-read the old range, and this read's result was dropped as outdated.)
        m_state.context.reset();
        m_read = {};
    }
    if (request.sequence) {
        // Read already, by the sequence editor link (off the UI thread).
        onRead(request.sequence->context, {}, request, false);
        return;
    }
    if (request.remaining) {
        // The rest of the stopped git rebase -i, from git's own files (read only).
        m_session.actions().run(
            "read the remaining rebase todo",
            [result](core::MutationContext& ctx) {
                const std::filesystem::path dir = std::filesystem::path(git_repository_path(ctx.repo())) / "rebase-merge";
                auto readText = [](const std::filesystem::path& path) {
                    std::ifstream in(path, std::ios::binary);
                    std::ostringstream out;
                    out << in.rdbuf();
                    return out.str();
                };
                if (!std::filesystem::exists(dir / "interactive"))
                    throw std::runtime_error("no interactive rebase is in progress");
                result->remainingText = readText(dir / "git-rebase-todo");
                result->context = std::make_shared<const todo::Context>(
                    todo::readRemaining(ctx.repo(), result->remainingText, readText(dir / "head-name")));
            },
            [this, result, generation, request](const core::MutationFinishedEvent& e) {
                if (generation != m_generation)
                    return;
                if (e.outcome != core::Outcome::Ok) {
                    close();
                    m_session.app().showError("Open interactive rebase", e.message);
                    return;
                }
                m_remainingText = result->remainingText;
                onRead(result->context, {}, request, false);
            },
            false, false, false);
        return;
    }
    todo::ReadOptions options;
    options.upstream = request.upstream;
    options.tip = request.tip;
    options.onto = request.onto;
    const std::string from = request.from;
    const std::string tipOf = request.tipContaining;
    m_session.actions().run(
        "read the interactive rebase range",
        [result, options, from, tipOf](core::MutationContext& ctx) mutable {
            git_repository* repo = ctx.repo();
            if (!from.empty()) {
                const auto oid = gg::git2::resolve(repo, from);
                git_error_clear();
                if (!oid)
                    throw std::runtime_error("unknown revision '" + from + "'");
                gg::git2::Commit c = gg::git2::lookupCommit(repo, *oid);
                options.upstream = git_commit_parentcount(c.get()) > 0 ? gg::git2::toHex(*git_commit_parent_id(c.get(), 0)) : "";
                if (tipOf.empty())
                    options.tip = tipContaining(repo, gg::git2::toHex(*oid));
            }
            if (!tipOf.empty())
                options.tip = tipContaining(repo, tipOf);
            // Read with update-ref lines and without autosquash: the options apply them to the list.
            result->context = std::make_shared<const todo::Context>(todo::read(repo, options));
            result->used = options;
        },
        [this, result, generation, request, keepTodo](const core::MutationFinishedEvent& e) {
            if (generation != m_generation)
                return; // closed or replaced meanwhile
            if (e.outcome != core::Outcome::Ok) {
                if (!keepTodo)
                    close();
                m_session.app().showError("Open interactive rebase", e.message);
                return;
            }
            onRead(result->context, result->used, request, keepTodo);
        },
        false, false, false);
}

void RebasePanel::onRead(std::shared_ptr<const todo::Context> context, todo::ReadOptions used, const Request& request,
    bool keepTodo)
{
    m_read = std::move(used);
    if (keepTodo) {
        // A new base: the list stays, the context (onto) changes.
        edit([&](State& s) { s.context = context; });
        return;
    }
    m_state.context = std::move(context);
    m_state.todo = m_state.context->initial;
    // git's own list: merges mode when git wrote a --rebase-merges list.
    if (m_sequence)
        m_state.rebaseMerges = todo::hasMergeRows(m_state.todo);
    if (request.adjust)
        request.adjust(m_state.todo, *m_state.context);
    m_onto.clear();
    for (size_t i = 0; i < m_state.todo.items.size(); ++i)
        if (std::find(request.selected.begin(), request.selected.end(), m_state.todo.items[i].commit) != request.selected.end())
            m_selection.insert(i);
    onTodoChanged();
}

// ---- editing ------------------------------------------------------------------------------------

void RebasePanel::pushUndo(State before)
{
    m_undo.push_back(std::move(before));
    m_redo.clear();
}

void RebasePanel::edit(const std::function<void(State&)>& fn)
{
    State before = m_state;
    fn(m_state);
    resetStaleMessages(before.todo, m_state.todo);
    if (m_state.todo == before.todo && m_state.context == before.context && m_state.updateRefs == before.updateRefs
        && m_state.autosquash == before.autosquash && m_state.rebaseMerges == before.rebaseMerges)
        return;
    pushUndo(std::move(before));
    onTodoChanged();
}

void RebasePanel::undo(bool redo)
{
    auto& from = redo ? m_redo : m_undo;
    auto& to = redo ? m_undo : m_redo;
    if (from.empty())
        return;
    to.push_back(m_state);
    m_state = std::move(from.back());
    from.pop_back();
    std::erase_if(m_selection, [&](size_t row) { return row >= m_state.todo.items.size(); });
    onTodoChanged();
}

void RebasePanel::onTodoChanged()
{
    m_options.updateRefs = m_state.updateRefs;
    m_options.autosquash = m_state.autosquash;
    m_options.rebaseMerges = m_state.rebaseMerges;
    m_issues = todo::validate(m_state.todo, *m_state.context);
    if (m_sequence)
        m_engine = {todo::Engine::Native,
            m_sequence->remaining ? "git rebase --edit-todo is waiting for this list" : "git rebase -i is waiting for this list"};
    else
        m_engine = m_remaining ? todo::EngineChoice{todo::Engine::Native, "the rest of the rebase in progress"}
                               : todo::chooseEngine(m_state.todo, m_options);
    startPreview();
}

// ---- live preview -------------------------------------------------------------------------------

void RebasePanel::cancelPreview()
{
    if (m_previewRequest != 0)
        m_session.engine().cancel(m_previewRequest);
    m_previewRequest = 0;
}

void RebasePanel::startPreview()
{
    // The newest list wins: the request before it is cancelled (and its answer ignored).
    cancelPreview();
    if (todo::hasErrors(m_issues)) {
        m_preview.reset();
        m_previewGraph.clear();
        m_previewNote = "Fix the errors in the list to see the result.";
        return;
    }
    m_previewNote.clear();
    m_pendingTodo = m_state.todo;
    m_previewRequest = m_session.engine().rebasePreview(m_state.todo, m_state.context, m_options);
    ++m_previewsRequested;
}

void RebasePanel::onPreview(const core::RebasePreviewEvent& event)
{
    if (!m_open || event.request != m_previewRequest)
        return; // superseded or closed
    m_previewRequest = 0;
    m_preview = event.preview;
    m_previewTodo = m_pendingTodo;
    ++m_previewsShown;
    layoutPreviewGraph();
}

void RebasePanel::layoutPreviewGraph()
{
    // The resulting commits newest first, then the base: one lane for a straight history, more
    // for the branches and merges of a --rebase-merges list. Parents outside the result (commits
    // a merge keeps from outside the range) get no edge.
    m_previewGraph.clear();
    m_previewLanes = 1;
    const auto& rows = m_preview->rows;
    const std::string base = m_preview->onto.empty() ? std::string("(root)") : m_preview->onto;
    std::set<std::string> shown{base};
    for (const auto& r : rows)
        shown.insert(r.id);
    std::vector<std::optional<std::string>> lanes;
    auto freeLane = [&] {
        for (size_t l = 0; l < lanes.size(); ++l)
            if (!lanes[l])
                return static_cast<int>(l);
        lanes.emplace_back();
        return static_cast<int>(lanes.size() - 1);
    };
    auto expecting = [&](const std::string& id) {
        for (size_t l = 0; l < lanes.size(); ++l)
            if (lanes[l] == id)
                return static_cast<int>(l);
        return -1;
    };
    auto place = [&](const std::string& id, const std::vector<std::string>& parents, bool conflicted) {
        core::HistoryRow g;
        g.conflicted = conflicted;
        int lane = expecting(id);
        const bool fromAbove = lane >= 0;
        if (!fromAbove)
            lane = freeLane();
        for (size_t l = 0; l < lanes.size(); ++l) {
            if (!lanes[l])
                continue;
            const auto li = static_cast<std::int16_t>(l);
            if (*lanes[l] == id) {
                if (static_cast<int>(l) != lane || fromAbove)
                    g.lines.push_back(core::GraphLine{li, static_cast<std::int16_t>(lane), 0, 1, static_cast<std::uint8_t>(l)});
                lanes[l].reset();
            } else {
                g.lines.push_back(core::GraphLine{li, li, 0, 2, static_cast<std::uint8_t>(l)});
            }
        }
        const auto me = static_cast<std::int16_t>(lane);
        bool first = true;
        for (const auto& p : parents) {
            const std::string& parent = p;
            g.parents.push_back(core::Oid{});
            if (!shown.count(parent))
                continue;
            int to = expecting(parent);
            if (to < 0) {
                to = first && !lanes[static_cast<size_t>(lane)] ? lane : freeLane();
                lanes[static_cast<size_t>(to)] = parent;
            }
            g.lines.push_back(core::GraphLine{me, static_cast<std::int16_t>(to), 1, 2, static_cast<std::uint8_t>(to)});
            first = false;
        }
        g.lane = lane;
        g.color = static_cast<std::uint8_t>(lane);
        m_previewLanes = std::max(m_previewLanes, static_cast<int>(lanes.size()));
        m_previewGraph.push_back(std::move(g));
    };
    for (size_t k = rows.size(); k-- > 0;)
        place(rows[k].id, rows[k].parents, !rows[k].conflicts.empty());
    place(base, {}, false);
}

void RebasePanel::onTaskFinished(const core::TaskFinishedEvent& event)
{
    if (event.request != m_previewRequest || !(event.cancelled || event.failed))
        return;
    // Cancelled from the toolbar (or failed): the last preview stays, marked as out of date.
    m_previewRequest = 0;
    m_previewNote = event.cancelled ? "The preview was cancelled." : "The preview failed.";
}

void RebasePanel::resetStaleMessages(const todo::Todo& before, todo::Todo& after)
{
    const auto oldSigs = groupSignatures(before);
    const auto newSigs = groupSignatures(after);
    std::set<size_t> firsts;
    for (const auto& g : todo::groups(after))
        firsts.insert(g.first);
    for (size_t i = 0; i < after.items.size(); ++i) {
        auto& item = after.items[i];
        if (!item.message)
            continue;
        if (item.action == Action::Merge) { // its own message while it stays `merge -c`
            if (!editsMergeMessage(item))
                item.message.reset();
            continue;
        }
        const auto was = oldSigs.find(item.commit);
        const auto now = newSigs.find(item.commit);
        if (!firsts.count(i) || was == oldSigs.end() || now == newSigs.end() || was->second != now->second)
            item.message.reset();
    }
}

void RebasePanel::setAction(Action action, FixupMessage fixup)
{
    edit([&](State& s) {
        for (size_t row : m_selection) {
            auto& item = s.todo.items[row];
            if (!item.isCommit())
                continue;
            item.action = action;
            item.fixup = fixup;
        }
    });
}

void RebasePanel::insertRow(Action action)
{
    size_t at = m_state.todo.items.size();
    if (!m_selection.empty())
        at = *m_selection.rbegin() + 1;
    edit([&](State& s) {
        todo::Item item;
        item.action = action;
        // Label: a name no row uses yet; reset: the new base; merge: the nearest label above.
        if (action == Action::Label) {
            std::set<std::string> used;
            for (const auto& i : s.todo.items)
                if (i.action == Action::Label)
                    used.insert(i.arg);
            item.arg = "label";
            for (int k = 2; used.count(item.arg); ++k)
                item.arg = "label-" + std::to_string(k);
        } else if (action == Action::Reset) {
            item.arg = "onto";
        } else if (action == Action::Merge) {
            for (size_t k = at; k-- > 0;)
                if (s.todo.items[k].action == Action::Label && s.todo.items[k].arg != "onto") {
                    item.arg = s.todo.items[k].arg;
                    break;
                }
        }
        s.todo.items.insert(s.todo.items.begin() + static_cast<std::ptrdiff_t>(at), item);
    });
    m_selection = {at};
    m_anchor = at;
}

void RebasePanel::removeRows()
{
    std::vector<size_t> rows;
    for (size_t row : m_selection)
        if (!m_state.todo.items[row].isCommit())
            rows.push_back(row);
    if (rows.empty())
        return;
    edit([&](State& s) {
        for (auto it = rows.rbegin(); it != rows.rend(); ++it)
            s.todo.items.erase(s.todo.items.begin() + static_cast<std::ptrdiff_t>(*it));
    });
    m_selection.clear();
    m_anchor.reset();
}

void RebasePanel::moveSelection(int delta)
{
    if (m_selection.empty())
        return;
    const size_t n = m_state.todo.items.size();
    if ((delta < 0 && *m_selection.begin() == 0) || (delta > 0 && *m_selection.rbegin() + 1 >= n))
        return;
    std::vector<size_t> rows(m_selection.begin(), m_selection.end());
    edit([&](State& s) {
        auto& items = s.todo.items;
        if (delta < 0)
            for (size_t row : rows)
                std::swap(items[row], items[row - 1]);
        else
            for (auto it = rows.rbegin(); it != rows.rend(); ++it)
                std::swap(items[*it], items[*it + 1]);
    });
    std::set<size_t> moved;
    for (size_t row : rows)
        moved.insert(delta < 0 ? row - 1 : row + 1);
    m_selection = std::move(moved);
    m_anchor.reset();
}

void RebasePanel::moveRows(std::vector<size_t> rows, size_t target)
{
    std::sort(rows.begin(), rows.end());
    if (std::find(rows.begin(), rows.end(), target) != rows.end())
        return;
    // Below the moved rows the target's place is after it, above them before it.
    const bool after = target > rows.back();
    const size_t removedBefore = static_cast<size_t>(std::count_if(rows.begin(), rows.end(), [&](size_t r) { return r < target; }));
    const size_t at = target - removedBefore + (after ? 1 : 0);
    edit([&](State& s) {
        std::vector<todo::Item> moving;
        for (auto it = rows.rbegin(); it != rows.rend(); ++it) {
            moving.insert(moving.begin(), s.todo.items[*it]);
            s.todo.items.erase(s.todo.items.begin() + static_cast<std::ptrdiff_t>(*it));
        }
        s.todo.items.insert(s.todo.items.begin() + static_cast<std::ptrdiff_t>(at), moving.begin(), moving.end());
    });
    m_selection.clear();
    for (size_t i = 0; i < rows.size(); ++i)
        m_selection.insert(at + i);
    m_anchor.reset();
}

void RebasePanel::setUpdateRefs(bool on)
{
    edit([&](State& s) {
        s.updateRefs = on;
        auto& items = s.todo.items;
        if (!on) {
            std::erase_if(items, [](const todo::Item& i) { return i.action == Action::UpdateRef; });
            return;
        }
        // Git's update-ref lines for the branches at each commit (or merge), after its squash/fixup rows.
        std::map<std::string, std::vector<todo::Item>> refsAt;
        std::string last;
        for (const auto& item : (s.rebaseMerges ? s.context->initialMerges : s.context->initial).items) {
            if (item.isCommit() || item.action == Action::Merge)
                last = item.commit;
            else if (item.action == Action::UpdateRef)
                refsAt[last].push_back(item);
        }
        for (size_t i = items.size(); i-- > 0;) {
            auto it = refsAt.find(items[i].commit);
            if (!(items[i].isCommit() || items[i].action == Action::Merge) || items[i].commit.empty() || it == refsAt.end())
                continue;
            size_t at = i + 1;
            while (at < items.size() && (items[at].action == Action::Squash || items[at].action == Action::Fixup))
                ++at;
            items.insert(items.begin() + static_cast<std::ptrdiff_t>(at), it->second.begin(), it->second.end());
            refsAt.erase(it);
        }
    });
    m_selection.clear();
}

void RebasePanel::setAutosquash(bool on)
{
    edit([&](State& s) {
        s.autosquash = on;
        if (on) {
            todo::autosquash(s.todo, *s.context);
            return;
        }
        // Off: back to Git's starting list.
        s.todo = baseList(s);
    });
    m_selection.clear();
}

todo::Todo RebasePanel::baseList(const State& s)
{
    todo::Todo list = s.rebaseMerges ? s.context->initialMerges : s.context->initial;
    if (!s.updateRefs)
        std::erase_if(list.items, [](const todo::Item& i) { return i.action == Action::UpdateRef; });
    if (s.autosquash)
        todo::autosquash(list, *s.context);
    return list;
}

void RebasePanel::setRebaseMerges(bool on)
{
    // Git's list with (or without) the merges: label, reset and merge rows keep the branches' shape.
    edit([&](State& s) {
        s.rebaseMerges = on;
        s.todo = baseList(s);
    });
    m_selection.clear();
    m_anchor.reset();
}

std::vector<size_t> RebasePanel::displayOrder() const
{
    std::vector<size_t> out(m_state.todo.items.size());
    for (size_t i = 0; i < out.size(); ++i)
        out[i] = m_newestFirst ? out.size() - 1 - i : i;
    return out;
}

std::string RebasePanel::messageText(size_t row) const
{
    if (const auto& item = m_state.todo.items[row]; editsMergeMessage(item)) {
        const auto info = m_state.context->commits.find(item.commit);
        return item.message ? *item.message : info != m_state.context->commits.end() ? info->second.message : std::string();
    }
    const auto group = todo::groupAt(m_state.todo, row);
    if (!group)
        return {};
    return todo::editorText(m_state.todo, *group, *m_state.context);
}

bool RebasePanel::canStart(std::string* reason) const
{
    auto fail = [&](const std::string& why) {
        if (reason)
            *reason = why;
        return false;
    };
    // (Saving for a waiting git needs no repository access: a running ggui action may be the git
    // that waits.)
    if (!m_sequence && !m_session.actions().busy().empty())
        return fail(m_session.actions().busyTooltip());
    if (todo::hasErrors(m_issues))
        return fail("Fix the errors in the list first");
    return true;
}

void RebasePanel::start()
{
    if (m_sequence)
        return saveForGit();
    if (m_remaining)
        return saveRemaining();
    if (m_engine.engine == todo::Engine::Native)
        return startNative();
    const todo::Todo list = m_state.todo;
    const auto context = m_state.context;
    const bool keepDate = m_options.keepCommitterDate;
    const gg::rewrite::Emptied emptied = m_options.emptied;
    const std::uint64_t generation = m_generation;
    m_session.actions().rewrite(
        "interactive rebase",
        [list, context, keepDate, emptied](git_repository* repo) {
            // The branch must still be where the editor read it.
            git_oid now;
            const std::string ref = !context->tipRef.empty() ? context->tipRef : std::string("HEAD");
            const bool found = git_reference_name_to_id(&now, repo, ref.c_str()) == 0;
            git_error_clear();
            if ((!context->tipRef.empty() || context->tipIsHead) && (!found || gg::git2::toHex(now) != context->tip))
                throw std::runtime_error(branchName(ref) + " moved since the list was read; open the editor again");
            gg::rewrite::Plan plan = todo::toPlan(list, *context);
            plan.keepCommitterDate = keepDate;
            plan.emptied = emptied;
            return plan;
        },
        [this, generation](const core::MutationFinishedEvent& e) {
            if (generation != m_generation)
                return;
            if (e.outcome == core::Outcome::Ok) {
                if (!e.message.empty())
                    m_session.app().notify(App::Notice::Warning, "Interactive rebase autostash", e.message);
                close();
            } else if (e.outcome != core::Outcome::Cancelled) {
                m_session.app().showError("Start interactive rebase", e.detail.empty() ? e.message : e.detail);
            }
        },
        m_options.autostash);
}

void RebasePanel::startNative()
{
    const todo::Context& c = *m_state.context;
    todo::Todo list = m_state.todo;
    if (const std::string each = gg::trim(m_options.execEach); !each.empty())
        todo::addExecEach(list, each);
    Actions::NativeRebase r;
    r.prepared.todo = todo::format(list);
    r.prepared.messages = todo::editorMessages(list);
    r.empty = m_options.emptied == gg::rewrite::Emptied::Keep ? "keep"
        : m_options.emptied == gg::rewrite::Emptied::Drop     ? "drop"
                                                               : "stop";
    r.updateRefs = std::any_of(list.items.begin(), list.items.end(), [](const todo::Item& i) { return i.action == Action::UpdateRef; });
    if (m_options.autostash)
        r.args.push_back("--autostash");
    if (todo::hasMergeRows(list))
        r.args.push_back("--rebase-merges");
    if (c.upstream.empty()) {
        r.args.push_back("--root");
        if (!c.onto.empty()) {
            r.args.push_back("--onto");
            r.args.push_back(c.onto);
        }
    } else {
        if (c.onto != c.upstream) {
            r.args.push_back("--onto");
            r.args.push_back(c.onto);
        }
        r.args.push_back(c.upstream);
    }
    // The branch to rebase when it is not HEAD's (git switches to it first).
    if (!c.tipIsHead)
        r.args.push_back(c.tipRef.empty() ? c.tip : branchName(c.tipRef));
    r.checkTip = !c.tipRef.empty() || c.tipIsHead;
    r.tipRef = c.tipRef;
    r.tip = c.tip;
    const std::uint64_t generation = m_generation;
    m_session.actions().nativeRebase(std::move(r), [this, generation](const core::MutationFinishedEvent& e) {
        if (generation != m_generation)
            return;
        if (e.outcome == core::Outcome::Ok) {
            close();
            if (!e.message.empty())
                m_session.app().notify(App::Notice::Info, "Interactive rebase stopped", e.message);
        } else if (e.outcome != core::Outcome::Cancelled) {
            m_session.app().showError("Start interactive rebase", e.detail.empty() ? e.message : e.detail);
        }
    });
}

void RebasePanel::saveRemaining()
{
    const std::uint64_t generation = m_generation;
    m_session.actions().editRemainingTodo(m_remainingText, todo::format(m_state.todo), todo::editorMessages(m_state.todo),
        [this, generation](const core::MutationFinishedEvent& e) {
            if (generation != m_generation)
                return;
            if (e.outcome == core::Outcome::Ok)
                close();
            else if (e.outcome != core::Outcome::Cancelled)
                m_session.app().showError("Save the remaining todo", e.detail.empty() ? e.message : e.detail);
        });
}

void RebasePanel::saveForGit()
{
    const std::string text = todo::format(m_state.todo);
    auto done = std::move(m_sequence->done);
    m_sequence.reset();
    close();
    if (done)
        done(text);
}

// ---- drawing ------------------------------------------------------------------------------------

void RebasePanel::draw()
{
    if (!m_open)
        return;
    if (m_focus) {
        // Opens as a tab next to History unless the user docked it elsewhere.
        const ImGuiWindow* self = ImGui::FindWindowByName(panel::Rebase);
        const ImGuiWindow* history = ImGui::FindWindowByName(panel::History);
        if ((!self || self->DockId == 0) && history && history->DockId != 0)
            ImGui::SetNextWindowDockID(history->DockId);
        ImGui::SetNextWindowFocus();
        m_focus = false;
    }
    bool open = true;
    ImGui::SetNextWindowSize(ImVec2(ImGui::GetFontSize() * 50, ImGui::GetFontSize() * 30), ImGuiCond_FirstUseEver);
    const bool visible = ImGui::Begin(panel::Rebase, &open);
    if (!open) {
        ImGui::End();
        close(); // closing the panel cancels the rebase
        return;
    }
    if (!visible) {
        ImGui::End();
        return;
    }
    if (!m_state.context) {
        spinner("##ir_loading", ImGui::GetFontSize() * 0.4f);
        ImGui::SameLine();
        ImGui::TextDisabled("Reading the commits...");
        if (button(ICON_MS_CLOSE, "Cancel###ir_cancel"))
            close();
        ImGui::End();
        return;
    }
    drawHeader();
    if (!m_open) { // cancelled
        ImGui::End();
        return;
    }
    drawOptions();
    ImGui::Separator();
    drawList();
    handleKeys();
    ImGui::End();
}

void RebasePanel::drawHeader()
{
    const todo::Context& c = *m_state.context;
    const Palette& p = theme().palette();
    const size_t n = m_session.shortIdLength();
    size_t commits = 0, merges = 0;
    for (const auto& item : m_state.todo.items) {
        commits += item.isCommit() ? 1 : 0;
        merges += item.action == Action::Merge ? 1 : 0;
    }
    const std::string tip = !c.tipRef.empty() ? branchName(c.tipRef) : c.tipIsHead ? "HEAD" : shortHex(c.tip, n);
    std::string title = "Rebase " + std::to_string(commits) + " commit(s) "
        + (merges ? "and " + std::to_string(merges) + " merge(s) " : std::string()) + "of " + tip + " onto "
        + (c.onto.empty() ? std::string("the root") : shortHex(c.onto, n));
    if (m_sequence && !m_sequence->remaining)
        title = "git rebase -i: " + title;
    if (m_remaining || (m_sequence && m_sequence->remaining))
        title = "Remaining todo of the rebase of " + (c.tipRef.empty() ? std::string("detached HEAD") : tip) + ": "
            + std::to_string(commits) + " commit(s) onto HEAD " + shortHex(c.onto, n);
    plainText((title + "###ir_title").c_str());

    // Start / Cancel, as in a modal dialog's button row.
    std::string reason;
    const bool startable = canStart(&reason);
    ImGui::SameLine();
    ImGui::BeginDisabled(!startable);
    const bool gitsList = m_remaining || m_sequence;
    if (ImGui::Button(gitsList ? ICON_MS_SAVE " Save###ir_start" : ICON_MS_PLAY_ARROW " Start###ir_start"))
        start();
    if (m_sequence && startable && ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
        ImGui::SetTooltip("Hand the list to %s, which goes on with it", m_sequence->remaining ? "git rebase --edit-todo" : "git rebase -i");
    else if (m_remaining && startable && ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
        ImGui::SetTooltip("Write git-rebase-todo (through git rebase --edit-todo); Continue goes on from there");
    ImGui::EndDisabled();
    if (!startable && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        ImGui::SetTooltip("%s", reason.c_str());
    ImGui::SameLine();
    const bool cancel = button(ICON_MS_CLOSE, "Cancel###ir_cancel");
    if (m_sequence && ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
        ImGui::SetTooltip("%s", m_sequence->remaining ? "git rebase --edit-todo keeps the list as it was"
                                                      : "git rebase -i stops and nothing changes (git gets an empty list)");
    if (cancel) {
        close();
        return;
    }

    // Engine (R3) and why.
    const bool memory = m_engine.engine == todo::Engine::InMemory;
    plainText((std::string("Engine: ") + (memory ? "in memory" : "git rebase") + " (" + m_engine.reason + ")###ir_engine").c_str());
    if (m_sequence)
        plainText("git opens its own editor for reword, squash and merge -c messages.###ir_git_note");

    // Validation.
    for (size_t i = 0; i < m_issues.size(); ++i) {
        const auto& issue = m_issues[i];
        ImGui::PushStyleColor(ImGuiCol_Text, issue.error() ? p.errorText : p.warning);
        std::string text = std::string(issue.error() ? ICON_MS_ERROR " " : ICON_MS_WARNING " ")
            + (issue.row >= 0 ? "Row " + std::to_string(issue.row + 1) + ": " : std::string()) + issue.message;
        plainText((text + "###ir_issue_" + std::to_string(i)).c_str());
        ImGui::PopStyleColor();
    }
}

void RebasePanel::drawOptions()
{
    if (!m_remaining && !m_sequence)
        drawRunOptions();
    drawTools();
}

void RebasePanel::drawRunOptions()
{
    const bool native = m_engine.engine == todo::Engine::Native;
    // The options flow like words: an item that does not fit in the rest of the row starts the next one.
    const float field = ImGui::GetFontSize() * 12;
    ImGui::SetNextItemWidth(field);
    bool applyOnto = ImGui::InputTextWithHint("Onto###ir_onto", m_state.context->upstream.empty() ? "the root" : "the upstream",
        &m_onto, ImGuiInputTextFlags_EnterReturnsTrue);
    applyOnto = acceptCommitDrop(m_onto) || applyOnto;
    if (applyOnto) {
        Request r;
        r.upstream = m_read.upstream;
        r.tip = m_read.tip;
        r.onto = gg::trim(m_onto);
        read(r, true);
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("New base (branch, tag or commit); Enter applies. Empty = the upstream.");
    sameLineIfFits(checkboxWidth("Autosquash"));
    bool autosquash = m_state.autosquash;
    if (ImGui::Checkbox("Autosquash###ir_autosquash", &autosquash))
        setAutosquash(autosquash);
    sameLineIfFits(checkboxWidth("Update refs"));
    bool updateRefs = m_state.updateRefs;
    if (ImGui::Checkbox("Update refs###ir_update_refs", &updateRefs))
        setUpdateRefs(updateRefs);
    sameLineIfFits(checkboxWidth("Rebase merges"));
    bool rebaseMerges = m_state.rebaseMerges;
    if (ImGui::Checkbox("Rebase merges###ir_rebase_merges", &rebaseMerges))
        setRebaseMerges(rebaseMerges);
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
        ImGui::SetTooltip("git rebase --rebase-merges: keep the merges. The list gets label, reset and merge rows "
                          "(Git's list, so the edits so far are replaced) and runs through git rebase.");
    sameLineIfFits(checkboxWidth("Autostash"));
    ImGui::Checkbox("Autostash###ir_autostash", &m_options.autostash);
    sameLineIfFits(checkboxWidth("Run as git rebase"));
    if (ImGui::Checkbox("Run as git rebase###ir_native", &m_options.runAsGitRebase))
        onTodoChanged();

    ImGui::SetNextItemWidth(field);
    if (ImGui::InputTextWithHint("Exec after every commit###ir_exec_each", "command", &m_options.execEach))
        onTodoChanged();
    const char* dates[] = {"Use now", "Keep original"};
    const float dateWidth = comboWidth({dates[0], dates[1]});
    sameLineIfFits(labelledWidth(dateWidth, "Committer date"));
    ImGui::SetNextItemWidth(dateWidth);
    // git rebase always sets the committer date to now (--committer-date-is-author-date is another
    // thing): Keep original is for the in-memory engine only.
    int date = m_options.keepCommitterDate && !native ? 1 : 0;
    ImGui::BeginDisabled(native);
    if (ImGui::Combo("Committer date###ir_committer_date", &date, dates, 2))
        m_options.keepCommitterDate = date == 1;
    ImGui::EndDisabled();
    if (native && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        ImGui::SetTooltip("git rebase sets the committer date to now; Keep original needs the in-memory engine.");
    const char* emptyChoices[] = {"Keep", "Drop", "Ask"}; // gg::rewrite::Emptied order
    const float emptyWidth = comboWidth({emptyChoices[0], emptyChoices[1], emptyChoices[2]});
    sameLineIfFits(labelledWidth(emptyWidth, "Becoming empty"));
    ImGui::SetNextItemWidth(emptyWidth);
    int emptied = static_cast<int>(m_options.emptied);
    if (ImGui::Combo("Becoming empty###ir_empty", &emptied, emptyChoices, 3)) {
        m_options.emptied = static_cast<gg::rewrite::Emptied>(emptied);
        onTodoChanged();
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Commits whose changes are already in the new base (git rebase --empty): keep them as "
                          "empty commits, drop them, or ask at Start (Git's default).");
}

void RebasePanel::drawTools()
{
    // List tools.
    ImGui::BeginDisabled(m_undo.empty());
    if (ImGui::Button(ICON_MS_UNDO "###ir_undo"))
        undo(false);
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        ImGui::SetTooltip("Undo (Ctrl+Z)");
    sameLineIfFits(ImGui::GetFrameHeight());
    ImGui::BeginDisabled(m_redo.empty());
    if (ImGui::Button(ICON_MS_REDO "###ir_redo"))
        undo(true);
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        ImGui::SetTooltip("Redo (Ctrl+Y)");
    sameLineIfFits(buttonWidth(ICON_MS_TERMINAL, "Insert exec"));
    if (button(ICON_MS_TERMINAL, "Insert exec###ir_insert_exec"))
        insertRow(Action::Exec);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("After the selected rows (x)");
    sameLineIfFits(buttonWidth(ICON_MS_PAUSE, "Insert break"));
    if (button(ICON_MS_PAUSE, "Insert break###ir_insert_break"))
        insertRow(Action::Break);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("After the selected rows (b)");
    if (m_state.rebaseMerges || todo::hasMergeRows(m_state.todo)) {
        struct Insert {
            const char* icon;
            const char* label;
            Action action;
            const char* tip;
        };
        constexpr Insert inserts[] = {
            {ICON_MS_LABEL, "Insert label###ir_insert_label", Action::Label, "Name the commit made so far, after the selected rows (l)"},
            {ICON_MS_RESTART_ALT, "Insert reset###ir_insert_reset", Action::Reset, "Go back to a label (onto = the new base), after the selected rows (t)"},
            {ICON_MS_MERGE, "Insert merge###ir_insert_merge", Action::Merge, "Merge a label into the commit made so far, after the selected rows (m)"},
        };
        for (const auto& in : inserts) {
            sameLineIfFits(buttonWidth(in.icon, in.label));
            if (button(in.icon, in.label))
                insertRow(in.action);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("%s", in.tip);
        }
    }
    sameLineIfFits(checkboxWidth("Newest first"));
    ImGui::Checkbox("Newest first###ir_newest_first", &m_newestFirst);
}

void RebasePanel::drawList()
{
    // The list, and the live preview to the right of it.
    const float avail = ImGui::GetContentRegionAvail().x;
    const float previewWidth = std::max(ImGui::GetFontSize() * 16, avail * 0.38f);
    const float listWidth = std::max(ImGui::GetFontSize() * 10, avail - previewWidth - ImGui::GetStyle().ItemSpacing.x);
    const ImGuiTableFlags flags = ImGuiTableFlags_ScrollY | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable
        | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_SizingFixedFit;
    if (!ImGui::BeginTable("##ir_table", 5, flags, ImVec2(listWidth, 0)))
        return;
    ImGui::TableSetupScrollFreeze(0, 1);
    ImGui::TableSetupColumn("Action", ImGuiTableColumnFlags_WidthFixed, ImGui::GetFontSize() * 7);
    ImGui::TableSetupColumn("ID");
    ImGui::TableSetupColumn("Subject", ImGuiTableColumnFlags_WidthStretch);
    ImGui::TableSetupColumn("Author");
    ImGui::TableSetupColumn("Date");
    ImGui::TableHeadersRow();
    const float messageHeight = ImGui::GetTextLineHeight() * 6;
    for (size_t row : displayOrder())
        drawRow(row, messageHeight);
    ImGui::EndTable();
    ImGui::SameLine();
    drawPreview();
    if (m_pendingMove) {
        auto [rows, target] = std::move(*m_pendingMove);
        m_pendingMove.reset();
        moveRows(std::move(rows), target);
    }
}

void RebasePanel::drawPreview()
{
    ImGui::BeginChild("##ir_preview", ImVec2(0, 0), ImGuiChildFlags_Borders);
    const Palette& p = theme().palette();
    const size_t n = m_session.shortIdLength();
    ImGui::TextUnformatted("Result");
    if (m_previewRequest != 0) {
        ImGui::SameLine();
        spinner("##irp_busy", ImGui::GetFontSize() * 0.3f);
        ImGui::SameLine();
        ImGui::TextDisabled("Updating...");
    }
    if (!m_previewNote.empty())
        ImGui::TextWrapped("%s", m_previewNote.c_str());
    if (!m_preview) {
        ImGui::EndChild();
        return;
    }
    const core::RebasePreview& pv = *m_preview;
    if (!pv.ok && pv.unsupported) {
        // A list the in-memory replay cannot model: not an error, Start (git rebase) still runs it.
        plainText(("No preview: " + pv.error + "###irp_unsupported").c_str());
        ImGui::EndChild();
        return;
    }
    if (!pv.ok) {
        ImGui::PushStyleColor(ImGuiCol_Text, p.errorText);
        ImGui::TextWrapped("Cannot compute the result: %s", pv.error.c_str());
        ImGui::PopStyleColor();
        ImGui::EndChild();
        return;
    }

    // Summary: what needs attention, and which branches move.
    size_t conflicted = 0, resolved = 0, decisions = 0, empty = 0;
    for (const auto& row : pv.rows) {
        conflicted += row.conflicts.empty() ? 0 : 1;
        resolved += row.resolved.empty() ? 0 : 1;
        decisions += row.decisions.empty() ? 0 : 1;
        empty += row.empty ? 1 : 0;
    }
    std::string summary = std::to_string(pv.rows.size()) + " commit(s)";
    if (conflicted)
        summary += ", " + std::to_string(conflicted) + " with conflicts";
    if (resolved)
        summary += ", " + std::to_string(resolved) + " resolve conflicts";
    if (decisions)
        summary += ", " + std::to_string(decisions) + " need a decision";
    if (empty)
        summary += ", " + std::to_string(empty) + " empty";
    plainText((summary + "###irp_summary").c_str());
    std::string moves;
    for (const auto& mv : pv.moves)
        moves += (moves.empty() ? "" : ", ") + mv.ref;
    if (!moves.empty())
        plainText(("Moves: " + moves + "###irp_moves").c_str());
    if (!pv.staying.empty()) {
        std::string staying;
        for (const auto& b : pv.staying)
            staying += (staying.empty() ? "" : ", ") + b;
        ImGui::PushStyleColor(ImGuiCol_Text, p.warning);
        plainText(("Stay on the old commits: " + staying + "###irp_staying").c_str());
        ImGui::PopStyleColor();
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Without an update-ref row these branches keep pointing at the commits before the rebase.");
    }
    for (size_t k = 0; k < pv.aside.size(); ++k) {
        const auto& a = pv.aside[k];
        plainText((a.branch + ": " + shortHex(a.id, n) + " " + a.subject + ", before the squash###irp_aside_" + std::to_string(k)).c_str());
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Its update-ref row comes before squash/fixup rows: the branch keeps the commit as it was "
                              "then, and the squash/fixup amends a copy (as git rebase -i does).");
    }
    if (!pv.droppedEmpty.empty()) {
        std::string dropped;
        for (const auto& subject : pv.droppedEmpty)
            dropped += (dropped.empty() ? "" : ", ") + subject;
        plainText(("Dropped, became empty: " + dropped + "###irp_dropped_empty").c_str());
    }

    const ImGuiTableFlags flags = ImGuiTableFlags_ScrollY | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingFixedFit;
    if (!ImGui::BeginTable("##irp_table", 2, flags)) {
        ImGui::EndChild();
        return;
    }
    const float laneWidth = ImGui::GetFontSize() * 0.9f;
    const float rowHeight = ImGui::GetTextLineHeight() + ImGui::GetStyle().CellPadding.y * 2;
    ImGui::TableSetupColumn("##irp_graph", ImGuiTableColumnFlags_WidthFixed,
        graph::inset(laneWidth) + laneWidth * (static_cast<float>(m_previewLanes) + 0.5f));
    ImGui::TableSetupColumn("##irp_commit", ImGuiTableColumnFlags_WidthStretch);
    const todo::Context& c = *m_state.context;
    const std::string tipName = c.tipRef.empty() ? std::string("HEAD") : branchName(c.tipRef);
    auto badges = [&](const std::vector<std::string>& names) {
        for (const auto& b : names) {
            const bool current = b == tipName && c.tipIsHead;
            drawBadge((b + "###irp_badge_" + b).c_str(), b == "HEAD" ? p.head : current ? p.branchCurrent : p.branch, current);
            ImGui::SameLine();
        }
    };
    for (size_t k = pv.rows.size(); k-- > 0;) {
        const auto& row = pv.rows[k];
        ImGui::TableNextRow(ImGuiTableRowFlags_None, rowHeight);
        ImGui::TableSetColumnIndex(0);
        ImGui::PushID(static_cast<int>(k));
        const ImVec2 cellStart = ImGui::GetCursorScreenPos();
        // Clicking a result selects its rows in the list.
        if (selectable(("###irp_row_" + std::to_string(k)).c_str(), m_selection.count(row.todoRow) > 0,
                ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowOverlap)) {
            m_selection.clear();
            for (size_t i = 0; i < m_state.todo.items.size(); ++i)
                if ((m_state.todo.items[i].isCommit() || m_state.todo.items[i].action == Action::Merge)
                    && std::find(row.sources.begin(), row.sources.end(), m_state.todo.items[i].commit) != row.sources.end())
                    m_selection.insert(i);
            if (row.merge)
                m_selection.insert(row.todoRow);
            m_anchor = row.todoRow;
        }
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) {
            std::string tip = row.unchanged ? "Unchanged: " + shortHex(row.id, n) : "New commit from";
            if (!row.unchanged)
                for (const auto& src : row.sources)
                    tip += " " + shortHex(src, n);
            if (!row.conflicts.empty()) {
                tip += row.newConflicts ? "\nFirst-class conflicts in:" : "\nConflicts carried along in:";
                for (const auto& [path, sides] : row.conflicts)
                    tip += "\n    " + path + (sides > 2 ? " (" + std::to_string(sides) + " sides)" : "");
            }
            if (!row.resolved.empty()) {
                tip += "\nConflicts resolved in:";
                for (const auto& path : row.resolved)
                    tip += "\n    " + path;
            }
            if (!row.decisions.empty()) {
                tip += "\nNeeds a decision before it can be written (Start asks):";
                for (const auto& d : row.decisions)
                    tip += "\n    " + d.path + " (" + d.kind + ")";
            }
            if (row.empty)
                tip += row.wasEmpty ? "\nAn empty commit (it was empty before)."
                    : m_options.emptied == gg::rewrite::Emptied::Ask
                    ? "\nBecomes empty: its changes are already in the base. Start asks whether to keep it."
                    : "\nBecomes empty: its changes are already in the base.";
            ImGui::SetTooltip("%s", tip.c_str());
        }
        const bool head = c.tipIsHead
            && std::find_if(row.branches.begin(), row.branches.end(), [&](const std::string& b) { return b == tipName; }) != row.branches.end();
        graph::drawCell(m_previewGraph[pv.rows.size() - 1 - k], laneWidth, rowHeight, cellStart, head);

        ImGui::TableSetColumnIndex(1);
        if (!row.conflicts.empty()) {
            ImGui::PushStyleColor(ImGuiCol_Text, p.conflict);
            ImGui::TextUnformatted(ICON_MS_WARNING);
            ImGui::PopStyleColor();
            ImGui::SameLine(0, 2);
        }
        if (!row.decisions.empty()) {
            ImGui::PushStyleColor(ImGuiCol_Text, p.warning);
            ImGui::TextUnformatted(ICON_MS_HELP);
            ImGui::PopStyleColor();
            ImGui::SameLine(0, 2);
        }
        if (row.empty) {
            ImGui::TextDisabled("(empty)");
            ImGui::SameLine();
        }
        badges(row.branches);
        ImU32 color = ImGui::GetColorU32(ImGuiCol_Text);
        if (!row.conflicts.empty())
            color = p.conflict;
        else if (row.unchanged)
            color = p.dim;
        ImGui::PushStyleColor(ImGuiCol_Text, color);
        ImGui::TextUnformatted(row.subject.c_str());
        ImGui::PopStyleColor();
        ImGui::PopID();
    }
    // The base the commits go onto.
    ImGui::TableNextRow(ImGuiTableRowFlags_None, rowHeight);
    ImGui::TableSetColumnIndex(0);
    if (!pv.onto.empty())
        graph::drawCell(m_previewGraph.back(), laneWidth, rowHeight, ImGui::GetCursorScreenPos(),
            c.tipIsHead && std::find(pv.ontoBranches.begin(), pv.ontoBranches.end(), tipName) != pv.ontoBranches.end());
    ImGui::TableSetColumnIndex(1);
    badges(pv.ontoBranches);
    if (pv.onto.empty())
        ImGui::TextDisabled("(the root)");
    else
        ImGui::TextDisabled("%s %s", shortHex(pv.onto, n).c_str(), pv.ontoSubject.c_str());
    ImGui::EndTable();
    ImGui::EndChild();
}

void RebasePanel::clickRow(size_t row)
{
    const ImGuiIO& io = ImGui::GetIO();
    if (io.KeyShift && m_anchor) {
        m_selection.clear();
        for (size_t i = std::min(row, *m_anchor); i <= std::max(row, *m_anchor); ++i)
            m_selection.insert(i);
        return;
    }
    if (io.KeyCtrl) {
        if (!m_selection.erase(row))
            m_selection.insert(row);
    } else {
        m_selection = {row};
    }
    m_anchor = row;
}

void RebasePanel::drawRow(size_t row, float messageHeight)
{
    const todo::Context& c = *m_state.context;
    const Palette& p = theme().palette();
    todo::Item& item = m_state.todo.items[row];
    const size_t n = m_session.shortIdLength();
    const auto info = c.commits.find(item.commit);
    const bool merge = item.action == Action::Merge;
    // Every commit of the list is in the context (a merge row's merge too).
    const bool hasInfo = item.isCommit() || (merge && info != c.commits.end());
    const std::string key = item.isCommit() ? item.commit
        : merge && !item.commit.empty() ? "merge_" + item.commit
                                        : "row_" + std::to_string(row);
    ImGui::PushID(static_cast<int>(row));
    ImGui::TableNextRow();

    // Selection, drag and drop: the whole row. The selectable is only text-high while the row is
    // as high as its action combo, so the highlight is painted as the row background instead.
    ImGui::TableSetColumnIndex(1);
    ImGui::AlignTextToFramePadding();
    const std::string idText = hasInfo ? shortHex(item.commit, n) : std::string();
    const bool selected = m_selection.count(row) > 0;
    ImGui::PushStyleColor(ImGuiCol_Header, IM_COL32_BLACK_TRANS);
    ImGui::PushStyleColor(ImGuiCol_HeaderHovered, IM_COL32_BLACK_TRANS);
    ImGui::PushStyleColor(ImGuiCol_HeaderActive, IM_COL32_BLACK_TRANS);
    const bool clicked = ImGui::Selectable((idText + "###ir_" + key).c_str(), selected,
        ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowOverlap);
    ImGui::PopStyleColor(3);
    const bool rowHovered = ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenOverlappedByItem);
    if (selected)
        ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg1, rowHovered ? p.selectionHovered : p.selection);
    else if (rowHovered)
        ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg1, ImGui::GetColorU32(ImGuiCol_HeaderHovered));
    if (clicked)
        clickRow(row);
    if (ImGui::BeginDragDropSource()) {
        const std::string payload = std::to_string(row);
        ImGui::SetDragDropPayload("GG_TODO_ROW", payload.data(), payload.size());
        ImGui::Text("%s %s", actionLabel(item).c_str(), hasInfo ? info->second.subject.c_str() : item.arg.c_str());
        ImGui::EndDragDropSource();
    }
    if (ImGui::BeginDragDropTarget()) {
        if (const ImGuiPayload* pl = ImGui::AcceptDragDropPayload("GG_TODO_ROW")) {
            const size_t from = std::stoul(std::string(static_cast<const char*>(pl->Data), static_cast<size_t>(pl->DataSize)));
            // The selected rows move together when the drag starts on one of them.
            std::vector<size_t> rows{from};
            if (m_selection.count(from))
                rows.assign(m_selection.begin(), m_selection.end());
            m_pendingMove = std::make_pair(rows, row); // after the table: rows shift

        }
        ImGui::EndDragDropTarget();
    }
    if (hasInfo && ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal))
        idTooltip(item.commit, n, info->second.authorName + " <" + info->second.authorEmail + ">");

    // Action.
    ImGui::TableSetColumnIndex(0);
    ImGui::SetNextItemWidth(-1);
    if (item.isCommit() || (merge && !item.commit.empty())) {
        const std::string current = actionLabel(item);
        // Combos report no item info to the test engine: register the label (tests find it).
        {
            [[maybe_unused]] ImGuiContext& g = *ImGui::GetCurrentContext();
            const std::string comboLabel = "###ir_action_" + key;
            IMGUI_TEST_ENGINE_ITEM_INFO(ImGui::GetID(comboLabel.c_str()), comboLabel.c_str(), ImGuiItemStatusFlags_None);
        }
        if (ImGui::BeginCombo(("###ir_action_" + key).c_str(), current.c_str())) {
            for (const auto& e : merge ? std::span<const ActionEntry>(kMergeActions) : std::span<const ActionEntry>(kActions))
                if (selectable(e.label, current == e.label)) {
                    const Action a = e.action;
                    const FixupMessage f = e.fixup;
                    edit([&](State& s) {
                        s.todo.items[row].action = a;
                        s.todo.items[row].fixup = f;
                        if (a == Action::Merge && f == FixupMessage::None)
                            s.todo.items[row].subject.clear(); // (Git would take it as the message)
                    });
                }
            ImGui::EndCombo();
        }
    } else {
        ImGui::TextUnformatted(todo::actionName(item.action));
    }

    // Subject (with branch badges), or the row's argument.
    ImGui::TableSetColumnIndex(2);
    for (const auto& issue : m_issues)
        if (issue.row == static_cast<int>(row)) {
            ImGui::PushStyleColor(ImGuiCol_Text, issue.error() ? p.errorText : p.warning);
            ImGui::TextUnformatted(issue.error() ? ICON_MS_ERROR : ICON_MS_WARNING);
            ImGui::PopStyleColor();
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("%s", issue.message.c_str());
            ImGui::SameLine();
            break;
        }
    if (hasInfo && (item.isCommit() || item.fixup != FixupMessage::None))
        if (auto b = c.branchesAt.find(item.commit); b != c.branchesAt.end())
            for (const auto& ref : b->second) {
                const bool current = ref == c.tipRef;
                drawBadge((branchName(ref) + "###ir_badge_" + branchName(ref)).c_str(), current ? p.branchCurrent : p.branch, current);
                ImGui::SameLine();
            }
    if (item.isCommit()) {
        ImGui::PushStyleColor(ImGuiCol_Text, item.action == Action::Drop ? p.dim : ImGui::GetColorU32(ImGuiCol_Text));
        ImGui::TextUnformatted(info->second.subject.c_str());
        ImGui::PopStyleColor();
    } else if (item.action == Action::Label) {
        drawArgField(row, "ir_label_", "label name", ImGui::GetFontSize() * 10);
    } else if (item.action == Action::Reset) {
        drawArgField(row, "ir_reset_", "label", ImGui::GetFontSize() * 10);
        ImGui::SameLine();
        const std::string& to = m_state.todo.items[row].arg;
        ImGui::TextDisabled("%s", to == "onto" ? "(the new base)"
                : to == todo::kNewRoot            ? "(a new root commit)"
                                                  : item.subject.c_str());
    } else if (merge) {
        drawArgField(row, "ir_merge_", "labels to merge", ImGui::GetFontSize() * 10);
        ImGui::SameLine();
        const std::string& heads = m_state.todo.items[row].arg;
        const std::string plain = std::string("Merge ") + (heads.find(' ') != std::string::npos ? "branches" : "branch") + " '" + heads + "'";
        if (item.fixup == FixupMessage::None || item.commit.empty()) // Git's message for a new merge
            ImGui::TextDisabled("%s", item.subject.empty() ? plain.c_str() : item.subject.c_str());
        else
            ImGui::TextUnformatted(hasInfo ? info->second.subject.c_str() : item.subject.c_str());
    } else if (item.action == Action::Exec) {
        ImGui::SetNextItemWidth(-1);
        std::string command = item.arg;
        const bool changed = ImGui::InputTextWithHint(("###ir_exec_" + std::to_string(row)).c_str(), "shell command", &command);
        textEdited(changed);
        if (changed) {
            m_state.todo.items[row].arg = command;
            onTodoChanged();
        }
    } else if (item.action == Action::UpdateRef) {
        drawBadge((branchName(item.arg) + "###ir_ref_" + branchName(item.arg)).c_str(), p.branch);
    }

    // Author and date.
    if (hasInfo) {
        ImGui::TableSetColumnIndex(3);
        ImGui::TextUnformatted(info->second.authorName.c_str());
        ImGui::TableSetColumnIndex(4);
        ImGui::TextUnformatted(dateText(info->second.authorTime).c_str());
    }

    // Inline message editor: reword rows and squash groups (the group's message, prefilled the
    // way Git's editor would be), and `merge -c` rows (the merge's message).
    if (editsMergeMessage(item) && !m_sequence) {
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(2);
        std::string text = messageText(row);
        const bool changed = ImGui::InputTextMultiline(("###ir_msg_" + item.commit).c_str(), &text, ImVec2(-1, messageHeight));
        textEdited(changed);
        if (changed) {
            m_state.todo.items[row].message = text;
            onTodoChanged();
        }
    }
    if (item.isCommit() && item.action != Action::Drop && !m_sequence) {
        const auto group = todo::groupAt(m_state.todo, row);
        if (group && group->first == row && (item.action == Action::Reword || group->needsEditor)) {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(2);
            std::string text = todo::editorText(m_state.todo, *group, c);
            const bool changed = ImGui::InputTextMultiline(("###ir_msg_" + item.commit).c_str(), &text, ImVec2(-1, messageHeight));
            textEdited(changed);
            if (changed) {
                m_state.todo.items[row].message = text;
                onTodoChanged();
            }
        }
    }
    ImGui::PopID();
}

void RebasePanel::drawArgField(size_t row, const char* id, const char* hint, float width)
{
    // A label/reset/merge row's names, edited in place (one undo step per editing session).
    ImGui::SetNextItemWidth(width);
    std::string value = m_state.todo.items[row].arg;
    const bool changed = ImGui::InputTextWithHint(("###" + std::string(id) + std::to_string(row)).c_str(), hint, &value);
    textEdited(changed);
    if (changed) {
        m_state.todo.items[row].arg = value;
        onTodoChanged();
    }
}

void RebasePanel::textEdited(bool changed)
{
    // One undo step per editing session of a field: the state from when it became active.
    if (ImGui::IsItemActivated())
        m_textEditBefore = m_state;
    if (changed && m_textEditBefore) {
        pushUndo(std::move(*m_textEditBefore));
        m_textEditBefore.reset();
    }
}

void RebasePanel::handleKeys()
{
    if (!ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows))
        return;
    // Undo / redo inside the editor (ahead of the global repository Undo while focused).
    if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_Z, ImGuiInputFlags_RouteFocused))
        undo(false);
    if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_Y, ImGuiInputFlags_RouteFocused)
        || ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_Z, ImGuiInputFlags_RouteFocused))
        undo(true);
    const ImGuiIO& io = ImGui::GetIO();
    if (io.WantTextInput || io.KeyCtrl)
        return;
    if (io.KeyAlt) {
        // Alt+arrow is ours: no menu layer toggle when Alt goes up.
        const ImGuiID owner = ImGui::GetID("##ir_keys");
        ImGui::SetKeyOwner(ImGuiKey_LeftAlt, owner);
        ImGui::SetKeyOwner(ImGuiKey_RightAlt, owner);
        // Up/down as displayed.
        const int up = m_newestFirst ? +1 : -1;
        if (ImGui::IsKeyPressed(ImGuiKey_UpArrow))
            moveSelection(up);
        else if (ImGui::IsKeyPressed(ImGuiKey_DownArrow))
            moveSelection(-up);
        return;
    }
    struct Key {
        ImGuiKey key;
        Action action;
    };
    constexpr Key keys[] = {{ImGuiKey_P, Action::Pick}, {ImGuiKey_R, Action::Reword}, {ImGuiKey_E, Action::Edit},
        {ImGuiKey_S, Action::Squash}, {ImGuiKey_F, Action::Fixup}, {ImGuiKey_D, Action::Drop}};
    for (const auto& k : keys)
        if (ImGui::IsKeyPressed(k.key, false))
            setAction(k.action);
    if (ImGui::IsKeyPressed(ImGuiKey_X, false))
        insertRow(Action::Exec);
    if (ImGui::IsKeyPressed(ImGuiKey_B, false))
        insertRow(Action::Break);
    // Git's letters for label (l), reset (t) and merge (m).
    if (ImGui::IsKeyPressed(ImGuiKey_L, false))
        insertRow(Action::Label);
    if (ImGui::IsKeyPressed(ImGuiKey_T, false))
        insertRow(Action::Reset);
    if (ImGui::IsKeyPressed(ImGuiKey_M, false))
        insertRow(Action::Merge);
    if (ImGui::IsKeyPressed(ImGuiKey_Delete, false))
        removeRows();
}

} // namespace ggui
