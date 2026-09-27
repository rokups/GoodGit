// Blame panel (REBUILD_PLAN §4.6).
#pragma once

#include "shell/Session.hpp"

#include <core/Engine.hpp>

#include <string>
#include <vector>

namespace ggui {

class BlamePanel {
public:
    explicit BlamePanel(Session& session);
    // Opens a new blame (pushes the current one to the back history).
    void open(const std::string& path, const core::Oid& commit, bool before = false, int line = 0);
    void back();
    void forward();
    void onBlame(const core::BlameEvent& event);
    void draw(bool* open);
    const core::BlamePtr& blame() const { return m_blame; }
    const core::BlameQuery* query() const { return m_pos >= 0 ? &m_history[static_cast<size_t>(m_pos)] : nullptr; }
    int selectionFirst() const { return m_selFirst; }
    int selectionLast() const { return m_selLast; }

private:
    void request();
    void drawLineMenu(int index);
    std::string blockText(int index, int* first, int* last) const;

    Session& m_session;
    std::vector<core::BlameQuery> m_history;
    int m_pos = -1;
    core::RequestId m_request = 0;
    core::BlamePtr m_blame;
    bool m_loading = false;
    std::string m_filter;
    int m_selFirst = -1;
    int m_selLast = -1;
    int m_scrollTo = 0;
};

} // namespace ggui
