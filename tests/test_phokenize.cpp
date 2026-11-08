// Conformance tests ported from libphext-rs v0.5.0 src/test_phokenize.rs
// (8 tests): property tests over deterministic pseudo-random documents —
// phokenize must match the next_scroll reference loop, and fetch / replace /
// insert / remove must act on exactly the target scroll.
#include "../phext.h"
#include "harness.h"

#include <chrono>
#include <fstream>
#include <map>
#include <sstream>

using phext::Coordinate;
static Coordinate C(const char* address) { return phext::to_coordinate(address); }
static std::string read_file(const char* path) {
   std::ifstream in(path, std::ios::binary);
   std::stringstream ss;
   ss << in.rdbuf();
   return ss.str();
}

/// The earlier phokenize: next_scroll in a loop. Quadratic, but the
/// definition of the expected output.
static std::vector<phext::PositionedScroll> reference(const std::string& phext_text) {
   std::vector<phext::PositionedScroll> result;
   Coordinate coord = phext::default_coordinate();
   std::string temp = phext_text;
   while (true) {
      phext::NextScrollResult step = phext::next_scroll(temp, coord);
      result.push_back(step.ps);
      coord = step.next;
      temp = step.remaining;
      if (temp.empty()) break;
   }
   return result;
}

static std::vector<std::pair<std::string, std::string>> pairs(const std::vector<phext::PositionedScroll>& tokens) {
   std::vector<std::pair<std::string, std::string>> out;
   for (const phext::PositionedScroll& t : tokens) out.push_back({t.coord.to_string(), t.scroll});
   return out;
}

static const char BREAKS[9] = {'\x17', '\x18', '\x19', '\x1A', '\x1C', '\x1D', '\x1E', '\x1F', '\x01'};

static std::vector<std::string> edge_cases() {
   std::vector<std::string> cases;
   cases.push_back("");
   cases.push_back("one scroll\nwith two lines");
   cases.push_back("a\x17" "b\x18" "c\x19" "d\x1A" "e\x1C" "f\x1D" "g\x1E" "h\x1F" "i\x01" "j");
   cases.push_back("\x17\x17" "lead\x17\x17\x17" "gap\x01\x01\x17" "deep\x17");
   cases.push_back("trailing one\x17");
   cases.push_back("trailing two\x17\x17");
   cases.push_back("\x01\x1F\x1E\x1D\x1C\x1A\x19\x18\x17");
   // "ünïcödé ✓" + SHELF + "shelf ☃" + BOOK + "boek ü" + SCROLL + "🜁"
   cases.push_back("\xC3\xBC" "n" "\xC3\xAF" "c" "\xC3\xB6" "d" "\xC3\xA9" " " "\xE2\x9C\x93" "\x1F" "shelf "
                   "\xE2\x98\x83" "\x1A" "boek " "\xC3\xBC" "\x17" "\xF0\x9F\x9C\x81");
   cases.push_back("\n\n\x17\n");
   return cases;
}

/// Small deterministic generator, so a failure always reproduces.
struct Lcg {
   std::uint64_t state;
   explicit Lcg(std::uint64_t seed) : state(seed) {}
   std::uint64_t next() {
      state = state * 6364136223846793005ULL + 1442695040888963407ULL;
      return state >> 33;
   }
};

static std::string random_doc(Lcg& rng) {
   static const char* pieces[8] = {"a", "bc", "\xC3\xBC", "\xE2\x98\x83", "\xF0\x9F\x9C\x81", "\n", " ", "text"};
   std::string doc;
   const std::uint64_t count = rng.next() % 60;
   for (std::uint64_t i = 0; i < count; ++i) {
      if (rng.next() % 3 == 0) {
         const std::uint64_t run = 1 + rng.next() % 3;
         for (std::uint64_t k = 0; k < run; ++k) doc.push_back(BREAKS[rng.next() % 9]);
      } else {
         doc += pieces[rng.next() % 8];
      }
   }
   return doc;
}

static std::vector<Coordinate> subspace_targets(const std::string& doc, Lcg& rng) {
   std::vector<Coordinate> targets;
   for (const phext::PositionedScroll& token : phext::phokenize(doc)) {
      targets.push_back(token.coord);
      Coordinate after = token.coord;
      after.scroll_break();
      targets.push_back(after);
      Coordinate deeper = token.coord;
      deeper.section_break();
      targets.push_back(deeper);
   }
   for (int i = 0; i < 4; ++i) {
      char buf[64];
      std::snprintf(buf, sizeof(buf), "%llu.%llu.%llu/%llu.%llu.%llu/%llu.%llu.%llu",
                    (unsigned long long)(1 + rng.next() % 3), (unsigned long long)(1 + rng.next() % 3),
                    (unsigned long long)(1 + rng.next() % 3), (unsigned long long)(1 + rng.next() % 3),
                    (unsigned long long)(1 + rng.next() % 3), (unsigned long long)(1 + rng.next() % 3),
                    (unsigned long long)(1 + rng.next() % 3), (unsigned long long)(1 + rng.next() % 3),
                    (unsigned long long)(1 + rng.next() % 5));
      targets.push_back(C(buf));
   }
   return targets;
}

/// The non-empty scrolls of a document, as explode sees them.
static std::map<std::string, std::string> content(const std::string& doc) {
   std::map<std::string, std::string> out;
   for (const auto& entry : phext::explode(doc)) {
      if (!entry.second.empty()) out[entry.first.to_string()] = entry.second;
   }
   return out;
}

static std::vector<std::string> documents(std::uint64_t seed, std::size_t count) {
   Lcg rng(seed);
   std::vector<std::string> docs = edge_cases();
   docs.push_back("a\x18" "b\x19" "c");
   docs.push_back("a\x17" "b\x17");
   docs.push_back("x\x17");
   docs.push_back("\x19\x1A" "a");
   docs.push_back("a\x18" "b\x19" "c\x1A" "d\x1C" "e\x1D" "f\x1E" "g\x1F" "h\x01" "i");
   {
      std::string deep;
      for (int i = 0; i < 1001; ++i) deep.push_back('\x01');
      deep += "deep";
      for (int i = 0; i < 1001; ++i) deep.push_back('\x1F');
      deep += "deeper";
      docs.push_back(deep);
   }
   for (std::size_t i = 0; i < count; ++i) docs.push_back(random_doc(rng));
   const std::string helios = read_file("tests/regression-1-helios.phext");
   if (!helios.empty()) docs.push_back(helios);
   return docs;
}

static void phokenize_matches_the_next_scroll_loop() {
   for (const std::string& doc : edge_cases()) {
      CHECK(pairs(phext::phokenize(doc)) == pairs(reference(doc)));
   }
   Lcg rng(0x5eed);
   for (int i = 0; i < 5000; ++i) {
      const std::string doc = random_doc(rng);
      CHECK(pairs(phext::phokenize(doc)) == pairs(reference(doc)));
   }
}

static void edits_act_on_exactly_the_target_scroll() {
   Lcg rng(0xc0de);
   for (const std::string& doc : documents(0xc0de, 1500)) {
      const std::map<std::string, std::string> before = content(doc);
      for (const Coordinate& target : subspace_targets(doc, rng)) {
         const std::string key = target.to_string();
         const auto it = before.find(key);
         const std::string current = (it == before.end()) ? "" : it->second;

         CHECK_EQ_STR(phext::fetch(doc, target), current);

         std::map<std::string, std::string> want = before;
         want[key] = "X";
         CHECK(content(phext::replace(doc, target, "X")) == want);

         want = before;
         want[key] = current + "Y";
         CHECK(content(phext::insert(doc, target, "Y")) == want);

         want = before;
         want.erase(key);
         CHECK(content(phext::remove(doc, target)) == want);

         (void)phext::offset(doc, target);
      }
   }
}

static void missing_coordinates_between_scrolls_are_left_alone() {
   const std::string doc = "a\x18" "b\x19" "c";
   const Coordinate between = C("1.1.1/1.1.1/1.1.2");
   CHECK_EQ_STR(phext::fetch(doc, between), "");
   CHECK_EQ_STR(phext::replace(doc, between, "X"), "a\x17" "X\x18" "b\x19" "c");
   CHECK_EQ_STR(phext::insert(doc, between, "Y"), "a\x17" "Y\x18" "b\x19" "c");
   CHECK_EQ_STR(phext::remove(doc, between), doc);
   const Coordinate after = C("1.1.1/1.1.1/1.1.2");
   CHECK_EQ_STR(phext::insert("x\x17", after, "Y"), "x\x17" "Y");
   CHECK_EQ_STR(phext::fetch("a\x17" "b\x17", after), "b");
}

static void subtract_removes_only_scrolls_with_content_on_the_right() {
   const std::vector<std::string> docs = documents(0x5b7, 1000);
   for (std::size_t index = 0; index < docs.size(); ++index) {
      const std::string& left = docs[index];
      const std::string& right = docs[(index + 1) % docs.size()];
      const std::map<std::string, std::string> mask = content(right);
      std::map<std::string, std::string> want;
      for (const auto& entry : content(left)) {
         if (mask.find(entry.first) == mask.end()) want[entry.first] = entry.second;
      }
      CHECK(content(phext::subtract(left, right)) == want);
   }
   CHECK_EQ_STR(phext::subtract("\x17\x17lead", "a\x18" "b"), "\x17\x17lead");
}

static void offset_does_not_panic_before_the_first_scroll() {
   CHECK(phext::offset("\x19\x1A" "a", C("1.1.1/1.1.1/1.1.1")) == 0);
   CHECK(phext::offset("", C("2.2.2/2.2.2/2.2.2")) == 0);
   CHECK(phext::offset("ab\x17" "cd", C("1.1.1/1.1.1/1.1.2")) == 3);
   CHECK(phext::offset("ab\x17" "cd", C("1.1.1/1.1.1/1.1.9")) == 3);
}

static void summaries_keep_multibyte_characters_whole() {
   // U+010A's low byte is 0x0A, a line break — byte scan must not cut there
   CHECK_EQ_STR(phext::create_summary("\xC4\x8A" "bc"), "\xC4\x8A" "bc");
   CHECK_EQ_STR(phext::create_summary("\xE6\x97\xA5\xE6\x9C\xAC\x17rest"), "\xE6\x97\xA5\xE6\x9C\xAC...");
   CHECK_EQ_STR(phext::create_summary("one\ntwo"), "one...");
}

static void explode_and_implode_roundtrip_the_same_scrolls() {
   Lcg rng(0xfab1e);
   for (int i = 0; i < 2000; ++i) {
      const std::string doc = random_doc(rng);
      const std::map<Coordinate, std::string> map = phext::explode(doc);
      const std::string rebuilt = phext::implode(map);
      const std::map<Coordinate, std::string> again = phext::explode(rebuilt);
      auto filled = [](const std::map<Coordinate, std::string>& m) {
         std::vector<std::pair<std::string, std::string>> v;
         for (const auto& entry : m) {
            if (!entry.second.empty()) v.push_back({entry.first.to_string(), entry.second});
         }
         std::sort(v.begin(), v.end());
         return v;
      };
      CHECK(filled(map) == filled(again));
   }
}

static void a_large_phext_explodes_in_linear_time() {
   std::string doc;
   for (int index = 0; index < 50000; ++index) {
      if (index > 0) {
         doc.push_back(index % 1600 == 0 ? '\x1C' : (index % 40 == 0 ? '\x1A' : '\x17'));
      }
      doc += "kind: message\nauthor: bench\n---\nsome words about the topic here";
   }
   const auto start = std::chrono::steady_clock::now();
   const std::map<Coordinate, std::string> map = phext::explode(doc);
   const std::string rebuilt = phext::implode(map);
   const std::string toc = phext::textmap(doc);
   const auto spent = std::chrono::steady_clock::now() - start;
   std::printf("  (50k-scroll explode/implode/textmap: %lld ms; RS asserts <2000 ms)\n",
               (long long)std::chrono::duration_cast<std::chrono::milliseconds>(spent).count());
   CHECK(map.size() == 50000);
   CHECK_EQ_STR(rebuilt, doc);
   std::size_t lines = 0;
   for (char ch : toc) if (ch == '\n') lines += 1;
   CHECK(lines == 50000);
   CHECK(spent < std::chrono::seconds(10));
}

int main() {
   RUN(phokenize_matches_the_next_scroll_loop);
   RUN(edits_act_on_exactly_the_target_scroll);
   RUN(missing_coordinates_between_scrolls_are_left_alone);
   RUN(subtract_removes_only_scrolls_with_content_on_the_right);
   RUN(offset_does_not_panic_before_the_first_scroll);
   RUN(summaries_keep_multibyte_characters_whole);
   RUN(explode_and_implode_roundtrip_the_same_scrolls);
   RUN(a_large_phext_explodes_in_linear_time);
   return harness::summary("test_phokenize (test_phokenize.rs ports)");
}
