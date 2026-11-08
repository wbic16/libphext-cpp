// Conformance tests ported from libphext-rs v0.5.0 src/test_seal.rs
// (5 tests). Built only in the xz-enabled configuration:
//   g++ -std=c++17 -DPHEXT_CPP_WITH_XZ ... -llzma
#include "../phext.h"
#include "harness.h"

#include <fstream>
#include <sstream>

#ifndef PHEXT_CPP_WITH_XZ
#error "test_seal.cpp must be compiled with -DPHEXT_CPP_WITH_XZ"
#endif

static std::string read_file(const char* path) {
   std::ifstream in(path, std::ios::binary);
   std::stringstream ss;
   ss << in.rdbuf();
   return ss.str();
}
static std::vector<std::uint8_t> bytes_of(const std::string& s) {
   return std::vector<std::uint8_t>(s.begin(), s.end());
}

static void test_seal_round_trip_real_phext() {
   const std::string corpus = read_file("tests/unit-test.phext");
   CHECK(!corpus.empty());
   const auto sealed = phext::seal(corpus);
   CHECK(sealed.has_value());
   if (!sealed) return;
   CHECK(!sealed->empty());
   // an xz stream starts with the xz magic bytes
   CHECK(sealed->size() >= 6);
   CHECK((*sealed)[0] == 0xFD && (*sealed)[1] == '7' && (*sealed)[2] == 'z' && (*sealed)[3] == 'X' &&
         (*sealed)[4] == 'Z' && (*sealed)[5] == 0x00);
   const auto back = phext::unseal_string(sealed->data(), sealed->size());
   CHECK(back.has_value());
   if (back) CHECK_EQ_STR(*back, corpus);
}

static void test_seal_round_trip_empty_and_small() {
   const auto sealed_empty = phext::seal("");
   CHECK(sealed_empty.has_value());
   if (sealed_empty) {
      const auto back = phext::unseal_string(sealed_empty->data(), sealed_empty->size());
      CHECK(back.has_value());
      if (back) CHECK_EQ_STR(*back, "");
   }
   const auto sealed = phext::seal("hello phext");
   CHECK(sealed.has_value());
   if (sealed) {
      const auto back = phext::unseal(sealed->data(), sealed->size());
      CHECK(back.has_value());
      if (back) CHECK(*back == bytes_of("hello phext"));
   }
}

static void test_seal_carrying_binary_scrolls() {
   std::string doc = "cover text";
   doc.push_back(phext::SCROLL_BREAK);
   doc += "phext-binary:1:base64\nAAEC/f7/fw==";
   doc.push_back(phext::SCROLL_BREAK);
   doc += "after";
   const auto sealed = phext::seal(doc);
   CHECK(sealed.has_value());
   if (!sealed) return;
   const auto restored = phext::unseal_string(sealed->data(), sealed->size());
   CHECK(restored.has_value());
   if (restored) CHECK_EQ_STR(*restored, doc);
}

static void test_seal_is_deterministic() {
   std::string doc = "the same phext, sealed twice";
   doc.push_back(phext::SCROLL_BREAK);
   doc += "the same phext, sealed twice";
   const auto first = phext::seal(doc);
   const auto second = phext::seal(doc);
   CHECK(first.has_value() && second.has_value());
   if (first && second) CHECK(*first == *second);
}

static void test_unseal_rejects_non_xz_data() {
   const std::string junk = "this is not an xz stream";
   CHECK(!phext::unseal(bytes_of(junk)).has_value());
   CHECK(!phext::unseal(std::vector<std::uint8_t>{}).has_value());
   const std::string plain = "phext, but unsealed";
   CHECK(!phext::unseal_string(bytes_of(plain)).has_value());
}

int main() {
   RUN(test_seal_round_trip_real_phext);
   RUN(test_seal_round_trip_empty_and_small);
   RUN(test_seal_carrying_binary_scrolls);
   RUN(test_seal_is_deterministic);
   RUN(test_unseal_rejects_non_xz_data);
   return harness::summary("test_seal (test_seal.rs ports, xz enabled)");
}
