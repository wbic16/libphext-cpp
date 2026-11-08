// Legacy trs::string regression tests + the parity audit's section-5 probes
// as regression tests against the NEW phext.h API.
//
// This file intentionally includes trs_string.h WITHOUT the old
// `-include iostream -include algorithm` workaround: the P9 include fix
// must make the legacy header self-contained under g++.
#include "../phext.h"
#include "../trs_string.h"
#include "harness.h"

#include <fstream>
#include <sstream>

static std::vector<std::uint8_t> bytes_of(const std::string& s) {
   return std::vector<std::uint8_t>(s.begin(), s.end());
}
static std::string read_file(const char* path) {
   std::ifstream in(path, std::ios::binary);
   std::stringstream ss;
   ss << in.rdbuf();
   return ss.str();
}

// audit §5.5: loadTerse did not reset walker state — pageCount() accumulated
// across loads (and across expand()/contract(), which re-load internally)
static void probe_loadterse_state_reset() {
   trs::string doc(bytes_of("A\x17" "B"));
   CHECK(doc.pageCount() == 2);
   doc.expand(); // re-loads internally
   CHECK(doc.pageCount() == 2); // was 4 before the P9 fix
   CHECK_EQ_STR((std::string)doc, "A\x18" "B");
   doc.loadTerse(bytes_of("solo"));
   CHECK(doc.pageCount() == 1); // was 3 before the P9 fix
   CHECK_EQ_STR((std::string)doc, "solo");
}

// audit §5.1: the NEW API must not wrap at 65,535 (master's uint16_t did)
static void probe_no_65535_wrap_new_api() {
   phext::Coordinate c;
   for (int i = 0; i < 65535; ++i) c.scroll_break();
   CHECK(c.x.scroll == 65536);
   CHECK(c.validate_coordinate());
   CHECK_EQ_STR(c.to_string(), "1.1.1/1.1.1/1.1.65536");
}

// audit §5.2: summary format — the NEW API follows RS create_summary
// (no 64-byte cap, "..." iff a break was hit, stops at ANY phext break)
static void probe_summary_format_new_api() {
   CHECK_EQ_STR(phext::create_summary(std::string(100, 'x')), std::string(100, 'x'));
   CHECK_EQ_STR(phext::create_summary("ab\ncd"), "ab...");
   CHECK_EQ_STR(phext::create_summary("ab\x17" "cd"), "ab...");
   // ...while the legacy helper keeps its documented divergent behaviour
   CHECK(trs::string::getSummaryText(std::string(100, 'x')).size() == 64);
}

// audit §4 row 1: the NEW API has the correct LINE break constant (0x0A);
// the legacy enum's LINE = 0x10 divergence is pinned here, not silently fixed
static void probe_line_break_constant() {
   CHECK(phext::LINE_BREAK == '\n');
   CHECK(phext::LINE_BREAK_U8 == 0x0A);
   CHECK(static_cast<int>(trs::Break::LINE) == 0x10); // legacy, documented divergence
}

// the legacy smoke values from scripts/test.sh, as unit checks
static void legacy_smoke_values() {
   const std::string hello = read_file("hello-world.t");
   CHECK(!hello.empty());
   trs::string doc(bytes_of(hello));
   CHECK(doc.pageCount() == 8);
   CHECK(doc.size() == 159);
}

int main() {
   RUN(probe_loadterse_state_reset);
   RUN(probe_no_65535_wrap_new_api);
   RUN(probe_summary_format_new_api);
   RUN(probe_line_break_constant);
   RUN(legacy_smoke_values);
   return harness::summary("test_legacy (audit section-5 probes + legacy)");
}
