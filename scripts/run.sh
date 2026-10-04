#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${BUILD_DIR:-build}"

cd "$ROOT"

command -v cmake >/dev/null 2>&1 || {
    echo "cmake is required" >&2
    exit 2
}

command -v c++ >/dev/null 2>&1 || {
    echo "a C++ compiler is required" >&2
    exit 2
}

cmake -S . -B "$BUILD_DIR" \
    -DCMAKE_BUILD_TYPE=Release \
    -DEFVI_ENABLE_DPDK=OFF \
    -DEFVI_BUILD_TESTS=ON \
    -DEFVI_BUILD_BENCHMARKS=ON \
    -DEFVI_BUILD_EXAMPLES=ON

cmake --build "$BUILD_DIR" --parallel
ctest --test-dir "$BUILD_DIR" --output-on-failure

"$BUILD_DIR/efvi_matcher"
"$BUILD_DIR/efvi_software_rx_demo"
"$BUILD_DIR/efvi_order_flow_demo"
"$BUILD_DIR/efvi_hugepage_probe"