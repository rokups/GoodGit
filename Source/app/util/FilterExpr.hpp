// Filter expressions (GG-25): "a, b, -c" lists terms. Each term is a case-insensitive substring.
// A term with a "-" prefix must not match. An item passes when all terms hold.
#pragma once

#include <initializer_list>
#include <string>
#include <string_view>
#include <vector>

namespace ggui {

class FilterExpr {
public:
    FilterExpr() = default;
    explicit FilterExpr(std::string_view text);

    // True when the expression has no term. Such an expression includes everything.
    bool empty() const { return m_terms.empty(); }
    // True when the item passes. `fields` are the texts of the item (for example name and id).
    bool matches(std::initializer_list<std::string_view> fields) const;
    bool matches(std::string_view field) const { return matches({field}); }

private:
    struct Term {
        std::string text; // lowercase
        bool negated = false;
    };
    std::vector<Term> m_terms;
};

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

// ASCII case-insensitive substring test. An empty needle matches.
bool containsNoCaseView(std::string_view haystack, std::string_view needle);

} // namespace ggui
