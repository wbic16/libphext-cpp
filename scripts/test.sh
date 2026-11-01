#!/usr/bin/env bash
# libphext-cpp test step: build, then smoke-run the binary on the two
# bundled documents and check the loaded totals against the known-good
# values verified during the 0.4.0 work:
#   hello-world.t -> 8 scrolls, 159 bytes
#   the-odyssey.t -> 52 scrolls, 706366 bytes
# The runs happen on copies in a temp dir: the binary can save in place
# when given a command flag, and the fixtures must never be mutated.
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
