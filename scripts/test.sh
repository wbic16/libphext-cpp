#!/usr/bin/env bash
# libphext-cpp test step:
#   1. build, then smoke-run the legacy binary on the two bundled documents
#      and check the loaded totals against the known-good values verified
#      during the 0.4.0 work:
#        hello-world.t -> 8 scrolls, 159 bytes
#        the-odyssey.t -> 52 scrolls, 706,366 bytes
#      The runs happen on copies in a temp dir: the binary can save in place
#      when given a command flag, and the fixtures must never be mutated.
#   2. build and run the phext.h conformance suites in tests/ — ports of
#      the libphext-rs v0.5.0 test corpus (test_lib.rs, test_phokenize.rs,
#      test_binary.rs, regressions.rs), the audit section-5 probes, and the
#      legacy regression tests. phext.h itself is exercised as C++17; the
#      legacy header keeps its C++20 target. The vendored RS fixture
#      tests/unit-test.phext is SHA-256-checked before use (it is gitignored
#      upstream in libphext-rs, so its hash is its provenance).
#   3. if liblzma development files are available, build and run the xz
#      seal suite (PHEXT_CPP_WITH_XZ); otherwise report the seal suite as
#      SKIPPED (the seal is opt-in, mirroring the RS `xz` feature).
set -euo pipefail
cd "$(dirname "$0")/.."
"$(dirname "$0")/build.sh"
TMP="$(mktemp -d)"
trap 'rm -rf "${TMP}"' EXIT
cp hello-world.t the-odyssey.t "${TMP}/"
FAIL=0
check() {
  local file="$1" want="$2"
  local got
  got="$("${PWD}/terse-string" "${TMP}/${file}" | head -n 1 || true)"
  echo "${file}: ${got}"
  if [ "${got}" = "Loaded Terse file with: ${want}" ]; then
    echo "  PASS (expected: Loaded Terse file with: ${want})"
  else
    echo "  FAIL (expected: Loaded Terse file with: ${want})"
    FAIL=1
  fi
}
check hello-world.t "8 scrolls and 159 bytes."
check the-odyssey.t "52 scrolls and 706366 bytes."
if [ "${FAIL}" != "0" ]; then
  echo "SMOKE TEST FAILED"
  exit 1
fi
echo "SMOKE TEST PASS"

CXX="${CXX:-g++}"
echo "== conformance suites (phext.h vs libphext-rs v0.5.0) =="
echo "4c1016783c8e9ec065244172929593e9595c6e6f1d266c317d12961b9ac49794  tests/unit-test.phext" | sha256sum -c - >/dev/null
echo "tests/unit-test.phext: SHA-256 OK"
for suite in test_phext test_phokenize test_binary; do
  "${CXX}" -std=c++17 -O1 -o "${TMP}/${suite}" "tests/${suite}.cpp"
  "${TMP}/${suite}"
done
"${CXX}" -std=c++20 -O1 -o "${TMP}/test_legacy" tests/test_legacy.cpp
"${TMP}/test_legacy"
if pkg-config --exists liblzma 2>/dev/null; then
  "${CXX}" -std=c++17 -O1 -DPHEXT_CPP_WITH_XZ $(pkg-config --cflags liblzma) \
    -o "${TMP}/test_seal" tests/test_seal.cpp $(pkg-config --libs liblzma)
  "${TMP}/test_seal"
else
  echo "SKIP test_seal: liblzma development files not found (xz seal is opt-in)"
fi
echo "ALL TESTS PASS"
