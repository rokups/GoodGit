#include "util/FilterExpr.hpp"

#include <algorithm>
#include <cctype>

namespace ggui {

namespace {

char lower(char c) { return static_cast<char>(std::tolower(static_cast<unsigned char>(c))); }

std::string_view trim(std::string_view s)
{
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front())))
        s.remove_prefix(1);
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back())))
        s.remove_suffix(1);
    return s;
}

} // namespace

bool containsNoCaseView(std::string_view haystack, std::string_view needle)
{
    if (needle.empty())
        return true;
    return std::search(haystack.begin(), haystack.end(), needle.begin(), needle.end(),
               [](char a, char b) { return lower(a) == lower(b); })
        != haystack.end();
}

FilterExpr::FilterExpr(std::string_view text)
{
    while (true) {
        const size_t comma = text.find(',');
        std::string_view term = trim(text.substr(0, comma));
        bool negated = false;
        if (!term.empty() && term.front() == '-') {
            negated = true;
            term = trim(term.substr(1));
        }
        if (!term.empty()) {
            Term t;
            t.negated = negated;
            t.text.reserve(term.size());
            for (const char c : term)
                t.text.push_back(lower(c));
            m_terms.push_back(std::move(t));
        }
        if (comma == std::string_view::npos)
            break;
        text.remove_prefix(comma + 1);
    }
}

bool FilterExpr::matches(std::initializer_list<std::string_view> fields) const
{
    for (const Term& t : m_terms) {
        bool hit = false;
        for (const std::string_view f : fields)
            if (containsNoCaseView(f, t.text)) {
                hit = true;
                break;
            }
        if (hit == t.negated)
            return false;
    }
    return true;
}

} // namespace ggui
