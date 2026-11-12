// Filter expressions (GG-25). The parser lives in libgg, so the core search uses it too.
#pragma once

#include "libgg/FilterExpr.hpp"

#include <string>

namespace ggui {

using gg::containsNoCaseView;
using gg::FilterExpr;

// Keeps a parsed expression next to the text of an input. Parses again only when the text changes.
class FilterInput {
public:
    std::string text;
    const FilterExpr& expr()
    {
        if (!m_valid || text != m_parsed) {
            m_parsed = text;
            m_expr = FilterExpr(m_parsed);
            m_valid = true;
        }
        return m_expr;
    }

private:
    std::string m_parsed;
    FilterExpr m_expr;
    bool m_valid = false;
};

} // namespace ggui
