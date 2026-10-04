#!/usr/bin/env bash
set -euo pipefail

if ! command -v apt-get >/dev/null 2>&1; then
    echo "This helper is for Debian/Ubuntu systems." >&2
    exit 2
fi

if [[ "${EUID}" -eq 0 ]]; then
    APT=(apt-get)
else
    APT=(sudo apt-get)
fi

"${APT[@]}" update
"${APT[@]}" install -y build-essential cmake pkg-config

if [[ "${WITH_DPDK:-0}" == "1" ]]; then
    "${APT[@]}" install -y libdpdk-dev
fi

echo
echo "Base toolchain installed."
echo "Run: ./scripts/run.sh"

if [[ "${WITH_DPDK:-0}" == "1" ]]; then
    echo
    echo "DPDK development files installed."
    echo "Configure the DPDK build with:"
    echo "  cmake -S . -B build-dpdk -DCMAKE_BUILD_TYPE=Release -DEFVI_ENABLE_DPDK=ON"
    echo "  cmake --build build-dpdk --parallel"
fi