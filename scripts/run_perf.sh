#!/usr/bin/env bash
set -euo pipefail

BUILD_DIR="${BUILD_DIR:-build}"
N="${1:-1000000}"

if ! command -v perf >/dev/null 2>&1; then
  echo "perf is not installed; install linux-tools on the target host." >&2
  exit 2
fi

echo "Processor-specific TLB events:"
perf list 2>/dev/null | grep -Ei 'dTLB|TLB-load|TLB miss' | head -40 || true

echo
echo "Normal-page reference:"
"${BUILD_DIR}/efvi_benchmark" "${N}" --normal

echo
echo "Run --huge1g --strict with the same workload and compare the processor-specific dTLB counters."
