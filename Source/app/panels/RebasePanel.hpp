// Interactive rebase todo editor (REBUILD_PLAN §4.13): a dockable panel with a header (range,
// options, engine and reason, validation, Start / Cancel) above Git's todo list.
//
// The range is read on a worker (gg::todo::read); everything after that (editing, validation,
// messages, engine choice) is pure functions of the todo and the read Context, so the UI thread
// never touches the repository. Start runs the in-memory engine through gg::todo::toPlan and
// Actions::rewrite (one operation, one Undo). Todos that need `git rebase -i` (edit, break,
// exec, "Run as git rebase") are edited and validated here but cannot start until the native
// engine exists (P3-19). The live preview (P3-17) goes beside the list and restarts from
// onTodoChanged().
#pragma once

#include <libgg/Todo.hpp>

#include <functional>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace ggui {

class Session;

namespace panel {
inline constexpr const char* Rebase = "Interactive rebase";
} // namespace panel

class RebasePanel {
public:
    // What to open. Revisions are resolved on the worker.
    struct Request {
        std::string upstream;        // list the commits after it ("" = from the root commit)
        std::string tip = "HEAD";    // branch name, "HEAD" or a revision
        std::string onto;            // new base ("" = upstream)
        // Instead of `upstream`: this commit and its descendants (upstream = its parent).
        std::string from;
        // Instead of `tip`: HEAD when it contains this commit, else the first local branch that does.
        std::string tipContaining;
        std::vector<std::string> selected; // commits selected when the editor opens
        // Adjusts the starting todo ("Open as interactive rebase…" from single actions).
        std::function<void(gg::todo::Todo&, const gg::todo::Context&)> adjust;
    };

    explicit RebasePanel(Session& session);

    void open(Request request);
    void close();
    // A todo is being read or edited (the panel is shown).
    bool isOpen() const { return m_open; }
    bool loading() const { return m_open && !m_state.context; }
    void draw();

    // ---- state (what the editor shows) --------------------------------------------------------
    const gg::todo::Todo& todo() const { return m_state.todo; }
    const gg::todo::Context* context() const { return m_state.context.get(); }
    const std::set<size_t>& selection() const { return m_selection; }
    const gg::todo::Options& options() const { return m_options; }
    bool newestFirst() const { return m_newestFirst; }
    const std::vector<gg::todo::Issue>& issues() const { return m_issues; }
    const gg::todo::EngineChoice& engine() const { return m_engine; }
    // Rows in display order (indexes into todo().items).
    std::vector<size_t> displayOrder() const;
    bool canStart(std::string* reason = nullptr) const;
    // The text of a group's inline message editor (the group whose first row is `row`).
    std::string messageText(size_t row) const;

private:
    // One undo step: the todo with the options that shaped it.
    struct State {
        gg::todo::Todo todo;
        std::shared_ptr<const gg::todo::Context> context;
        bool updateRefs = true;
        bool autosquash = false;
    };

    void read(const Request& request, bool keepTodo);
    void onRead(std::shared_ptr<const gg::todo::Context> context, gg::todo::ReadOptions used, const Request& request,
        bool keepTodo);
    // Applies an edit as one undo step.
    void edit(const std::function<void(State&)>& fn);
    void pushUndo(State before);
    void undo(bool redo);
    void onTodoChanged();
    // Typed messages whose group changed (rows, actions) go back to Git's default.
    static void resetStaleMessages(const gg::todo::Todo& before, gg::todo::Todo& after);

    void setAction(gg::todo::Action action, gg::todo::FixupMessage fixup = gg::todo::FixupMessage::None);
    void insertRow(gg::todo::Action action);
    void removeRows();
    void moveSelection(int delta);  // in todo order
    void moveRows(std::vector<size_t> rows, size_t target);
    void setUpdateRefs(bool on);
    void setAutosquash(bool on);
    void start();

    void drawHeader();
    void drawOptions();
    void drawList();
    void drawRow(size_t row, float messageHeight);
    void handleKeys();
    void clickRow(size_t row);
    void textEdited(bool changed);

    Session& m_session;
    bool m_open = false;
    bool m_focus = false;
    std::uint64_t m_generation = 0;   // results of older reads are ignored
    gg::todo::ReadOptions m_read;     // what the current context was read with (revisions resolved)
    State m_state;
    std::vector<State> m_undo;
    std::vector<State> m_redo;
    std::optional<State> m_textEditBefore; // state when a message or command field became active
    std::set<size_t> m_selection;
    std::optional<size_t> m_anchor;   // Shift-click range start
    std::optional<std::pair<std::vector<size_t>, size_t>> m_pendingMove; // dropped rows, target
    gg::todo::Options m_options;
    bool m_newestFirst = false;
    std::string m_onto;
    std::vector<gg::todo::Issue> m_issues;
    gg::todo::EngineChoice m_engine;
};

} // namespace ggui
