#include "libgg/FilterExpr.hpp"

#include <algorithm>
#include <cctype>

namespace gg {

namespace {

char lower(char c) { return static_cast<char>(std::tolower(static_cast<unsigned char>(c))); }

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
    struct Ch {
        char c;
        bool escaped;
    };
    std::vector<Ch> term;
    const auto flush = [&] {
        const auto isSpace = [](const Ch& x) { return !x.escaped && std::isspace(static_cast<unsigned char>(x.c)); };
        size_t b = 0;
        size_t e = term.size();
        while (b < e && isSpace(term[b]))
            ++b;
        while (e > b && isSpace(term[e - 1]))
            --e;
        bool negated = false;
        if (b < e && !term[b].escaped && term[b].c == '-') {
            negated = true;
            ++b;
            while (b < e && isSpace(term[b]))
                ++b;
        }
        if (b < e) {
            Term t;
            t.negated = negated;
            t.text.reserve(e - b);
            for (size_t i = b; i < e; ++i)
                t.text.push_back(lower(term[i].c));
            m_terms.push_back(std::move(t));
        }
        term.clear();
    };
    for (size_t i = 0; i < text.size(); ++i) {
        const char c = text[i];
        if (c == ',') {
            flush();
        } else if (c == '\\' && i + 1 < text.size()) {
            const char n = text[i + 1];
            if (n == ',' || n == '-' || n == '\\' || std::isspace(static_cast<unsigned char>(n))) {
                term.push_back({n, true});
                ++i;
            } else {
                term.push_back({c, false}); // unknown escape: the backslash stays
            }
        } else {
            term.push_back({c, false});
        }
    }
    flush();
}

std::string FilterExpr::escape(std::string_view text)
{
    const auto isSpace = [](char c) { return std::isspace(static_cast<unsigned char>(c)) != 0; };
    size_t lead = 0;
    while (lead < text.size() && isSpace(text[lead]))
        ++lead;
    size_t trail = text.size();
    while (trail > lead && isSpace(text[trail - 1]))
        --trail;
    std::string out;
    out.reserve(text.size() + 4);
    for (size_t i = 0; i < text.size(); ++i) {
        const char c = text[i];
        const bool edgeSpace = isSpace(c) && (i < lead || i >= trail);
        if (c == '\\' || c == ',' || edgeSpace || (c == '-' && i == 0))
            out.push_back('\\');
        out.push_back(c);
    }
    return out;
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

bool FilterExpr::matchesLowered(std::string_view field) const
{
    for (const Term& t : m_terms)
        if ((field.find(t.text) != std::string_view::npos) == t.negated)
            return false;
    return true;
}

} // namespace gg
