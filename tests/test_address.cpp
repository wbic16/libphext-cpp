// Conformance tests for the v0.6.1 line/column addressing layer in phext.h,
// ported 1:1 from libphext-rs v0.6.1 src/test_address.rs (10 tests).
#include "../phext.h"
#include "harness.h"

#include <tuple>

static phext::Coordinate home() { return phext::to_coordinate("1.1.1/1.1.1/1.1.1"); }
static phext::Coordinate scroll2() { return phext::to_coordinate("1.1.1/1.1.1/1.1.2"); }

static std::string member_a() {
   std::string member = phext::insert(std::string(), home(), "alpha bravo\ncharlie delta\necho");
   member = phext::insert(member, scroll2(), "second scroll\nline two");
   return member;
}

static std::string member_b() {
   return phext::insert(std::string(), home(), "uni2 home\nsecond line");
}

static phext::Chain chain() {
   phext::Chain c;
   c.members = {member_a(), member_b()};
   c.closed = false;
   return c;
}

// resolve_point against a literal address, checking the full (universe, coordinate,
// offset) triple (or nullopt when expected_offset < 0)
static void check_resolve(const phext::Chain& c, const char* address, bool expect_ok,
                          std::uint64_t expect_universe, const phext::Coordinate& expect_coord,
                          std::uint64_t expect_offset) {
   const auto point = phext::parse_point_address(address);
   CHECK(point.has_value());
   if (!point.has_value()) return;
   const auto resolved = c.resolve_point(*point);
   CHECK(resolved.has_value() == expect_ok);
   if (expect_ok && resolved.has_value()) {
      CHECK(std::get<0>(*resolved) == expect_universe);
      CHECK(std::get<1>(*resolved) == expect_coord);
      CHECK(std::get<2>(*resolved) == expect_offset);
   }
}

static void test_point_parse_canonical() {
   const auto p = phext::parse_point_address("$3:1.1.1/1.1.1/1.1.1@24,5");
   CHECK(p.has_value());
   CHECK(p->universe == 3);
   CHECK(p->coordinate == home());
   CHECK(p->position.has_value());
   CHECK(p->position->line == 24 && p->position->column == 5);
   CHECK(!p->offset.has_value());
   CHECK_EQ_STR(p->to_string(), "$3:1.1.1/1.1.1/1.1.1@24,5");
   // $1: is accepted on input, never emitted
   const auto p1 = phext::parse_point_address("$1:1.1.1/1.1.1/1.1.1@24,5");
   CHECK(p1.has_value());
   CHECK_EQ_STR(p1->to_string(), "1.1.1/1.1.1/1.1.1@24,5");
   // leading zeros parse by value; canonical form is minimal decimal
   const auto pz = phext::parse_point_address("1.1.1/1.1.1/1.1.1@024,005");
   CHECK(pz.has_value());
   CHECK_EQ_STR(pz->to_string(), "1.1.1/1.1.1/1.1.1@24,5");
   // bare coordinate: no position
   const auto bare = phext::parse_point_address("1.1.1/1.1.1/1.1.2");
   CHECK(bare.has_value());
   CHECK(!bare->position.has_value());
   CHECK_EQ_STR(bare->to_string(), "1.1.1/1.1.1/1.1.2");
   // offset form
   const auto po = phext::parse_point_address("1.1.1/1.1.1/1.1.1@24,5+10");
   CHECK(po.has_value());
   CHECK(po->offset.has_value() && *po->offset == 10);
   CHECK_EQ_STR(po->to_string(), "1.1.1/1.1.1/1.1.1@24,5+10");
}

static void test_point_parse_rejects() {
   const char* bad[] = {
      "$0:1.1.1/1.1.1/1.1.1@1,1",   // universe 0
      "1.1.1/1.1.1/1.1.1@0,5",      // line 0
      "1.1.1/1.1.1/1.1.1@24,0",     // column 0
      "1.1.1/1.1.1/0.1.1@1,1",      // coordinate component 0
      "1.1.1/1.1.1@1,1",            // only two arms
      "1.1.1/1.1.1/1.1.1@24",       // no column
      "1.1.1/1.1.1/1.1.1@24,5+",    // empty offset
      "1.1.1/1.1.1/1.1.1@24,+5",    // empty column
      "1.1.1/1.1.1/1.1.1@24,5@1,1", // second @
      "1.1.1/1.1.1/1.1.1@ 24,5",    // whitespace
      "",                           // empty
   };
   for (const char* address : bad) {
      CHECK(!phext::parse_point_address(address).has_value());
   }
}

static void test_point_resolve() {
   const phext::Chain c = chain();
   // line 1 "alpha bravo" = 11 chars + newline = 12; column 3 -> +2
   check_resolve(c, "1.1.1/1.1.1/1.1.1@2,3", true, 1, home(), 14);
   // last valid boundary of line 1
   check_resolve(c, "1.1.1/1.1.1/1.1.1@1,12", true, 1, home(), 11);
   // past the line, past the scroll, missing scroll, missing universe
   check_resolve(c, "1.1.1/1.1.1/1.1.1@1,13", false, 0, home(), 0);
   check_resolve(c, "1.1.1/1.1.1/1.1.1@4,1", false, 0, home(), 0);
   check_resolve(c, "1.1.1/1.1.1/1.1.9@1,1", false, 0, home(), 0);
   check_resolve(c, "$3:1.1.1/1.1.1/1.1.1@1,1", false, 0, home(), 0);
   // bare point resolves to offset 0; universe 2 resolves in its own member
   check_resolve(c, "1.1.1/1.1.1/1.1.2", true, 1, scroll2(), 0);
   check_resolve(c, "$2:1.1.1/1.1.1/1.1.1@2,1", true, 2, home(), 10);
}

static void test_point_fetch_offset() {
   const phext::Chain c = chain();
   CHECK_EQ_STR(c.fetch_point("1.1.1/1.1.1/1.1.1@1,1+5"), "alpha");
   CHECK_EQ_STR(c.fetch_point("1.1.1/1.1.1/1.1.1@2,1+7"), "charlie");
   // across a line boundary: offset 9 is 'v' of "bravo"
   CHECK_EQ_STR(c.fetch_point("1.1.1/1.1.1/1.1.1@1,10+5"), "vo\nch");
   // clipped at the scroll boundary, never into the next scroll
   CHECK_EQ_STR(c.fetch_point("1.1.1/1.1.1/1.1.1@3,1+100"), "echo");
   CHECK_EQ_STR(c.fetch_point("1.1.1/1.1.1/1.1.1@1,1+0"), "");
   // a bare point is a caret: fetches nothing; failures fetch nothing
   CHECK_EQ_STR(c.fetch_point("1.1.1/1.1.1/1.1.1@1,1"), "");
   CHECK_EQ_STR(c.fetch_point("1.1.1/1.1.1/1.1.1@9,1+3"), "");
   CHECK_EQ_STR(c.fetch_point("$2:1.1.1/1.1.1/1.1.1@1,1+4"), "uni2");
}

static void test_point_fetch_offset_unicode() {
   phext::Chain c;
   c.members = {phext::insert(std::string(), home(), "h\xC3\xA9llo w\xC3\xB6rld")}; // "héllo wörld"
   c.closed = false;
   // columns and offsets count scalars: é is one character (two bytes)
   check_resolve(c, "1.1.1/1.1.1/1.1.1@1,3", true, 1, home(), 2);
   CHECK_EQ_STR(c.fetch_point("1.1.1/1.1.1/1.1.1@1,2+3"), "\xC3\xA9ll"); // "éll"
}

static void test_range_parse_canonical() {
   const auto r = phext::parse_range_address("1.1.1/1.1.1/1.1.1@24,5-1.1.1/1.1.1/1.1.1@24,9");
   CHECK(r.has_value());
   CHECK_EQ_STR(r->to_string(), "1.1.1/1.1.1/1.1.1@24,5-1.1.1/1.1.1/1.1.1@24,9");
   // reversed input normalizes: direction is discarded
   const auto rev = phext::parse_range_address("1.1.1/1.1.1/1.1.1@24,9-1.1.1/1.1.1/1.1.1@24,5");
   CHECK(rev.has_value());
   CHECK(*rev == *r);
   // $1: on input is dropped in canonical form, even beside a $3: endpoint
   const auto cross = phext::parse_range_address("$1:1.1.1/1.1.1/1.1.1@24,5-$3:1.1.1/1.1.1/1.1.1@31,12");
   CHECK(cross.has_value());
   CHECK_EQ_STR(cross->to_string(), "1.1.1/1.1.1/1.1.1@24,5-$3:1.1.1/1.1.1/1.1.1@31,12");
   // offset forms are not range endpoints
   CHECK(!phext::parse_range_address("1.1.1/1.1.1/1.1.1@1,1+3-1.1.1/1.1.1/1.1.1@1,5").has_value());
   // one hyphen only
   CHECK(!phext::parse_range_address("1.1.1/1.1.1/1.1.1-1.1.1/1.1.1/1.1.2-1.1.1/1.1.1/1.1.3").has_value());
}

static void test_range_fetch_within_scroll() {
   const phext::Chain c = chain();
   // characters 1-5 of line 1 (boundaries 1..6, half-open)
   CHECK_EQ_STR(c.fetch_range("1.1.1/1.1.1/1.1.1@1,1-1.1.1/1.1.1/1.1.1@1,6"), "alpha");
   // the whole of line 2, newline included
   CHECK_EQ_STR(c.fetch_range("1.1.1/1.1.1/1.1.1@2,1-1.1.1/1.1.1/1.1.1@3,1"), "charlie delta\n");
   // empty range is a caret
   CHECK_EQ_STR(c.fetch_range("1.1.1/1.1.1/1.1.1@1,3-1.1.1/1.1.1/1.1.1@1,3"), "");
   // reversed fetches the same extent
   CHECK_EQ_STR(c.fetch_range("1.1.1/1.1.1/1.1.1@1,6-1.1.1/1.1.1/1.1.1@1,1"), "alpha");
}

static void test_range_fetch_cross_scroll() {
   const phext::Chain c = chain();
   const std::string frag = c.fetch_range("1.1.1/1.1.1/1.1.1@3,2-1.1.1/1.1.1/1.1.2@1,7");
   CHECK_EQ_STR(frag, "cho\x17" "second");
   // the fragment round-trips as a phext: its scrolls fetch back the segments
   const auto parsed = phext::parse_chains(frag);
   CHECK(parsed.size() == 1);
   CHECK_EQ_STR(parsed[0].fetch("1.1.1/1.1.1/1.1.1"), "cho");
   CHECK_EQ_STR(parsed[0].fetch("1.1.1/1.1.1/1.1.2"), "second");
   // whole scrolls, no @ on either endpoint: the member itself
   CHECK_EQ_STR(c.fetch_range("1.1.1/1.1.1/1.1.1-1.1.1/1.1.1/1.1.2"), member_a());
}

static void test_range_fetch_multiverse() {
   const phext::Chain c = chain();
   const std::string frag = c.fetch_range("1.1.1/1.1.1/1.1.1@3,2-$2:1.1.1/1.1.1/1.1.1@1,5");
   // universe 1's scroll 2 lies strictly between the endpoints: a whole segment
   CHECK_EQ_STR(frag, "cho\x17" "second scroll\nline two\x02" "uni2");
   const auto parsed = phext::parse_chains(frag);
   CHECK(parsed.size() == 1);
   CHECK(parsed[0].members.size() == 2);
   CHECK_EQ_STR(parsed[0].fetch("$2:1.1.1/1.1.1/1.1.1"), "uni2");
   // resolve_range exposes the segments
   const auto range = phext::parse_range_address("1.1.1/1.1.1/1.1.1@3,2-$2:1.1.1/1.1.1/1.1.1@1,5");
   CHECK(range.has_value());
   const auto segments = c.resolve_range(*range);
   CHECK(segments.has_value());
   CHECK(segments->size() == 3);
   if (segments.has_value() && segments->size() == 3) {
      CHECK(std::get<0>((*segments)[0]) == 1 && std::get<1>((*segments)[0]) == home());
      CHECK_EQ_STR(std::get<2>((*segments)[0]), "cho");
      CHECK(std::get<0>((*segments)[1]) == 1 && std::get<1>((*segments)[1]) == scroll2());
      CHECK_EQ_STR(std::get<2>((*segments)[1]), "second scroll\nline two");
      CHECK(std::get<0>((*segments)[2]) == 2 && std::get<1>((*segments)[2]) == home());
      CHECK_EQ_STR(std::get<2>((*segments)[2]), "uni2");
   }
}

static void test_range_resolution_errors() {
   const phext::Chain c = chain();
   // universe past the end of the chain
   CHECK_EQ_STR(c.fetch_range("1.1.1/1.1.1/1.1.1@1,1-$3:1.1.1/1.1.1/1.1.1@1,1"), "");
   const auto r1 = phext::parse_range_address("1.1.1/1.1.1/1.1.1@1,1-$3:1.1.1/1.1.1/1.1.1@1,1");
   CHECK(r1.has_value() && !c.resolve_range(*r1).has_value());
   // missing endpoint scroll
   const auto r2 = phext::parse_range_address("1.1.1/1.1.1/1.1.1@1,1-1.1.1/1.1.1/1.1.9@1,1");
   CHECK(r2.has_value() && !c.resolve_range(*r2).has_value());
   // line past the scroll's end: an error, never a clamp
   const auto r3 = phext::parse_range_address("1.1.1/1.1.1/1.1.1@1,1-1.1.1/1.1.1/1.1.1@9,1");
   CHECK(r3.has_value() && !c.resolve_range(*r3).has_value());
   // column past the line's last boundary
   const auto r4 = phext::parse_range_address("1.1.1/1.1.1/1.1.1@1,1-1.1.1/1.1.1/1.1.1@1,13");
   CHECK(r4.has_value() && !c.resolve_range(*r4).has_value());
}

int main() {
   RUN(test_point_parse_canonical);
   RUN(test_point_parse_rejects);
   RUN(test_point_resolve);
   RUN(test_point_fetch_offset);
   RUN(test_point_fetch_offset_unicode);
   RUN(test_range_parse_canonical);
   RUN(test_range_fetch_within_scroll);
   RUN(test_range_fetch_cross_scroll);
   RUN(test_range_fetch_multiverse);
   RUN(test_range_resolution_errors);
   return harness::summary("test_address");
}
