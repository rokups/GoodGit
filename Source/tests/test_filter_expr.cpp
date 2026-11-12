// Filter expressions (GG-25): the parser and the match rules.
#include "tests/Harness.hpp"
#include "util/FilterExpr.hpp"

namespace ggtest {

GG_TEST("filter_expr", "terms, negation, whitespace and fields")
{
    using ggui::FilterExpr;
    const auto pass = [](const char* expr, std::initializer_list<std::string_view> fields) {
        return FilterExpr(expr).matches(fields);
    };
    GG_CHECK(FilterExpr("").empty());
    GG_CHECK(FilterExpr(" , ,, - ,-").empty());
    GG_CHECK(!FilterExpr("a").empty());
    GG_CHECK(pass("", {"anything"}));
    GG_CHECK(pass(" , ,", {"anything"}));
    GG_CHECK(pass("-", {"anything"}));
    GG_CHECK(pass("fix", {"a fix here"}));
    GG_CHECK(!pass("fix", {"a feature"}));
    // All terms must hold.
    GG_CHECK(pass("a,b", {"xaxbx"}));
    GG_CHECK(!pass("a,b", {"xax"}));
    GG_CHECK(!pass("a,b", {"xbx"}));
    // Negation.
    GG_CHECK(pass("-x", {"abc"}));
    GG_CHECK(!pass("-x", {"abxc"}));
    GG_CHECK(pass("-master", {"main", "dev"}));
    GG_CHECK(!pass("-master", {"main", "master"}));
    GG_CHECK(pass("a,-x", {"abc"}));
    GG_CHECK(!pass("a,-x", {"axc"}));
    GG_CHECK(!pass("a,-x", {"xyz"}));
    // Whitespace and empty terms.
    GG_CHECK(pass("  a  ,  ,  b  ", {"ab"}));
    GG_CHECK(!pass("  a  ,  -  ,  -b  ", {"ab"}));
    GG_CHECK(pass(" - x ", {"abc"}));
    GG_CHECK(!pass(" - x ", {"abxc"}));
    GG_CHECK(pass("a b", {"a b"}));
    GG_CHECK(!pass("a b", {"ab"}));
    // Case.
    GG_CHECK(pass("FIX", {"a fix"}));
    GG_CHECK(pass("fix", {"A FIX"}));
    GG_CHECK(!pass("-FIX", {"a fix"}));
    // Several fields: a term matches in any field; a negated term must match in none.
    GG_CHECK(pass("abc,hello", {"abc123", "say hello"}));
    GG_CHECK(!pass("-hello", {"abc123", "say hello"}));
    GG_CHECK(pass("-zzz", {"abc123", "say hello"}));
    GG_CHECK(!pass("abc", {"", "say hello"}));
    GG_CHECK(!pass("a", {}));

    ggui::FilterInput in;
    GG_CHECK(in.expr().empty());
    in.text = "a,-b";
    GG_CHECK(in.expr().matches("a"));
    GG_CHECK(!in.expr().matches("ab"));
    in.text = "";
    GG_CHECK(in.expr().matches("ab"));
}

GG_TEST("filter_expr", "backslash escapes and the escape helper")
{
    using ggui::FilterExpr;
    const auto pass = [](const char* expr, std::string_view field) { return FilterExpr(expr).matches(field); };
    // Escaped comma: one literal term.
    GG_CHECK(pass("a\\,b", "xa,bx"));
    GG_CHECK(!pass("a\\,b", "a b"));
    // Escaped leading minus: a literal term, not a negation.
    GG_CHECK(pass("\\-x", "a -x"));
    GG_CHECK(!pass("\\-x", "x"));
    // Escaped backslash.
    GG_CHECK(pass("a\\\\b", "a\\b"));
    GG_CHECK(!pass("a\\\\b", "ab"));
    // A lone trailing backslash is a literal backslash.
    GG_CHECK(pass("a\\", "xa\\"));
    GG_CHECK(!pass("a\\", "xa"));
    // An unknown escape keeps the backslash.
    GG_CHECK(pass("a\\b", "a\\b"));
    GG_CHECK(!pass("a\\b", "ab"));
    // A minus inside a term is plain text; an escaped term still combines with others.
    GG_CHECK(pass("\\-x,y", "-x y"));
    GG_CHECK(!pass("\\-x,y", "-x"));

    // Round trip: the escaped text matches exactly the strings that contain the text.
    const char* texts[] = {"a", "a,b", "-x", "\\", " lead", "x\\", "trail ", " ", "a\\,b", "-", ",", "A-b"};
    const char* hay[] = {"a", "xa,bx", "ab", "-x", "x", "\\", "x\\y", " lead", "lead", "a lead", "trail ", "trail",
        " ", "", "a\\,b", "-", ",", "a-b", "A-B"};
    for (const char* t : texts) {
        const std::string esc = FilterExpr::escape(t);
        const FilterExpr expr(esc);
        for (const char* h : hay)
            GG_CHECK(expr.matches(h) == gg::containsNoCaseView(h, t));
    }
    GG_CHECK_STR_EQ(FilterExpr::escape("a,b"), "a\\,b");
    GG_CHECK_STR_EQ(FilterExpr::escape("-x"), "\\-x");
    GG_CHECK_STR_EQ(FilterExpr::escape("x-"), "x-");
}

} // namespace ggtest
