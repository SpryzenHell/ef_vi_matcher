#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
echo "EF_VI Zero-Copy Matcher"
echo "Project root: ${ROOT}"
echo
echo "The maintained build uses CMake:"
echo "  cmake -S . -B build -DCMAKE_BUILD_TYPE=Release"
echo "  cmake --build build --parallel"
echo "  ctest --test-dir build --output-on-failure"
echo
echo "The previous env.sh was for the inherited Liquibook/MPC build and is no longer used."
