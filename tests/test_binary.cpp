// Conformance tests ported from libphext-rs v0.5.0 src/test_binary.rs
// (10 tests): canonical binary scrolls and the base64-verbatim ingestion rule.
#include "../phext.h"
#include "harness.h"

using phext::Coordinate;
static Coordinate C(const char* address) { return phext::to_coordinate(address); }

static void test_encode_decode_round_trip() {
   const std::vector<std::uint8_t> empty;
   const std::string scroll = phext::encode_scroll(empty);
   CHECK_EQ_STR(scroll, "phext-binary:1:base64\n");
   const auto decoded_empty = phext::decode_scroll(scroll);
   CHECK(decoded_empty.has_value());
   CHECK(decoded_empty.value() == empty);

   std::vector<std::uint8_t> all_bytes;
   for (int i = 0; i <= 255; ++i) all_bytes.push_back(static_cast<std::uint8_t>(i));
   const std::string scroll2 = phext::encode_scroll(all_bytes);
   CHECK(phext::is_binary_scroll(scroll2));
   const auto decoded2 = phext::decode_scroll(scroll2);
   CHECK(decoded2.has_value());
   CHECK(decoded2.value() == all_bytes);

   const std::vector<std::uint8_t> foreign = {0xFF, 0xFE, 0x80, 0x00, 0x61, 0xC3, 0x28};
   const auto decoded3 = phext::decode_scroll(phext::encode_scroll(foreign));
   CHECK(decoded3.has_value());
   CHECK(decoded3.value() == foreign);

   std::vector<std::uint8_t> big;
   big.reserve(1024 * 1024 + 13);
   for (std::size_t i = 0; i < 1024 * 1024 + 13; ++i) big.push_back(static_cast<std::uint8_t>(i % 256));
   const auto decoded4 = phext::decode_scroll(phext::encode_scroll(big));
   CHECK(decoded4.has_value());
   CHECK(decoded4.value() == big);
}

static void test_decode_scroll_rejects_non_binary() {
   CHECK(!phext::decode_scroll("just some text").has_value());
   CHECK(!phext::decode_scroll("").has_value());
   CHECK(!phext::decode_scroll("phext-binary:1:base64").has_value());
   CHECK(!phext::is_binary_scroll("phext-binary:1:base64"));
   const char* malformed = "phext-binary:1:base64\n!!! not base64 !!!";
   CHECK(phext::is_binary_scroll(malformed));
   CHECK(!phext::decode_scroll(malformed).has_value());
   // strictness beyond the RS tests, pinning the base64 STANDARD semantics:
   CHECK(!phext::decode_scroll("phext-binary:1:base64\nQUJDRA").has_value());     // missing padding
   CHECK(!phext::decode_scroll("phext-binary:1:base64\nQUJD RA==").has_value());  // whitespace
   CHECK(!phext::decode_scroll("phext-binary:1:base64\nQR==").has_value());       // non-zero trailing bits
   const auto ok = phext::decode_scroll("phext-binary:1:base64\nQQ==");
   CHECK(ok.has_value());
   CHECK(ok.value() == std::vector<std::uint8_t>{'A'});
}

static void test_insert_and_fetch_binary() {
   const Coordinate target = C("1.1.1/1.1.1/1.1.2");
   const std::vector<std::uint8_t> payload = {0x00, 0x01, 0x02, 0xFE, 0xFF, 0x7F, 0x80};
   std::string doc = phext::insert("", C("1.1.1/1.1.1/1.1.1"), "plain text scroll");
   doc = phext::insert_binary(doc, target, payload);
   CHECK_EQ_STR(phext::fetch(doc, C("1.1.1/1.1.1/1.1.1")), "plain text scroll");
   CHECK(phext::fetch_binary(doc, target) == payload);
   const std::string stored = phext::fetch(doc, target);
   CHECK(phext::is_binary_scroll(stored));
   const auto decoded = phext::decode_scroll(stored);
   CHECK(decoded.has_value());
   CHECK(decoded.value() == payload);
}

static void test_fetch_binary_fallbacks() {
   const std::string doc = phext::insert("", C("1.1.1/1.1.1/1.1.1"), "hello");
   CHECK(phext::fetch_binary(doc, C("1.1.1/1.1.1/1.1.1")) == std::vector<std::uint8_t>({'h', 'e', 'l', 'l', 'o'}));
   CHECK(phext::fetch_binary(doc, C("9.9.9/9.9.9/9.9.9")).empty());
   const std::string doc2 = phext::insert("", C("1.1.1/1.1.1/1.1.1"), "phext-binary:1:base64\n!!!");
   const std::string raw = "phext-binary:1:base64\n!!!";
   CHECK(phext::fetch_binary(doc2, C("1.1.1/1.1.1/1.1.1")) == std::vector<std::uint8_t>(raw.begin(), raw.end()));
}

static void test_update_rule_non_utf8_is_base64_verbatim() {
   const Coordinate target = C("1.1.1/1.1.1/1.1.1");
   // payload deliberately includes dimension-break bytes (0x17, 0x1C)
   const std::vector<std::uint8_t> payload = {0xFF, 0x17, 0x1C, 0x80, 0x00};
   const std::string doc = phext::replace_bytes("", target, payload);
   const std::string stored = phext::fetch(doc, target);
   CHECK(phext::is_binary_scroll(stored));
   CHECK(phext::fetch_binary(doc, target) == payload);
   const std::string doc2 = phext::insert_bytes("", target, payload);
   CHECK(phext::fetch_binary(doc2, target) == payload);
}

static void test_update_rule_utf8_stays_text() {
   const Coordinate target = C("1.1.1/1.1.1/1.1.1");
   const std::string text = "ordinary text \xE2\x9C\x93"; // "ordinary text ✓"
   const std::vector<std::uint8_t> bytes(text.begin(), text.end());
   const std::string doc = phext::replace_bytes("", target, bytes);
   CHECK_EQ_STR(phext::fetch(doc, target), text);
   CHECK(!phext::is_binary_scroll(phext::fetch(doc, target)));
   CHECK(phext::fetch_binary(doc, target) == bytes);
}

static void test_ingest_foreign_byte_stream() {
   std::vector<std::uint8_t> stream = {'h', 'e', 'l', 'l', 'o'};
   stream.push_back(phext::SCROLL_BREAK_U8);
   stream.insert(stream.end(), {0xFF, 0xFE, 0x41, 0x80});
   const std::string doc = phext::ingest(stream);
   CHECK_EQ_STR(phext::fetch(doc, C("1.1.1/1.1.1/1.1.1")), "hello");
   const Coordinate foreign_at = C("1.1.1/1.1.1/1.1.2");
   CHECK(phext::is_binary_scroll(phext::fetch(doc, foreign_at)));
   CHECK(phext::fetch_binary(doc, foreign_at) == std::vector<std::uint8_t>({0xFF, 0xFE, 0x41, 0x80}));
}

static void test_ingest_canonical_phext_is_identity() {
   std::string doc = phext::insert("", C("1.1.1/1.1.1/1.1.1"), "first");
   doc = phext::insert(doc, C("1.1.1/1.1.1/1.1.2"), "second \xE2\x9C\x93 scroll");
   doc = phext::insert(doc, C("2.1.1/1.3.1/1.1.1"), "far away");
   const std::vector<std::uint8_t> bytes(doc.begin(), doc.end());
   CHECK_EQ_STR(phext::ingest(bytes), doc);
}

static void test_ingest_empty_and_break_only_streams() {
   const std::string doc = phext::ingest(std::vector<std::uint8_t>{});
   CHECK_EQ_STR(phext::fetch(doc, C("1.1.1/1.1.1/1.1.1")), "");
   const std::string doc2 = phext::ingest(std::vector<std::uint8_t>{phext::SCROLL_BREAK_U8, phext::SECTION_BREAK_U8});
   CHECK_EQ_STR(phext::fetch(doc2, C("1.1.1/1.1.1/1.1.1")), "");
}

static void test_phokenize_bytes_matches_phokenize() {
   const std::string doc = "alpha\x17" "beta\x18" "gamma";
   const std::vector<phext::PositionedScroll> text_parts = phext::phokenize(doc);
   const std::vector<std::uint8_t> bytes(doc.begin(), doc.end());
   const auto byte_parts = phext::phokenize_bytes(bytes);
   CHECK(text_parts.size() == byte_parts.size());
   const std::size_t n = text_parts.size() < byte_parts.size() ? text_parts.size() : byte_parts.size();
   for (std::size_t i = 0; i < n; ++i) {
      CHECK(text_parts[i].coord == byte_parts[i].first);
      CHECK(std::string(byte_parts[i].second.begin(), byte_parts[i].second.end()) == text_parts[i].scroll);
   }
}

int main() {
   RUN(test_encode_decode_round_trip);
   RUN(test_decode_scroll_rejects_non_binary);
   RUN(test_insert_and_fetch_binary);
   RUN(test_fetch_binary_fallbacks);
   RUN(test_update_rule_non_utf8_is_base64_verbatim);
   RUN(test_update_rule_utf8_stays_text);
   RUN(test_ingest_foreign_byte_stream);
   RUN(test_ingest_canonical_phext_is_identity);
   RUN(test_ingest_empty_and_break_only_streams);
   RUN(test_phokenize_bytes_matches_phokenize);
   return harness::summary("test_binary (test_binary.rs ports)");
}
