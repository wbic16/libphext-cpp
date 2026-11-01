#!/usr/bin/env bash
# libphext-cpp build step.
# Canonical build: Windows + MSVC (terse-string.sln / .vcxproj).
# On Linux this script compiles main.cpp with g++ -std=c++20.
# WHY the -include flags: trs_string.h uses std::cerr and std::find without
# including <iostream>/<algorithm>; MSVC supplies them transitively, g++
# does not. The two -include flags supply exactly those headers and nothing
# else -- they work around a pre-existing gap, they do not fix it.
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
g++ -std=c++20 -include iostream -include algorithm -o terse-string main.cpp
echo "Built ./terse-string"
