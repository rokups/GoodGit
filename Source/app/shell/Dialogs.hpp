// Modal dialogs (docs/spec/ui-spec.md §9). Every dialog is a Form: a title (the popup window
// name, used by tests), an optional message, fields and buttons. One dialog is shown at a
// time; later ones queue.
#pragma once

#include <functional>
#include <string>
#include <vector>

namespace ggui {

class Session;

struct Form;

struct Field {
    enum Kind { Text, Password, Multiline, Check, Combo, Info };
    Kind kind = Text;
    std::string id;           // widget id: "##<id>"
    std::string label;
    std::string text;
    bool checked = false;
    int choice = 0;
    std::vector<std::string> options;
    std::string hint;
    bool filterable = false;  // Combo: a filter field at the top of the list (Enter picks the first match)
    std::string filter;
    // Shown only while this returns true (empty: always); a hidden field keeps its value.
    std::function<bool(const Form&)> visible;
};

struct FormButton {
    std::string label;
    std::function<void(Form&)> action; // may be empty (Cancel)
    std::function<bool(const Form&)> enabled;
    // Icon shown before the label; nullptr: chosen from the label's verb (Cancel, Delete, Push...).
    const char* icon = nullptr;
};

struct Form {
    std::string title;
    std::string message;
    const char* icon = nullptr; // shown before the message (error popups)
    std::vector<Field> fields;
    std::vector<FormButton> buttons;
    // Rows with a "Reveal" button (Push refused): text + commit id.
    std::vector<std::pair<std::string, std::string>> revealRows;
    std::function<void(const std::string&)> onReveal;

    Field& add(Field f)
    {
        fields.push_back(std::move(f));
        return fields.back();
    }
    const Field* field(const std::string& id) const;
    std::string text(const std::string& id) const;
    bool checked(const std::string& id) const;
    int choice(const std::string& id) const;
};

class Dialogs {
public:
    void open(Form form);
    void draw();
    bool anyOpen() const { return !m_queue.empty(); }
    const Form* current() const { return m_queue.empty() ? nullptr : &m_queue.front(); }
    void closeAll() { m_queue.clear(); m_opened = false; }

    // Specific dialogs with non-trivial content. `message` is the MutationError's message (why
    // the push was refused: first-class conflicts or broken conflict markers).
    void pushRefused(Session& session, const std::string& message, const std::string& detail);

private:
    std::vector<Form> m_queue;
    bool m_opened = false;
    bool m_focusFirst = false;
};

} // namespace ggui
