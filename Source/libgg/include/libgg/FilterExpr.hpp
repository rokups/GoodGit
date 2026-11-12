// Filter expressions (GG-25): "a, b, -c" lists terms. Each term is a case-insensitive substring.
// A term with a "-" prefix must not match. An item passes when all terms hold.
// A backslash escapes the next character: "\," is a comma, "\-" at the start of a term is a minus,
// "\\" is a backslash and a backslash before a space makes a space that trimming keeps.
// A backslash before any other character stays a backslash. A lone trailing backslash stays.
#pragma once

#include <initializer_list>
#include <string>
#include <string_view>
#include <vector>

namespace gg {

class FilterExpr {
public:
    FilterExpr() = default;
    explicit FilterExpr(std::string_view text);

    // Returns text that parses as one literal term: it matches the strings that contain `text`.
    static std::string escape(std::string_view text);

    // True when the expression has no term. Such an expression includes everything.
    bool empty() const { return m_terms.empty(); }
    // True when the item passes. `fields` are the texts of the item (for example name and id).
    bool matches(std::initializer_list<std::string_view> fields) const;
    bool matches(std::string_view field) const { return matches({field}); }
    // Same as matches() for one field that is already lowercase (ASCII). Faster: no case folding.
    bool matchesLowered(std::string_view lowerField) const;

private:
    struct Term {
        std::string text; // lowercase
        bool negated = false;
    };
    std::vector<Term> m_terms;
};

// ASCII case-insensitive substring test. An empty needle matches.
bool containsNoCaseView(std::string_view haystack, std::string_view needle);

} // namespace gg
