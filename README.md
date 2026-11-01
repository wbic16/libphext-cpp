# Terse String
The `trs::string` class provides a minimal wrapper for working with terse strings.

The sample program demonstrates how to read and write Terse files.

* You can display a nav tree by running `terse-string.exe <file>`.
* You can validate/parse an input file by running `terse-string.exe <file> <output>`.

## Example

A copy of the Odyssey has been included to get you started. It was cloned from Project Gutenberg.

`.\x64\Release\terse-string.exe .\the-odyssey.t`:

Loaded Terse file with: 52 scrolls.
 * Library #1
   * Shelf #1
     * Series #1
       * Collection #1
         * Volume #1
           * Book #1
             * Chapter #1
               * Section #1
                 * Scroll #1: The Project Gutenberg eBook of The Odyssey
                 * Scroll #2: Title: The Odyssey
             * Chapter #2
               * Section #1
                 * Scroll #1: The Odyssey
                 * Scroll #2: Contents
                 * Scroll #3: PREFACE TO FIRST EDITION
                 * Scroll #4: PREFACE TO SECOND EDITION
                 * Scroll #5: PREFACE TO THE TERSE EDITION
                 * Scroll #6: THE ODYSSEY
                 * Scroll #7: BOOK I
                 * Scroll #8: BOOK II
                 * Scroll #9: BOOK III
                 * Scroll #10: BOOK IV
                 * Scroll #11: BOOK V
                 * Scroll #12: BOOK VI
                 * Scroll #13: BOOK VII
                 * Scroll #14: BOOK VIII
                 * Scroll #15: BOOK IX
                 * Scroll #16: BOOK X
                 * Scroll #17: BOOK XI
                 * Scroll #18: BOOK XII
                 * Scroll #19: BOOK XIII
                 * Scroll #20: BOOK XIV
                 * Scroll #21: BOOK XV
                 * Scroll #22: BOOK XVI
                 * Scroll #23: BOOK XVII
                 * Scroll #24: BOOK XVIII
                 * Scroll #25: BOOK XIX
                 * Scroll #26: BOOK XX
                 * Scroll #27: BOOK XXI
                 * Scroll #28: BOOK XXII
                 * Scroll #29: BOOK XXIII
                 * Scroll #30: BOOK XXIV
                 * Scroll #31: FOOTNOTES #1-10
                 * Scroll #32: FOOTNOTES #11-20
                 * Scroll #33: FOOTNOTES #21-30
                 * Scroll #34: FOOTNOTES #31-40
                 * Scroll #35: FOOTNOTES #41-50
                 * Scroll #36: FOOTNOTES #51-60
                 * Scroll #37: FOOTNOTES #61-70
                 * Scroll #38: FOOTNOTES #71-80
                 * Scroll #39: FOOTNOTES #81-90
                 * Scroll #40: FOOTNOTES #91-100
                 * Scroll #41: FOOTNOTES #101-110
                 * Scroll #42: FOOTNOTES #111-120
                 * Scroll #43: FOOTNOTES #121-130
                 * Scroll #44: FOOTNOTES #131-140
                 * Scroll #45: FOOTNOTES #141-150
                 * Scroll #46: FOOTNOTES #151-160
                 * Scroll #47: FOOTNOTES #161-170
                 * Scroll #48: FOOTNOTES #171-180
                 * Scroll #49: FOOTNOTES #181-187
                 * Scroll #50: Copyright and License

## Development and Release

**Prerequisites (read first):**

1. Canonical build: **Windows with Visual Studio / MSBuild** (`terse-string.sln`).
2. Linux fallback: `g++` with C++20 support. The Linux build needs a workaround, stated plainly: `trs_string.h` uses `std::cerr` and `std::find` without including `<iostream>` / `<algorithm>`; MSVC supplies them transitively, g++ does not. The build script passes `-include iostream -include algorithm` to supply exactly those two headers — a workaround for a pre-existing gap, not a fix.

**Test:**

1. Run `scripts/test.sh`
2. That builds the binary (step below), then smoke-runs it on copies of `hello-world.t` (expected: 8 scrolls, 159 bytes) and `the-odyssey.t` (expected: 52 scrolls, 706,366 bytes) and prints PASS/FAIL against those known-good totals.

**Build:**

1. Windows: run `scripts/build.sh` — it prints the MSBuild route (`msbuild terse-string.sln /p:Configuration=Release`); or open the solution in Visual Studio and build Release.
2. Linux: run `scripts/build.sh` — it runs exactly:
   `g++ -std=c++20 -include iostream -include algorithm -o terse-string main.cpp`

**Publish:**

There is no package registry for this library. Distribution is a git tag on the GitHub repository, and the script never pushes anything itself.

1. Run `scripts/publish.sh` — it runs the smoke tests, prints exactly what will be published (git tag `v0.4.0`), and stops.
2. Only when that output looks right, run `PUBLISH_CONFIRM=1 scripts/publish.sh` — that creates the LOCAL tag `v0.4.0` and prints the push command.
3. A human then publishes the tag by running exactly: `git push origin v0.4.0`

## Coordinate components

Coordinate components (`trs::Coordinate`) are 32-bit and unbounded in practice. In earlier revisions they were `uint16_t`, which silently wrapped a component from 65,535 back to 0 on advance — a fixed-width cap in disguise; that cap is removed. (This repository carries no version marker, so no version number is stamped here.)

Dense graphs: phext is sparse by design. Coordinate components are unbounded, but a fully-populated dense graph contains the product of its dimensions' extents in scrolls — a dense block spanning 1000 in each of 9 dimensions implies 10^27 scrolls. Materializing, exploding, or indexing such a graph costs memory proportional to its populated scroll count. The old 999 cap never protected against this (999^9 was already unreachable); sparsity does. Keep graphs sparse.
