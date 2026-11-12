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

} // namespace ggtest
