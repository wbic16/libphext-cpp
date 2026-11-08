// -----------------------------------------------------------------------------------------------------------
// libphext-cpp 0.5.0 — phext.h
//
// A header-only C++17 port of libphext-rs v0.5.0 (wbic16/libphext-rs @ exo),
// mirroring its public API function-for-function: free functions over
// std::string / std::string_view in namespace `phext`.
//
// Parity notes:
//  * All offsets are BYTE offsets over the UTF-8 encoding, exactly as in Rust.
//    std::string is treated as a byte string throughout; nothing here counts
//    characters or columns.
//  * Coordinate components are uint64_t, matching Rust `usize` on 64-bit
//    targets (the only bound is COORDINATE_MINIMUM = 1, as of RS 0.4.0).
//    This differs in width from the legacy trs::Coordinate (uint32_t) in
//    trs_string.h — phext::Coordinate is the parity type.
//  * Rust functions that return String from &str inputs can panic inside
//    String::from_utf8 on non-UTF-8 intermediate bytes; this port stores
//    bytes in std::string and never validates on the text path, so those
//    cases produce the byte-identical result instead of a panic. Scroll
//    *content* semantics are unchanged. Non-UTF-8 data enters canonically
//    through the byte APIs (insert_bytes / replace_bytes / ingest), which
//    apply the RS 0.5.0 base64-verbatim ingestion rule.
//  * Error model (chosen at P1, per the parity audit §5.6):
//      - strict coordinate parse  -> ParseResult with a ParseError code
//      - lenient coordinate parse -> to_coordinate (never fails)
//      - base64 decode            -> std::optional (nullopt = not binary /
//                                    malformed), mirroring Rust Option
//      - xz seal (opt-in)         -> std::optional (nullopt = io error /
//                                    non-UTF-8 for unseal_string)
//    No function in this header throws on bad input, and malformed input
//    never causes undefined behaviour.
//
// Dependencies: the C++ standard library, plus the vendored xxhash.h sitting
// next to this header (single-header XXH_INLINE_ALL mode) for checksum().
// Base64 is hand-rolled in-header. The xz seal is opt-in: define
// PHEXT_CPP_WITH_XZ and link liblzma to enable it (mirrors the RS `xz`
// feature flag, off by default).
//
// (c) 2023-2026 Will Bickford (Phext, Inc.) — MIT License
// -----------------------------------------------------------------------------------------------------------
#ifndef PHEXT_H
#define PHEXT_H

#define PHEXT_CPP_VERSION_MAJOR 0
#define PHEXT_CPP_VERSION_MINOR 5
#define PHEXT_CPP_VERSION_PATCH 0

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// xxHash: XXH3-128 for checksum(). Vendored single header, inlined.
#ifndef PHEXT_CPP_XXHASH_INCLUDED
#define PHEXT_CPP_XXHASH_INCLUDED
#define XXH_INLINE_ALL
#include "xxhash.h"
#endif

namespace phext {

// -----------------------------------------------------------------------------------------------------------
// constants (RS src/phext.rs:81-113)
// -----------------------------------------------------------------------------------------------------------
inline constexpr std::uint64_t COORDINATE_MINIMUM = 1; // human numbering — we start at 1, not 0
// coordinate components are unbounded as of 0.4.0 (the 999 cap was removed)

inline constexpr char LIBRARY_BREAK    = '\x01'; // 11th dimension
inline constexpr char MORE_COWBELL     = '\x07';
inline constexpr char LINE_BREAK       = '\x0A';
inline constexpr char SCROLL_BREAK     = '\x17'; // 3D break
inline constexpr char SECTION_BREAK    = '\x18'; // 4D break
inline constexpr char CHAPTER_BREAK    = '\x19'; // 5D break
inline constexpr char BOOK_BREAK       = '\x1A'; // 6D break
inline constexpr char VOLUME_BREAK     = '\x1C'; // 7D break
inline constexpr char COLLECTION_BREAK = '\x1D'; // 8D break
inline constexpr char SERIES_BREAK     = '\x1E'; // 9D break
inline constexpr char SHELF_BREAK      = '\x1F'; // 10th dimension

inline constexpr std::uint8_t LIBRARY_BREAK_U8    = 0x01;
inline constexpr std::uint8_t MORE_COWBELL_U8     = 0x07;
inline constexpr std::uint8_t LINE_BREAK_U8       = 0x0A;
inline constexpr std::uint8_t SCROLL_BREAK_U8     = 0x17;
inline constexpr std::uint8_t SECTION_BREAK_U8    = 0x18;
inline constexpr std::uint8_t CHAPTER_BREAK_U8    = 0x19;
inline constexpr std::uint8_t BOOK_BREAK_U8       = 0x1A;
inline constexpr std::uint8_t VOLUME_BREAK_U8     = 0x1C;
inline constexpr std::uint8_t COLLECTION_BREAK_U8 = 0x1D;
inline constexpr std::uint8_t SERIES_BREAK_U8     = 0x1E;
inline constexpr std::uint8_t SHELF_BREAK_U8      = 0x1F;

inline constexpr std::uint8_t ADDRESS_MICRO_BREAK = '.'; // delimiter for micro-coordinates
inline constexpr std::uint8_t ADDRESS_MACRO_BREAK = '/'; // delimiter for macro-coordinates
inline constexpr std::uint8_t ADDRESS_MACRO_ALT   = ';'; // also allow ';' for url encoding

// -----------------------------------------------------------------------------------------------------------
// coordinate types (RS src/phext.rs:156-306)
// -----------------------------------------------------------------------------------------------------------
struct ZCoordinate {
   std::uint64_t library = 1;
   std::uint64_t shelf = 1;
   std::uint64_t series = 1;
   std::string to_string() const {
      return std::to_string(library) + "." + std::to_string(shelf) + "." + std::to_string(series);
   }
};
inline bool operator==(const ZCoordinate& a, const ZCoordinate& b) {
   return a.library == b.library && a.shelf == b.shelf && a.series == b.series;
}
inline bool operator!=(const ZCoordinate& a, const ZCoordinate& b) { return !(a == b); }
inline bool operator<(const ZCoordinate& a, const ZCoordinate& b) {
   if (a.library != b.library) return a.library < b.library;
   if (a.shelf != b.shelf) return a.shelf < b.shelf;
   return a.series < b.series;
}
inline bool operator>(const ZCoordinate& a, const ZCoordinate& b) { return b < a; }
inline bool operator<=(const ZCoordinate& a, const ZCoordinate& b) { return !(b < a); }
inline bool operator>=(const ZCoordinate& a, const ZCoordinate& b) { return !(a < b); }

struct YCoordinate {
   std::uint64_t collection = 1;
   std::uint64_t volume = 1;
   std::uint64_t book = 1;
   std::string to_string() const {
      return std::to_string(collection) + "." + std::to_string(volume) + "." + std::to_string(book);
   }
};
inline bool operator==(const YCoordinate& a, const YCoordinate& b) {
   return a.collection == b.collection && a.volume == b.volume && a.book == b.book;
}
inline bool operator!=(const YCoordinate& a, const YCoordinate& b) { return !(a == b); }
inline bool operator<(const YCoordinate& a, const YCoordinate& b) {
   if (a.collection != b.collection) return a.collection < b.collection;
   if (a.volume != b.volume) return a.volume < b.volume;
   return a.book < b.book;
}
inline bool operator>(const YCoordinate& a, const YCoordinate& b) { return b < a; }
inline bool operator<=(const YCoordinate& a, const YCoordinate& b) { return !(b < a); }
inline bool operator>=(const YCoordinate& a, const YCoordinate& b) { return !(a < b); }

struct XCoordinate {
   std::uint64_t chapter = 1;
   std::uint64_t section = 1;
   std::uint64_t scroll = 1;
   std::string to_string() const {
      return std::to_string(chapter) + "." + std::to_string(section) + "." + std::to_string(scroll);
   }
};
inline bool operator==(const XCoordinate& a, const XCoordinate& b) {
   return a.chapter == b.chapter && a.section == b.section && a.scroll == b.scroll;
}
inline bool operator!=(const XCoordinate& a, const XCoordinate& b) { return !(a == b); }
inline bool operator<(const XCoordinate& a, const XCoordinate& b) {
   if (a.chapter != b.chapter) return a.chapter < b.chapter;
   if (a.section != b.section) return a.section < b.section;
   return a.scroll < b.scroll;
}
inline bool operator>(const XCoordinate& a, const XCoordinate& b) { return b < a; }
inline bool operator<=(const XCoordinate& a, const XCoordinate& b) { return !(b < a); }
inline bool operator>=(const XCoordinate& a, const XCoordinate& b) { return !(a < b); }

// Phext coordinates are formed along a 9-dimensional hierarchy:
// z3.z2.z1/y3.y2.y1/x3.x2.x1. Dense graphs: phext is sparse by design.
// Coordinate components are unbounded, but a fully-populated dense graph
// contains the product of its dimensions' extents in scrolls — sparsity,
// not a cap, is the protection. Keep graphs sparse.
struct Coordinate {
   ZCoordinate z;
   YCoordinate y;
   XCoordinate x;

   bool validate_coordinate() const {
      return z.library >= COORDINATE_MINIMUM && z.shelf >= COORDINATE_MINIMUM &&
             z.series >= COORDINATE_MINIMUM && y.collection >= COORDINATE_MINIMUM &&
             y.volume >= COORDINATE_MINIMUM && y.book >= COORDINATE_MINIMUM &&
             x.chapter >= COORDINATE_MINIMUM && x.section >= COORDINATE_MINIMUM &&
             x.scroll >= COORDINATE_MINIMUM;
   }

   // canonical "z.z.z/y.y.y/x.x.x"; "" when invalid (RS Coordinate::to_string)
   std::string to_string() const {
      if (!validate_coordinate()) return "";
      return z.to_string() + "/" + y.to_string() + "/" + x.to_string();
   }

   // url-encoded "z.z.z;y.y.y;x.x.x"; "" when invalid
   std::string to_urlencoded() const {
      if (!validate_coordinate()) return "";
      return z.to_string() + ";" + y.to_string() + ";" + x.to_string();
   }

   // break mutators: increment one component, reset all lower components to 1.
   // components are unbounded: advancing never saturates.
   void scroll_break() { x.scroll = x.scroll + 1; }
   void section_break() { x.section = x.section + 1; x.scroll = 1; }
   void chapter_break() { x.chapter = x.chapter + 1; x.section = 1; x.scroll = 1; }
   void book_break() { y.book = y.book + 1; x.chapter = 1; x.section = 1; x.scroll = 1; }
   void volume_break() { y.volume = y.volume + 1; y.book = 1; x.chapter = 1; x.section = 1; x.scroll = 1; }
   void collection_break() {
      y.collection = y.collection + 1; y.volume = 1; y.book = 1; x.chapter = 1; x.section = 1; x.scroll = 1;
   }
   void series_break() {
      z.series = z.series + 1; y.collection = 1; y.volume = 1; y.book = 1; x.chapter = 1; x.section = 1; x.scroll = 1;
   }
   void shelf_break() {
      z.shelf = z.shelf + 1; z.series = 1; y.collection = 1; y.volume = 1; y.book = 1;
      x.chapter = 1; x.section = 1; x.scroll = 1;
   }
   void library_break() {
      z.library = z.library + 1; z.shelf = 1; z.series = 1; y.collection = 1; y.volume = 1; y.book = 1;
      x.chapter = 1; x.section = 1; x.scroll = 1;
   }
};
inline bool operator==(const Coordinate& a, const Coordinate& b) { return a.z == b.z && a.y == b.y && a.x == b.x; }
inline bool operator!=(const Coordinate& a, const Coordinate& b) { return !(a == b); }
inline bool operator<(const Coordinate& a, const Coordinate& b) {
   if (a.z != b.z) return a.z < b.z;
   if (a.y != b.y) return a.y < b.y;
   return a.x < b.x;
}
inline bool operator>(const Coordinate& a, const Coordinate& b) { return b < a; }
inline bool operator<=(const Coordinate& a, const Coordinate& b) { return !(b < a); }
inline bool operator>=(const Coordinate& a, const Coordinate& b) { return !(a < b); }

inline Coordinate default_coordinate() { return Coordinate{}; }

struct PositionedScroll {
   Coordinate coord;
   std::string scroll;
};
inline bool operator==(const PositionedScroll& a, const PositionedScroll& b) {
   return a.coord == b.coord && a.scroll == b.scroll;
}
inline bool operator!=(const PositionedScroll& a, const PositionedScroll& b) { return !(a == b); }
inline bool operator<(const PositionedScroll& a, const PositionedScroll& b) { return a.coord < b.coord; }

struct Range {
   Coordinate start;
   Coordinate end;
   std::string to_string() const { return start.to_string() + "-" + end.to_string(); }
};

// the RS PhextParseError message
inline const char* parse_error_message() {
   return "Phext addresses are of the form LB.SF.SR/CN.VM.BK/CH.SN.SC";
}

enum class ParseError { None, BadShape, BadComponent };

struct ParseResult {
   bool ok = false;
   Coordinate coord{};
   ParseError error = ParseError::None;
};

// -----------------------------------------------------------------------------------------------------------
// detail helpers (private in RS)
// -----------------------------------------------------------------------------------------------------------
namespace detail {

inline bool is_dimension_break(std::uint8_t byte) {
   return byte == SCROLL_BREAK_U8 || byte == SECTION_BREAK_U8 || byte == CHAPTER_BREAK_U8 ||
          byte == BOOK_BREAK_U8 || byte == VOLUME_BREAK_U8 || byte == COLLECTION_BREAK_U8 ||
          byte == SERIES_BREAK_U8 || byte == SHELF_BREAK_U8 || byte == LIBRARY_BREAK_U8;
}

inline void apply_break(Coordinate& location, std::uint8_t byte) {
   if (byte == SCROLL_BREAK_U8) location.scroll_break();
   if (byte == SECTION_BREAK_U8) location.section_break();
   if (byte == CHAPTER_BREAK_U8) location.chapter_break();
   if (byte == BOOK_BREAK_U8) location.book_break();
   if (byte == VOLUME_BREAK_U8) location.volume_break();
   if (byte == COLLECTION_BREAK_U8) location.collection_break();
   if (byte == SERIES_BREAK_U8) location.series_break();
   if (byte == SHELF_BREAK_U8) location.shelf_break();
   if (byte == LIBRARY_BREAK_U8) location.library_break();
}

// breaks that walk `from` up to `target` (RS replace/insert fixup loops)
inline std::string build_fixups(Coordinate from, const Coordinate& target) {
   std::string fixup;
   while (from.z.library < target.z.library) { fixup.push_back(LIBRARY_BREAK); from.library_break(); }
   while (from.z.shelf < target.z.shelf) { fixup.push_back(SHELF_BREAK); from.shelf_break(); }
   while (from.z.series < target.z.series) { fixup.push_back(SERIES_BREAK); from.series_break(); }
   while (from.y.collection < target.y.collection) { fixup.push_back(COLLECTION_BREAK); from.collection_break(); }
   while (from.y.volume < target.y.volume) { fixup.push_back(VOLUME_BREAK); from.volume_break(); }
   while (from.y.book < target.y.book) { fixup.push_back(BOOK_BREAK); from.book_break(); }
   while (from.x.chapter < target.x.chapter) { fixup.push_back(CHAPTER_BREAK); from.chapter_break(); }
   while (from.x.section < target.x.section) { fixup.push_back(SECTION_BREAK); from.section_break(); }
   while (from.x.scroll < target.x.scroll) { fixup.push_back(SCROLL_BREAK); from.scroll_break(); }
   return fixup;
}

inline void append_scroll(std::string& output, const PositionedScroll& token, Coordinate coord) {
   while (coord < token.coord) {
      if (coord.z.library < token.coord.z.library) { output.push_back(LIBRARY_BREAK); coord.library_break(); continue; }
      if (coord.z.shelf < token.coord.z.shelf) { output.push_back(SHELF_BREAK); coord.shelf_break(); continue; }
      if (coord.z.series < token.coord.z.series) { output.push_back(SERIES_BREAK); coord.series_break(); continue; }
      if (coord.y.collection < token.coord.y.collection) { output.push_back(COLLECTION_BREAK); coord.collection_break(); continue; }
      if (coord.y.volume < token.coord.y.volume) { output.push_back(VOLUME_BREAK); coord.volume_break(); continue; }
      if (coord.y.book < token.coord.y.book) { output.push_back(BOOK_BREAK); coord.book_break(); continue; }
      if (coord.x.chapter < token.coord.x.chapter) { output.push_back(CHAPTER_BREAK); coord.chapter_break(); continue; }
      if (coord.x.section < token.coord.x.section) { output.push_back(SECTION_BREAK); coord.section_break(); continue; }
      if (coord.x.scroll < token.coord.x.scroll) { output.push_back(SCROLL_BREAK); coord.scroll_break(); continue; }
   }
   output += token.scroll;
}

// strict UTF-8 validation, matching Rust str::from_utf8 acceptance exactly
// (rejects overlong forms, surrogates, and code points above U+10FFFF)
inline bool is_valid_utf8(const std::uint8_t* data, std::size_t len) {
   std::size_t i = 0;
   while (i < len) {
      const std::uint8_t b = data[i];
      if (b < 0x80) { i += 1; continue; }
      std::uint32_t cp;
      std::size_t extra;
      std::uint32_t min_cp;
      if (b >= 0xC2 && b <= 0xDF) { cp = b & 0x1F; extra = 1; min_cp = 0x80; }
      else if (b >= 0xE0 && b <= 0xEF) { cp = b & 0x0F; extra = 2; min_cp = 0x800; }
      else if (b >= 0xF0 && b <= 0xF4) { cp = b & 0x07; extra = 3; min_cp = 0x10000; }
      else return false;
      if (i + extra >= len) return false; // continuation bytes i+1..i+extra must exist
      for (std::size_t k = 1; k <= extra; ++k) {
         const std::uint8_t cb = data[i + k];
         if (cb < 0x80 || cb > 0xBF) return false;
         cp = (cp << 6) | (cb & 0x3F);
      }
      if (cp < min_cp) return false;                    // overlong
      if (cp >= 0xD800 && cp <= 0xDFFF) return false;   // surrogate
      if (cp > 0x10FFFF) return false;
      i += extra + 1;
   }
   return true;
}

} // namespace detail

// -----------------------------------------------------------------------------------------------------------
// coordinate parsing — two deliberately different doors, as in RS (audit §5.8)
// -----------------------------------------------------------------------------------------------------------

// strict parser (RS TryFrom<&str> for Coordinate, PR #8): exactly 3 '/'-parts
// of exactly 3 '.'-components each; every component must parse as a uint64
// (Rust usize parse semantics: an optional leading '+' is accepted; empty,
// negative, whitespace-bearing, non-numeric, or overflowing components are
// errors). Never UB, never throws. Note: "0" parses — validity is a separate
// validate_coordinate() question, exactly as in RS.
inline ParseResult parse_coordinate(std::string_view address) {
   ParseResult result;
   // split_on('/', ...) preserving empty segments, then split_on('.', ...)
   std::vector<std::string_view> parts;
   {
      std::size_t start = 0;
      for (std::size_t i = 0; i <= address.size(); ++i) {
         if (i == address.size() || address[i] == '/') {
            parts.push_back(address.substr(start, i - start));
            start = i + 1;
         }
      }
   }
   if (parts.size() != 3) { result.error = ParseError::BadShape; return result; }

   std::uint64_t* fields[9] = {&result.coord.z.library, &result.coord.z.shelf, &result.coord.z.series,
                               &result.coord.y.collection, &result.coord.y.volume, &result.coord.y.book,
                               &result.coord.x.chapter, &result.coord.x.section, &result.coord.x.scroll};
   for (int arm = 0; arm < 3; ++arm) {
      std::vector<std::string_view> comps;
      const std::string_view part = parts[arm];
      std::size_t start = 0;
      for (std::size_t i = 0; i <= part.size(); ++i) {
         if (i == part.size() || part[i] == '.') {
            comps.push_back(part.substr(start, i - start));
            start = i + 1;
         }
      }
      if (comps.size() != 3) { result.error = ParseError::BadShape; return result; }
      for (int c = 0; c < 3; ++c) {
         std::string_view text = comps[c];
         if (!text.empty() && text[0] == '+') text.remove_prefix(1); // Rust usize parse accepts '+'
         if (text.empty()) { result.error = ParseError::BadComponent; return result; }
         std::uint64_t value = 0;
         for (char ch : text) {
            if (ch < '0' || ch > '9') { result.error = ParseError::BadComponent; return result; }
            const std::uint64_t digit = static_cast<std::uint64_t>(ch - '0');
            if (value > (UINT64_MAX - digit) / 10) { result.error = ParseError::BadComponent; return result; }
            value = value * 10 + digit;
         }
         *fields[arm * 3 + c] = value;
      }
   }
   result.ok = true;
   result.error = ParseError::None;
   return result;
}

// lenient legacy parser (RS to_coordinate): byte scan; digits accumulate with
// saturating u64 math; '.' '/' ';' delimit; all other bytes are ignored;
// missing components stay at their defaults; never errors.
inline Coordinate to_coordinate(std::string_view address) {
   Coordinate result = default_coordinate();
   std::uint8_t index = 0;
   std::uint64_t value = 0;
   const std::uint64_t exp = 10;
   for (char raw : address) {
      const std::uint8_t byte = static_cast<std::uint8_t>(raw);
      if (byte == ADDRESS_MICRO_BREAK || byte == ADDRESS_MACRO_BREAK || byte == ADDRESS_MACRO_ALT) {
         switch (index) {
            case 1: result.z.library = value; index += 1; break;
            case 2: result.z.shelf = value; index += 1; break;
            case 3: result.z.series = value; index += 1; break;
            case 4: result.y.collection = value; index += 1; break;
            case 5: result.y.volume = value; index += 1; break;
            case 6: result.y.book = value; index += 1; break;
            case 7: result.x.chapter = value; index += 1; break;
            case 8: result.x.section = value; index += 1; break;
            default: break;
         }
         value = 0;
      }
      if (byte >= 0x30 && byte <= 0x39) {
         // saturating_mul / saturating_add, as in RS
         if (value > (UINT64_MAX - static_cast<std::uint64_t>(byte - 0x30)) / exp) {
            value = UINT64_MAX;
         } else {
            value = exp * value + static_cast<std::uint64_t>(byte - 0x30);
         }
         if (index == 0) index = 1;
      }
   }
   if (index > 0) result.x.scroll = value;
   return result;
}

// -----------------------------------------------------------------------------------------------------------
// predicates
// -----------------------------------------------------------------------------------------------------------
inline bool check_for_cowbell(std::string_view input) {
   for (char byte : input) {
      if (static_cast<std::uint8_t>(byte) == MORE_COWBELL_U8) return true;
   }
   return false;
}

// true for the ten phext break bytes (line break included, cowbell not)
inline bool is_phext_break(std::uint8_t byte) {
   return byte == LINE_BREAK_U8 || byte == SCROLL_BREAK_U8 || byte == SECTION_BREAK_U8 ||
          byte == CHAPTER_BREAK_U8 || byte == BOOK_BREAK_U8 || byte == VOLUME_BREAK_U8 ||
          byte == COLLECTION_BREAK_U8 || byte == SERIES_BREAK_U8 || byte == SHELF_BREAK_U8 ||
          byte == LIBRARY_BREAK_U8;
}

// -----------------------------------------------------------------------------------------------------------
// subspace core (RS get_subspace_coordinates and the editors standing on it)
// -----------------------------------------------------------------------------------------------------------
struct SubspaceCoordinates {
   std::size_t start = 0;
   std::size_t end = 0;
   Coordinate best{};
};

// Finds the byte offsets (start, end, best) for the given coordinate:
//  - when the document reaches target, start..end is that scroll's text and
//    best is target;
//  - otherwise start == end is where the scroll would go (just before the
//    break that steps past target, or the end of the document) and best is
//    the coordinate in effect there.
inline SubspaceCoordinates get_subspace_coordinates(const std::uint8_t* subspace, std::size_t len, Coordinate target) {
   Coordinate walker = default_coordinate();
   Coordinate best = default_coordinate();
   std::optional<std::size_t> found;
   const std::size_t max = len;
   std::size_t position = 0;
   bool moved = true;

   while (true) {
      // `walker` is the coordinate in effect at `position`
      if (moved && !found.has_value()) {
         if (walker == target) {
            found = position;
            best = walker;
         } else if (walker < target) {
            best = walker;
         } else {
            // before the first scroll: target sorts ahead of 1.1.1/1.1.1/1.1.1
            return SubspaceCoordinates{position, position, best};
         }
         moved = false;
      }
      if (position == max) break;

      const std::uint8_t next = subspace[position];
      if (detail::is_dimension_break(next)) {
         detail::apply_break(walker, next);
         moved = true;
         if (walker > target) {
            if (found.has_value()) return SubspaceCoordinates{*found, position, best};
            return SubspaceCoordinates{position, position, best};
         }
         // retained defensively from RS (a break after `found` that does not
         // move the walker would leave the scroll text after the break)
         if (found.has_value() && *found == position) {
            found = position + 1;
         }
      }
      position += 1;
   }

   if (found.has_value()) return SubspaceCoordinates{*found, max, best};
   return SubspaceCoordinates{max, max, best};
}
inline SubspaceCoordinates get_subspace_coordinates(std::string_view subspace, Coordinate target) {
   return get_subspace_coordinates(reinterpret_cast<const std::uint8_t*>(subspace.data()), subspace.size(), target);
}

inline std::string fetch(std::string_view input, Coordinate target) {
   const SubspaceCoordinates parts = get_subspace_coordinates(input, target);
   if (parts.end > parts.start) {
      return std::string(input.substr(parts.start, parts.end - parts.start));
   }
   return "";
}

inline std::string replace(std::string_view input, Coordinate location, std::string_view scroll) {
   const SubspaceCoordinates parts = get_subspace_coordinates(input, location);
   const std::size_t start = parts.start;
   std::size_t end = parts.end;
   const std::string fixup = detail::build_fixups(parts.best, location);
   if (end > input.size()) end = input.size();
   std::string result;
   result.reserve(start + fixup.size() + scroll.size() + (input.size() - end));
   result.append(input.substr(0, start));
   result += fixup;
   result.append(scroll);
   result.append(input.substr(end));
   return result;
}

inline std::string range_replace(std::string_view input, Range location, std::string_view scroll) {
   const SubspaceCoordinates parts_start = get_subspace_coordinates(input, location.start);
   const SubspaceCoordinates parts_end = get_subspace_coordinates(input, location.end);
   const std::size_t start = parts_start.start;
   std::size_t end = parts_end.end;
   if (end > input.size()) end = input.size();
   std::string result;
   result.reserve(start + scroll.size() + (input.size() - end));
   result.append(input.substr(0, start));
   result.append(scroll);
   result.append(input.substr(end));
   return result;
}

inline std::string insert(std::string input, Coordinate location, std::string_view scroll) {
   const SubspaceCoordinates parts = get_subspace_coordinates(input, location);
   const std::size_t end = parts.end;
   const std::string fixup = detail::build_fixups(parts.best, location);
   std::string result;
   result.reserve(end + fixup.size() + scroll.size() + (input.size() - end));
   result.append(std::string_view(input).substr(0, end));
   result += fixup;
   result.append(scroll);
   result.append(std::string_view(input).substr(end));
   return result;
}

// -----------------------------------------------------------------------------------------------------------
// token layer
// -----------------------------------------------------------------------------------------------------------
struct NextScrollResult {
   PositionedScroll ps;
   Coordinate next;
   std::string remaining;
};

// retrieves the next scroll from the given string, assuming an arbitrary
// starting point (RS next_scroll, transliterated including its quirks)
inline NextScrollResult next_scroll(std::string_view input, Coordinate start) {
   Coordinate location = start;
   std::string output;
   std::string remaining;
   std::size_t pi = 0;
   Coordinate begin = start;
   const std::size_t pmax = input.size();
   while (pi < pmax) {
      bool dimension_break = false;
      const std::uint8_t byte = static_cast<std::uint8_t>(input[pi]);
      if (byte == SCROLL_BREAK_U8) { location.scroll_break(); dimension_break = true; }
      if (byte == SECTION_BREAK_U8) { location.section_break(); dimension_break = true; }
      if (byte == CHAPTER_BREAK_U8) { location.chapter_break(); dimension_break = true; }
      if (byte == BOOK_BREAK_U8) { location.book_break(); dimension_break = true; }
      if (byte == VOLUME_BREAK_U8) { location.volume_break(); dimension_break = true; }
      if (byte == COLLECTION_BREAK_U8) { location.collection_break(); dimension_break = true; }
      if (byte == SERIES_BREAK_U8) { location.series_break(); dimension_break = true; }
      if (byte == SHELF_BREAK_U8) { location.shelf_break(); dimension_break = true; }
      if (byte == LIBRARY_BREAK_U8) { location.library_break(); dimension_break = true; }

      if (dimension_break) {
         if (!output.empty()) {
            pi += 1;
            break;
         }
      } else {
         begin = location;
         output.push_back(input[pi]);
      }
      pi += 1;
   }
   while (pi < input.size()) {
      remaining.push_back(input[pi]);
      pi += 1;
   }
   return NextScrollResult{PositionedScroll{begin, output}, location, remaining};
}

// transforms a packed phext buffer into a phext token (phoken) stream —
// one pass over the bytes (RS phokenize, 0.4.x O(n) form)
inline std::vector<PositionedScroll> phokenize(std::string_view input) {
   std::vector<PositionedScroll> result;
   const std::size_t max = input.size();
   Coordinate coord = default_coordinate();
   std::size_t pi = 0;
   while (true) {
      Coordinate begin = coord;
      std::optional<std::size_t> text_start;
      std::size_t text_end = max;
      while (pi < max) {
         const std::uint8_t byte = static_cast<std::uint8_t>(input[pi]);
         if (detail::is_dimension_break(byte)) {
            detail::apply_break(coord, byte);
            if (text_start.has_value()) {
               text_end = pi;
               pi += 1;
               break;
            }
         } else if (!text_start.has_value()) {
            text_start = pi;
            begin = coord;
         }
         pi += 1;
      }
      std::string scroll;
      if (text_start.has_value()) {
         scroll = std::string(input.substr(*text_start, text_end - *text_start));
      }
      result.push_back(PositionedScroll{begin, scroll});
      if (pi >= max) break;
   }
   return result;
}

// serialises positioned scrolls (RS dephokenize: empty scrolls are skipped,
// minimal breaks are emitted; input order is the caller's responsibility)
inline std::string dephokenize(std::vector<PositionedScroll> tokens) {
   std::string result;
   Coordinate coord = default_coordinate();
   for (const PositionedScroll& ps : tokens) {
      if (!ps.scroll.empty()) {
         detail::append_scroll(result, ps, coord);
      }
      coord = ps.coord;
   }
   return result;
}

inline std::map<Coordinate, std::string> explode(std::string_view input) {
   const std::vector<PositionedScroll> parts = phokenize(input);
   std::map<Coordinate, std::string> hash;
   for (const PositionedScroll& row : parts) {
      hash[row.coord] = row.scroll; // later duplicate coords overwrite, as in RS
   }
   return hash;
}

inline std::string implode(const std::map<Coordinate, std::string>& map) {
   std::vector<PositionedScroll> vec;
   vec.reserve(map.size());
   for (const auto& entry : map) {
      vec.push_back(PositionedScroll{entry.first, entry.second});
   }
   // std::map iteration is already coordinate order (RS sorts explicitly)
   return dephokenize(vec);
}

// combines left and right into a new phext, content merged per scroll;
// on a coordinate tie BOTH scrolls are emitted (RS merge semantics)
inline std::string merge(std::string_view left, std::string_view right) {
   const std::vector<PositionedScroll> tl = phokenize(left);
   const std::vector<PositionedScroll> tr = phokenize(right);
   std::size_t tli = 0;
   std::size_t tri = 0;
   std::string result;
   Coordinate coord = default_coordinate();
   while (true) {
      const bool have_left = tli < tl.size();
      const bool have_right = tri < tr.size();
      const bool pick_left = have_left && (!have_right || tl[tli].coord <= tr[tri].coord);
      const bool pick_right = have_right && (!have_left || tr[tri].coord <= tl[tli].coord);
      if (pick_left) {
         detail::append_scroll(result, tl[tli], coord);
         coord = tl[tli].coord;
         tli += 1;
      }
      if (pick_right) {
         detail::append_scroll(result, tr[tri], coord);
         coord = tr[tri].coord;
         tri += 1;
      }
      if (!pick_left && !pick_right) break;
   }
   return result;
}

// left's scrolls, minus any coordinate where right has non-empty content
inline std::string subtract(std::string_view left, std::string_view right) {
   const std::vector<PositionedScroll> pl = phokenize(left);
   const std::vector<PositionedScroll> pr = phokenize(right);
   std::string result;
   std::size_t pri = 0;
   Coordinate coord = default_coordinate();
   for (const PositionedScroll& token : pl) {
      if (token.scroll.empty()) continue;
      while (pri < pr.size() && pr[pri].coord < token.coord) pri += 1;
      const bool masked = pri < pr.size() && pr[pri].coord == token.coord && !pr[pri].scroll.empty();
      if (!masked) {
         detail::append_scroll(result, token, coord);
         coord = token.coord;
      }
   }
   return result;
}

inline std::string normalize(std::string_view input) {
   return dephokenize(phokenize(input));
}

inline std::string remove(std::string_view input, Coordinate location) {
   const std::string phase1 = replace(input, location, "");
   return normalize(phase1);
}

// -----------------------------------------------------------------------------------------------------------
// transforms + listings
// -----------------------------------------------------------------------------------------------------------

// byte-wise promotion of every break byte (RS expand): \n -> 0x17 -> 0x18 ->
// 0x19 -> 0x1A -> 0x1C -> 0x1D -> 0x1E -> 0x1F -> 0x01; 0x01 unchanged —
// including line breaks inside scroll text
inline std::string expand(std::string_view input) {
   std::string buffer(input);
   for (char& raw : buffer) {
      const std::uint8_t byte = static_cast<std::uint8_t>(raw);
      if (byte == LINE_BREAK_U8) raw = SCROLL_BREAK;
      else if (byte == SCROLL_BREAK_U8) raw = SECTION_BREAK;
      else if (byte == SECTION_BREAK_U8) raw = CHAPTER_BREAK;
      else if (byte == CHAPTER_BREAK_U8) raw = BOOK_BREAK;
      else if (byte == BOOK_BREAK_U8) raw = VOLUME_BREAK;
      else if (byte == VOLUME_BREAK_U8) raw = COLLECTION_BREAK;
      else if (byte == COLLECTION_BREAK_U8) raw = SERIES_BREAK;
      else if (byte == SERIES_BREAK_U8) raw = SHELF_BREAK;
      else if (byte == SHELF_BREAK_U8) raw = LIBRARY_BREAK;
   }
   return buffer;
}

// exact inverse byte-wise demotion (RS contract)
inline std::string contract(std::string_view input) {
   std::string buffer(input);
   for (char& raw : buffer) {
      const std::uint8_t byte = static_cast<std::uint8_t>(raw);
      if (byte == LIBRARY_BREAK_U8) raw = SHELF_BREAK;
      else if (byte == SHELF_BREAK_U8) raw = SERIES_BREAK;
      else if (byte == SERIES_BREAK_U8) raw = COLLECTION_BREAK;
      else if (byte == COLLECTION_BREAK_U8) raw = VOLUME_BREAK;
      else if (byte == VOLUME_BREAK_U8) raw = BOOK_BREAK;
      else if (byte == BOOK_BREAK_U8) raw = CHAPTER_BREAK;
      else if (byte == CHAPTER_BREAK_U8) raw = SECTION_BREAK;
      else if (byte == SECTION_BREAK_U8) raw = SCROLL_BREAK;
      else if (byte == SCROLL_BREAK_U8) raw = LINE_BREAK;
   }
   return buffer;
}

// text up to the first phext break (byte scan; breaks are ASCII so the cut
// is always a UTF-8 character boundary), plus "..." iff a break was hit.
// No length cap (RS create_summary).
inline std::string create_summary(std::string_view input) {
   std::size_t end = 0;
   while (end < input.size()) {
      if (is_phext_break(static_cast<std::uint8_t>(input[end]))) break;
      end += 1;
   }
   std::string summary(input.substr(0, end));
   if (end < input.size()) summary += "...";
   return summary;
}

inline std::string navmap(std::string_view urlbase, std::string_view input) {
   const std::vector<PositionedScroll> phokens = phokenize(input);
   std::string result;
   if (!phokens.empty()) result += "<ul>\n";
   for (const PositionedScroll& phoken : phokens) {
      result += "<li><a href=\"";
      result += urlbase;
      result += phoken.coord.to_urlencoded();
      result += "\">";
      result += phoken.coord.to_string();
      result += " ";
      result += create_summary(phoken.scroll);
      result += "</a></li>\n";
   }
   if (!phokens.empty()) result += "</ul>\n";
   return result;
}

inline std::string textmap(std::string_view input) {
   const std::vector<PositionedScroll> phokens = phokenize(input);
   std::string result;
   for (const PositionedScroll& phoken : phokens) {
      result += "* ";
      result += phoken.coord.to_string();
      result += ": ";
      result += create_summary(phoken.scroll);
      result += "\n";
   }
   return result;
}

namespace detail {
inline std::string soundex_internal(const std::string& byte_str) {
   static const char* letter1 = "bpfv";
   static const char* letter2 = "cskgjqxz";
   static const char* letter3 = "dt";
   static const char* letter4 = "l";
   static const char* letter5 = "mn";
   static const char* letter6 = "r";
   std::uint64_t value = 1; // 1-100
   for (char raw : byte_str) {
      const char c = raw;
      if (std::strchr(letter1, c) && c != '\0') { value += 1; continue; }
      if (std::strchr(letter2, c) && c != '\0') { value += 2; continue; }
      if (std::strchr(letter3, c) && c != '\0') { value += 3; continue; }
      if (std::strchr(letter4, c) && c != '\0') { value += 4; continue; }
      if (std::strchr(letter5, c) && c != '\0') { value += 5; continue; }
      if (std::strchr(letter6, c) && c != '\0') { value += 6; continue; }
   }
   return std::to_string(value % 99);
}
} // namespace detail

inline std::string soundex_v1(std::string_view input) {
   std::vector<PositionedScroll> phokens = phokenize(input);
   for (PositionedScroll& ith : phokens) {
      ith.scroll = detail::soundex_internal(ith.scroll);
   }
   return dephokenize(phokens);
}

// -----------------------------------------------------------------------------------------------------------
// offsets (RS index / offset — byte offsets)
// -----------------------------------------------------------------------------------------------------------
namespace detail {
inline std::vector<PositionedScroll> index_phokens(std::string_view input) {
   const std::vector<PositionedScroll> phokens = phokenize(input);
   std::uint64_t offset = 0;
   Coordinate coord = default_coordinate();
   std::vector<PositionedScroll> output;
   for (const PositionedScroll& phoken : phokens) {
      const Coordinate reference = phoken.coord;
      while (coord.z.library < reference.z.library) { coord.library_break(); offset += 1; }
      while (coord.z.shelf < reference.z.shelf) { coord.shelf_break(); offset += 1; }
      while (coord.z.series < reference.z.series) { coord.series_break(); offset += 1; }
      while (coord.y.collection < reference.y.collection) { coord.collection_break(); offset += 1; }
      while (coord.y.volume < reference.y.volume) { coord.volume_break(); offset += 1; }
      while (coord.y.book < reference.y.book) { coord.book_break(); offset += 1; }
      while (coord.x.chapter < reference.x.chapter) { coord.chapter_break(); offset += 1; }
      while (coord.x.section < reference.x.section) { coord.section_break(); offset += 1; }
      while (coord.x.scroll < reference.x.scroll) { coord.scroll_break(); offset += 1; }
      output.push_back(PositionedScroll{coord, std::to_string(offset)});
      offset += phoken.scroll.size();
   }
   return output;
}
} // namespace detail

// a phext whose scrolls hold the decimal byte offset of each scroll
inline std::string index(std::string_view input) {
   return dephokenize(detail::index_phokens(input));
}

// the byte offset of the scroll at coord, or of the last scroll before it;
// 0 when no scroll comes at or before coord (never UB, RS 0.4.x fix)
inline std::size_t offset(std::string_view input, Coordinate coord) {
   const std::vector<PositionedScroll> output = detail::index_phokens(input);
   std::size_t result = 0;
   for (const PositionedScroll& phoken : output) {
      if (phoken.coord > coord) break;
      // RS: parse::<usize>().unwrap_or(result)
      std::size_t parsed = 0;
      bool valid = !phoken.scroll.empty();
      for (char ch : phoken.scroll) {
         if (ch < '0' || ch > '9') { valid = false; break; }
         parsed = parsed * 10 + static_cast<std::size_t>(ch - '0');
      }
      if (valid) result = parsed;
   }
   return result;
}

// -----------------------------------------------------------------------------------------------------------
// checksum + manifest (RS: XXH3-128 over the UTF-8 bytes, formatted as
// exactly 32 lowercase hex chars, zero-padded — cross-implementation by
// definition; do not substitute another hash)
// -----------------------------------------------------------------------------------------------------------
inline std::string checksum(std::string_view input) {
   const XXH128_hash_t hash = XXH3_128bits(input.data(), input.size());
   // RS formats the u128 (high64 << 64 | low64) as {:0>32} of {:x}; that is
   // exactly high and low each rendered as 16 zero-padded lowercase hex.
   static const char* digits = "0123456789abcdef";
   std::string out;
   out.reserve(32);
   for (int shift = 60; shift >= 0; shift -= 4) out.push_back(digits[(hash.high64 >> shift) & 0xF]);
   for (int shift = 60; shift >= 0; shift -= 4) out.push_back(digits[(hash.low64 >> shift) & 0xF]);
   return out;
}

// a phext whose every scroll content is replaced by that scroll's checksum
inline std::string manifest(std::string_view input) {
   std::vector<PositionedScroll> phokens = phokenize(input);
   for (PositionedScroll& phoken : phokens) {
      phoken.scroll = checksum(phoken.scroll);
   }
   return dephokenize(phokens);
}

} // namespace phext

// std::hash support for the coordinate types (RS derives Hash)
namespace std {
template <> struct hash<phext::ZCoordinate> {
   std::size_t operator()(const phext::ZCoordinate& c) const noexcept {
      std::size_t h = std::hash<std::uint64_t>{}(c.library);
      h ^= std::hash<std::uint64_t>{}(c.shelf) + 0x9e3779b97f4a7c15ULL + (h << 6) + (h >> 2);
      h ^= std::hash<std::uint64_t>{}(c.series) + 0x9e3779b97f4a7c15ULL + (h << 6) + (h >> 2);
      return h;
   }
};
template <> struct hash<phext::YCoordinate> {
   std::size_t operator()(const phext::YCoordinate& c) const noexcept {
      std::size_t h = std::hash<std::uint64_t>{}(c.collection);
      h ^= std::hash<std::uint64_t>{}(c.volume) + 0x9e3779b97f4a7c15ULL + (h << 6) + (h >> 2);
      h ^= std::hash<std::uint64_t>{}(c.book) + 0x9e3779b97f4a7c15ULL + (h << 6) + (h >> 2);
      return h;
   }
};
template <> struct hash<phext::XCoordinate> {
   std::size_t operator()(const phext::XCoordinate& c) const noexcept {
      std::size_t h = std::hash<std::uint64_t>{}(c.chapter);
      h ^= std::hash<std::uint64_t>{}(c.section) + 0x9e3779b97f4a7c15ULL + (h << 6) + (h >> 2);
      h ^= std::hash<std::uint64_t>{}(c.scroll) + 0x9e3779b97f4a7c15ULL + (h << 6) + (h >> 2);
      return h;
   }
};
template <> struct hash<phext::Coordinate> {
   std::size_t operator()(const phext::Coordinate& c) const noexcept {
      std::size_t h = std::hash<phext::ZCoordinate>{}(c.z);
      h ^= std::hash<phext::YCoordinate>{}(c.y) + 0x9e3779b97f4a7c15ULL + (h << 6) + (h >> 2);
      h ^= std::hash<phext::XCoordinate>{}(c.x) + 0x9e3779b97f4a7c15ULL + (h << 6) + (h >> 2);
      return h;
   }
};
} // namespace std

namespace phext {

// -----------------------------------------------------------------------------------------------------------
// binary scrolls (RS src/binary.rs, feature `binary` in RS — always available
// here because the base64 codec is hand-rolled in-header, no dependency)
//
// Canonical format: a binary scroll's content is exactly
//   phext-binary:1:base64\n<base64 body>
// header line + single 0x0A + RFC 4648 base64 (standard alphabet, padding
// required, no wrapping/whitespace). Empty payload = header line alone.
// Ingestion rule (canonical, all ports): non-UTF-8 bytes observed in an
// incoming scroll update are base64-encoded VERBATIM via the byte APIs —
// never rejected, never an error.
// -----------------------------------------------------------------------------------------------------------
inline constexpr std::string_view BINARY_SCROLL_HEADER = "phext-binary:1:base64";
namespace detail {
inline constexpr std::string_view BINARY_SCROLL_PREFIX = "phext-binary:1:base64\n";

inline bool starts_with(std::string_view text, std::string_view prefix) {
   return text.size() >= prefix.size() && text.substr(0, prefix.size()) == prefix;
}

// RFC 4648 base64, standard alphabet. Encoding always emits canonical
// padding. Decoding is strict, matching the base64 crate's STANDARD engine:
// length must be a multiple of 4, padding required and canonical (including
// zero trailing bits in the final symbol), no whitespace, no foreign bytes.
inline std::string base64_encode(const std::uint8_t* data, std::size_t len) {
   static const char* alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
   std::string out;
   out.reserve(((len + 2) / 3) * 4);
   for (std::size_t i = 0; i < len; i += 3) {
      const std::uint32_t b0 = data[i];
      const std::uint32_t b1 = (i + 1 < len) ? data[i + 1] : 0;
      const std::uint32_t b2 = (i + 2 < len) ? data[i + 2] : 0;
      out.push_back(alphabet[b0 >> 2]);
      out.push_back(alphabet[((b0 & 0x03) << 4) | (b1 >> 4)]);
      out.push_back((i + 1 < len) ? alphabet[((b1 & 0x0F) << 2) | (b2 >> 6)] : '=');
      out.push_back((i + 2 < len) ? alphabet[b2 & 0x3F] : '=');
   }
   return out;
}

inline int base64_value(char c) {
   if (c >= 'A' && c <= 'Z') return c - 'A';
   if (c >= 'a' && c <= 'z') return c - 'a' + 26;
   if (c >= '0' && c <= '9') return c - '0' + 52;
   if (c == '+') return 62;
   if (c == '/') return 63;
   return -1;
}

inline std::optional<std::vector<std::uint8_t>> base64_decode_strict(std::string_view body) {
   if (body.size() % 4 != 0) return std::nullopt;
   std::vector<std::uint8_t> out;
   out.reserve((body.size() / 4) * 3);
   for (std::size_t i = 0; i < body.size(); i += 4) {
      const bool last = (i + 4 == body.size());
      const char c0 = body[i], c1 = body[i + 1], c2 = body[i + 2], c3 = body[i + 3];
      const int v0 = base64_value(c0);
      const int v1 = base64_value(c1);
      if (v0 < 0 || v1 < 0) return std::nullopt;
      int v2 = -2, v3 = -2; // -2 = padding
      if (c2 != '=') v2 = base64_value(c2);
      if (c3 != '=') v3 = base64_value(c3);
      if (c2 == '=' || c3 == '=') {
         if (!last) return std::nullopt;                    // padding only in the final group
         if (c2 == '=' && c3 != '=') return std::nullopt;   // "xx=y" is not canonical
         if (v2 == -1 || v3 == -1) return std::nullopt;
         if (c2 == '=' && c3 == '=') {
            if (v1 & 0x0F) return std::nullopt;             // trailing bits must be zero
         } else {
            if (v2 >= 0 && (v2 & 0x03)) return std::nullopt; // trailing bits must be zero
         }
      } else {
         if (v2 < 0 || v3 < 0) return std::nullopt;
      }
      const std::uint32_t triple = (static_cast<std::uint32_t>(v0) << 18) |
                                   (static_cast<std::uint32_t>(v1) << 12) |
                                   (static_cast<std::uint32_t>(v2 < 0 ? 0 : v2) << 6) |
                                   static_cast<std::uint32_t>(v3 < 0 ? 0 : v3);
      out.push_back(static_cast<std::uint8_t>((triple >> 16) & 0xFF));
      if (c2 != '=') out.push_back(static_cast<std::uint8_t>((triple >> 8) & 0xFF));
      if (c3 != '=') out.push_back(static_cast<std::uint8_t>(triple & 0xFF));
   }
   return out;
}
} // namespace detail

// true when `scroll` carries the binary scroll header line (prefix test only)
inline bool is_binary_scroll(std::string_view scroll) {
   return detail::starts_with(scroll, detail::BINARY_SCROLL_PREFIX);
}

// encodes raw bytes as a binary scroll (header line + base64 body)
inline std::string encode_scroll(const std::uint8_t* payload, std::size_t len) {
   std::string out(detail::BINARY_SCROLL_PREFIX);
   out += detail::base64_encode(payload, len);
   return out;
}
inline std::string encode_scroll(const std::vector<std::uint8_t>& payload) {
   return encode_scroll(payload.data(), payload.size());
}

// decodes a binary scroll back to its raw bytes; nullopt when the scroll is
// not a binary scroll, or when its body is not strict base64 (RS Option)
inline std::optional<std::vector<std::uint8_t>> decode_scroll(std::string_view scroll) {
   if (!detail::starts_with(scroll, detail::BINARY_SCROLL_PREFIX)) return std::nullopt;
   return detail::base64_decode_strict(scroll.substr(detail::BINARY_SCROLL_PREFIX.size()));
}

// stores payload at location as a binary scroll, unconditionally
inline std::string insert_binary(std::string input, Coordinate location, const std::uint8_t* payload, std::size_t len) {
   return insert(std::move(input), location, encode_scroll(payload, len));
}
inline std::string insert_binary(std::string input, Coordinate location, const std::vector<std::uint8_t>& payload) {
   return insert_binary(std::move(input), location, payload.data(), payload.size());
}

inline std::string replace_binary(std::string_view input, Coordinate location, const std::uint8_t* payload, std::size_t len) {
   return replace(input, location, encode_scroll(payload, len));
}
inline std::string replace_binary(std::string_view input, Coordinate location, const std::vector<std::uint8_t>& payload) {
   return replace_binary(input, location, payload.data(), payload.size());
}

// retrieves the bytes held at target: a well-formed binary scroll decodes to
// its payload; any other scroll (plain text, malformed binary, absent)
// yields the scroll's own UTF-8 bytes (empty when unoccupied). Total.
inline std::vector<std::uint8_t> fetch_binary(std::string_view input, Coordinate target) {
   const std::string scroll = fetch(input, target);
   const std::optional<std::vector<std::uint8_t>> decoded = decode_scroll(scroll);
   if (decoded.has_value()) return *decoded;
   return std::vector<std::uint8_t>(scroll.begin(), scroll.end());
}

namespace detail {
// the stored form of an incoming scroll update, per the ingestion rule
inline std::string stored_form(const std::uint8_t* payload, std::size_t len) {
   if (is_valid_utf8(payload, len)) {
      if (len == 0) return "";
      return std::string(reinterpret_cast<const char*>(payload), len);
   }
   return encode_scroll(payload, len);
}
} // namespace detail

// byte-oriented insert under the ingestion rule (UTF-8 as text, non-UTF-8
// as a base64-verbatim binary scroll); never rejects non-UTF-8 data
inline std::string insert_bytes(std::string input, Coordinate location, const std::uint8_t* payload, std::size_t len) {
   return insert(std::move(input), location, detail::stored_form(payload, len));
}
inline std::string insert_bytes(std::string input, Coordinate location, const std::vector<std::uint8_t>& payload) {
   return insert_bytes(std::move(input), location, payload.data(), payload.size());
}

inline std::string replace_bytes(std::string_view input, Coordinate location, const std::uint8_t* payload, std::size_t len) {
   return replace(input, location, detail::stored_form(payload, len));
}
inline std::string replace_bytes(std::string_view input, Coordinate location, const std::vector<std::uint8_t>& payload) {
   return replace_bytes(input, location, payload.data(), payload.size());
}

// the byte-level twin of phokenize: identical segmentation, scroll bodies
// returned uninterpreted, valid UTF-8 or not
inline std::vector<std::pair<Coordinate, std::vector<std::uint8_t>>> phokenize_bytes(const std::uint8_t* input, std::size_t len) {
   std::vector<std::pair<Coordinate, std::vector<std::uint8_t>>> result;
   const std::size_t max = len;
   Coordinate coord = default_coordinate();
   std::size_t pi = 0;
   while (true) {
      Coordinate begin = coord;
      std::optional<std::size_t> text_start;
      std::size_t text_end = max;
      while (pi < max) {
         const std::uint8_t byte = input[pi];
         if (detail::is_dimension_break(byte)) {
            detail::apply_break(coord, byte);
            if (text_start.has_value()) {
               text_end = pi;
               pi += 1;
               break;
            }
         } else if (!text_start.has_value()) {
            text_start = pi;
            begin = coord;
         }
         pi += 1;
      }
      std::vector<std::uint8_t> scroll;
      if (text_start.has_value()) {
         scroll.assign(input + *text_start, input + text_end);
      }
      result.push_back(std::make_pair(begin, scroll));
      if (pi >= max) break;
   }
   return result;
}
inline std::vector<std::pair<Coordinate, std::vector<std::uint8_t>>> phokenize_bytes(const std::vector<std::uint8_t>& input) {
   return phokenize_bytes(input.data(), input.size());
}

// ingests an externally supplied phext byte stream: valid UTF-8 scrolls pass
// through verbatim; any scroll whose bytes are not valid UTF-8 is stored as
// a binary scroll holding the base64 of the exact observed bytes, so
// fetch_binary at its coordinate returns the original bytes exactly. Never
// rejects. The result is the canonical implode(explode) normal form.
inline std::string ingest(const std::uint8_t* bytes, std::size_t len) {
   const std::vector<std::pair<Coordinate, std::vector<std::uint8_t>>> parts = phokenize_bytes(bytes, len);
   std::map<Coordinate, std::string> map;
   for (const auto& part : parts) {
      const std::vector<std::uint8_t>& raw = part.second;
      if (detail::is_valid_utf8(raw.data(), raw.size())) {
         map[part.first] = std::string(raw.begin(), raw.end());
      } else {
         map[part.first] = encode_scroll(raw.data(), raw.size());
      }
   }
   return implode(map);
}
inline std::string ingest(const std::vector<std::uint8_t>& bytes) {
   return ingest(bytes.data(), bytes.size());
}

} // namespace phext

// -----------------------------------------------------------------------------------------------------------
// xz seal (RS src/seal.rs, feature `xz`) — opt-in: define PHEXT_CPP_WITH_XZ
// and link liblzma. Off by default, mirroring the RS feature flag.
//
// The seal is a standard xz stream over the phext's exact bytes; preset 6
// is an encoder choice, not a format requirement — any valid xz stream
// unseals. Compression is a seal, not a working format.
// -----------------------------------------------------------------------------------------------------------
#ifdef PHEXT_CPP_WITH_XZ
#include <lzma.h>
namespace phext {

inline constexpr std::uint32_t SEAL_PRESET = 6;

inline std::optional<std::vector<std::uint8_t>> seal_bytes(const std::uint8_t* input, std::size_t len) {
   lzma_stream stream = LZMA_STREAM_INIT;
   if (lzma_easy_encoder(&stream, SEAL_PRESET, LZMA_CHECK_CRC64) != LZMA_OK) {
      lzma_end(&stream);
      return std::nullopt;
   }
   std::vector<std::uint8_t> out;
   std::uint8_t buffer[8192];
   stream.next_in = input;
   stream.avail_in = len;
   lzma_ret ret = LZMA_OK;
   do {
      stream.next_out = buffer;
      stream.avail_out = sizeof(buffer);
      ret = lzma_code(&stream, stream.avail_in == 0 ? LZMA_FINISH : LZMA_RUN);
      out.insert(out.end(), buffer, buffer + (sizeof(buffer) - stream.avail_out));
   } while (ret == LZMA_OK);
   lzma_end(&stream);
   if (ret != LZMA_STREAM_END) return std::nullopt;
   return out;
}
inline std::optional<std::vector<std::uint8_t>> seal(std::string_view input) {
   return seal_bytes(reinterpret_cast<const std::uint8_t*>(input.data()), input.size());
}
inline std::optional<std::vector<std::uint8_t>> seal_bytes(const std::vector<std::uint8_t>& input) {
   return seal_bytes(input.data(), input.size());
}

inline std::optional<std::vector<std::uint8_t>> unseal(const std::uint8_t* data, std::size_t len) {
   lzma_stream stream = LZMA_STREAM_INIT;
   if (lzma_stream_decoder(&stream, UINT64_MAX, 0) != LZMA_OK) {
      lzma_end(&stream);
      return std::nullopt;
   }
   std::vector<std::uint8_t> out;
   std::uint8_t buffer[8192];
   stream.next_in = data;
   stream.avail_in = len;
   lzma_ret ret = LZMA_OK;
   do {
      stream.next_out = buffer;
      stream.avail_out = sizeof(buffer);
      ret = lzma_code(&stream, LZMA_RUN);
      out.insert(out.end(), buffer, buffer + (sizeof(buffer) - stream.avail_out));
      if (ret == LZMA_OK && stream.avail_in == 0 && stream.avail_out == sizeof(buffer)) {
         // no progress possible: truncated stream (lzma would spin otherwise)
         ret = LZMA_DATA_ERROR;
      }
   } while (ret == LZMA_OK);
   lzma_end(&stream);
   if (ret != LZMA_STREAM_END) return std::nullopt;
   return out;
}
inline std::optional<std::vector<std::uint8_t>> unseal(const std::vector<std::uint8_t>& data) {
   return unseal(data.data(), data.size());
}

// unseals directly to text; nullopt when the data is not a valid xz stream
// or the unsealed bytes are not valid UTF-8 (RS InvalidData case)
inline std::optional<std::string> unseal_string(const std::uint8_t* data, std::size_t len) {
   const std::optional<std::vector<std::uint8_t>> bytes = unseal(data, len);
   if (!bytes.has_value()) return std::nullopt;
   if (!detail::is_valid_utf8(bytes->data(), bytes->size())) return std::nullopt;
   return std::string(bytes->begin(), bytes->end());
}
inline std::optional<std::string> unseal_string(const std::vector<std::uint8_t>& data) {
   return unseal_string(data.data(), data.size());
}

} // namespace phext
#endif // PHEXT_CPP_WITH_XZ

#endif // PHEXT_H
