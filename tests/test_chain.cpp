// Conformance tests for the chain layer in phext.h, ported 1:1 from
// libphext-rs v0.6.0 src/test_chain.rs (11 tests) plus the two top-level
// tests in that file (multiverse_addressing, adjacent_closes).
#include "../phext.h"
#include "harness.h"

#include <fstream>
#include <sstream>

static std::string read_file(const char* path) {
   std::ifstream in(path, std::ios::binary);
   std::stringstream ss;
   ss << in.rdbuf();
   return ss.str();
}

// parse + serialize must reproduce every stream byte-for-byte
static void roundtrip(const std::string& stream) {
   const auto chains = phext::parse_chains(stream);
   CHECK_EQ_STR(phext::serialize_chains(chains), stream);
}

static void test_chain_free_input_is_one_unclosed_chain() {
   const std::string doc = "alpha\x17" "beta\x01" "gamma";
   const auto chains = phext::parse_chains(doc);
   CHECK(chains.size() == 1);
   CHECK(chains[0].members == std::vector<std::string>{doc});
   CHECK(!chains[0].closed);
   CHECK_EQ_STR(phext::serialize_chains(chains), doc);
   CHECK_EQ_STR(phext::fetch(doc, phext::to_coordinate("1.1.1/1.1.1/1.1.2")), "beta");
   CHECK_EQ_STR(phext::fetch(doc, phext::to_coordinate("2.1.1/1.1.1/1.1.1")), "gamma");
   roundtrip(doc);
}

static void test_two_phexts_chained_and_closed_round_trip() {
   std::string stream = "edition one";
   stream.push_back(phext::CHAIN_BREAK);
   stream += "edition two";
   stream.push_back(phext::CHAIN_CLOSE);
   const auto chains = phext::parse_chains(stream);
   CHECK(chains.size() == 1);
   CHECK(chains[0].members == (std::vector<std::string>{"edition one", "edition two"}));
   CHECK(chains[0].closed);
   roundtrip(stream);
}

static void test_unclosed_chain_at_eof_reports_truncated() {
   const auto chains = phext::parse_chains("one\x02" "two");
   CHECK(chains.size() == 1);
   CHECK(chains[0].members == (std::vector<std::string>{"one", "two"}));
   CHECK(!chains[0].closed);
   roundtrip("one\x02" "two");
}

static void test_two_chains_in_one_stream_parse_as_two_units() {
   const std::string stream = "a\x02" "b\x03" "c\x02" "d\x03";
   const auto chains = phext::parse_chains(stream);
   CHECK(chains.size() == 2);
   CHECK(chains[0].members == (std::vector<std::string>{"a", "b"}));
   CHECK(chains[0].closed);
   CHECK(chains[1].members == (std::vector<std::string>{"c", "d"}));
   CHECK(chains[1].closed);
   roundtrip(stream);

   const auto mixed = phext::parse_chains("a\x03" "b");
   CHECK(mixed.size() == 2);
   CHECK(mixed[0].members == std::vector<std::string>{"a"});
   CHECK(mixed[0].closed);
   CHECK(mixed[1].members == std::vector<std::string>{"b"});
   CHECK(!mixed[1].closed);
   roundtrip("a\x03" "b");
}

static void test_chained_members_carry_full_phexts() {
   const std::string first = "root\x17" "second scroll";
   const std::string second = phext::insert(std::string(), phext::to_coordinate("1.1.1/1.1.1/1.1.3"), "third edition");
   const std::string stream = first + "\x02" + second + "\x03";
   const auto chains = phext::parse_chains(stream);
   CHECK(chains.size() == 1);
   CHECK(chains[0].members.size() == 2);
   CHECK_EQ_STR(phext::fetch(chains[0].members[0], phext::to_coordinate("1.1.1/1.1.1/1.1.2")), "second scroll");
   CHECK_EQ_STR(phext::fetch(chains[0].members[1], phext::to_coordinate("1.1.1/1.1.1/1.1.3")), "third edition");
   CHECK_EQ_STR(phext::fetch(chains[0].members[1], phext::to_coordinate("1.1.1/1.1.1/1.1.1")), "");
   roundtrip(stream);
}

static void test_editions_in_succession_read_in_order() {
   const std::string stream = "v1\x02" "v2\x02" "v3\x03";
   const auto chains = phext::parse_chains(stream);
   CHECK(chains.size() == 1);
   CHECK(chains[0].members == (std::vector<std::string>{"v1", "v2", "v3"}));
   CHECK(chains[0].closed);
   roundtrip(stream);
}

static void test_empty_and_break_only_streams_round_trip() {
   const auto empty = phext::parse_chains("");
   CHECK(empty.size() == 1);
   CHECK(empty[0].members == std::vector<std::string>{""});
   CHECK(!empty[0].closed);
   roundtrip("");

   const auto lone = phext::parse_chains("\x03");
   CHECK(lone.size() == 1);
   CHECK(lone[0].members == std::vector<std::string>{""});
   CHECK(lone[0].closed);
   roundtrip("\x03");

   CHECK(phext::parse_chains("a\x02")[0].members == (std::vector<std::string>{"a", ""}));
   CHECK(phext::parse_chains("\x02" "b")[0].members == (std::vector<std::string>{"", "b"}));
   CHECK(phext::parse_chains("\x02\x02")[0].members.size() == 3);
   roundtrip("a\x02");
   roundtrip("\x02" "b");
   roundtrip("\x02\x02");
   roundtrip("\x02");

   const auto closes = phext::parse_chains("a\x03\x03");
   CHECK(closes.size() == 2);
   CHECK(closes[0].closed && closes[1].closed);
   CHECK(closes[1].members == std::vector<std::string>{""});
   roundtrip("a\x03\x03");
}

static void test_serialize_from_constructed_chains() {
   phext::Chain chain;
   CHECK(!chain.closed);
   CHECK_EQ_STR(chain.serialize(), "");
   chain.members.push_back("v1");
   chain.members.push_back("v2");
   CHECK_EQ_STR(chain.serialize(), "v1\x02" "v2");
   chain.closed = true;
   CHECK_EQ_STR(chain.serialize(), "v1\x02" "v2\x03");
   CHECK_EQ_STR(phext::serialize_chains(std::vector<phext::Chain>{chain}), "v1\x02" "v2\x03");
}

static void test_canon_shaped_documents_parse_unchanged() {
   // RS reads "regression-1-helios.phext" relative to its crate working dir;
   // this suite runs from the repo root, so the path carries tests/.
   const std::string doc = read_file("tests/regression-1-helios.phext");
   CHECK(!doc.empty());
   CHECK(doc.find(phext::CHAIN_BREAK) == std::string::npos);
   CHECK(doc.find(phext::CHAIN_CLOSE) == std::string::npos);
   const auto chains = phext::parse_chains(doc);
   CHECK(chains.size() == 1);
   CHECK(chains[0].members == std::vector<std::string>{doc});
   CHECK(!chains[0].closed);
   CHECK_EQ_STR(phext::serialize_chains(chains), doc);
}

static void test_chain_layer_does_not_touch_member_structure() {
   const auto chains = phext::parse_chains("x\x17" "y\x02" "z\x17" "w\x03");
   CHECK(chains[0].members == (std::vector<std::string>{"x\x17" "y", "z\x17" "w"}));
   CHECK(phext::phokenize(chains[0].members[1]).size() == 2);
   roundtrip("x\x17" "y\x02" "z\x17" "w\x03");
}

static void test_multiverse_addressing() {
   phext::Chain chain;
   chain.members = {"U1 home", "U2 home"};
   chain.closed = true;
   CHECK_EQ_STR(chain.fetch("1.1.1/1.1.1/1.1.1"), "U1 home");
   CHECK_EQ_STR(chain.fetch("$1:1.1.1/1.1.1/1.1.1"), "U1 home");
   CHECK_EQ_STR(chain.fetch("$2:1.1.1/1.1.1/1.1.1"), "U2 home");
   CHECK_EQ_STR(chain.fetch("$3:1.1.1/1.1.1/1.1.1"), "");
   CHECK(!phext::parse_universe_address("$0:1.1.1/1.1.1/1.1.1").has_value());
   const auto parsed = phext::parse_universe_address("$12:2.1.1/1.1.1/1.1.1");
   CHECK(parsed.has_value());
   if (parsed.has_value()) CHECK(parsed->first == 12);
   // malformed prefixes also yield "" from fetch (no error channel)
   CHECK_EQ_STR(chain.fetch("$x:1.1.1/1.1.1/1.1.1"), "");
   CHECK_EQ_STR(chain.fetch("$:1.1.1/1.1.1/1.1.1"), "");
   CHECK_EQ_STR(chain.fetch("$1"), "");
}

static void test_adjacent_closes_are_two_empty_chains() {
   const auto chains = phext::parse_chains("\x03\x03");
   CHECK(chains.size() == 2);
   CHECK(chains[0].closed && chains[1].closed);
   CHECK_EQ_STR(phext::serialize_chains(chains), "\x03\x03");
}

int main() {
   RUN(test_chain_free_input_is_one_unclosed_chain);
   RUN(test_two_phexts_chained_and_closed_round_trip);
   RUN(test_unclosed_chain_at_eof_reports_truncated);
   RUN(test_two_chains_in_one_stream_parse_as_two_units);
   RUN(test_chained_members_carry_full_phexts);
   RUN(test_editions_in_succession_read_in_order);
   RUN(test_empty_and_break_only_streams_round_trip);
   RUN(test_serialize_from_constructed_chains);
   RUN(test_canon_shaped_documents_parse_unchanged);
   RUN(test_chain_layer_does_not_touch_member_structure);
   RUN(test_multiverse_addressing);
   RUN(test_adjacent_closes_are_two_empty_chains);
   return harness::summary("test_chain");
}
