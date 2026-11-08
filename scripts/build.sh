#!/usr/bin/env bash
# libphext-cpp build step.
# Canonical build: Windows + MSVC (terse-string.sln / .vcxproj).
# On Linux this script compiles main.cpp with g++ -std=c++20.
# trs_string.h includes everything it uses since 0.5.0 (the old
# `-include iostream -include algorithm` workaround is gone). phext.h, the
# libphext-rs parity API, needs only C++17; the legacy demo stays C++20.
set -euo pipefail
cd "$(dirname "$0")/.."
case "$(uname -s)" in
  MINGW*|MSYS*|CYGWIN*|Windows*)
    echo "Windows detected. Canonical build: open terse-string.sln in Visual Studio, or run:"
    echo "  msbuild terse-string.sln /p:Configuration=Release"
    echo "(This script does not invoke MSBuild itself.)"
    exit 0
    ;;
esac
g++ -std=c++20 -o terse-string main.cpp
echo "Built ./terse-string"
