#!/usr/bin/env bash
set -euo pipefail

BUILD_DIR="${BUILD_DIR:-build}"
N="${1:-1000000}"
MODE="${2:---normal}"

mkdir -p results
(
  cd results
  "../${BUILD_DIR}/efvi_benchmark" "${N}" "${MODE}"
  cp efvi_benchmark.csv "benchmark_${MODE#--}.csv"
)
