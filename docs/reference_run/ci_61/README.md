# CI reference run 61

This directory contains the raw output captured from GitHub Actions run 61 on 2026-10-04.

The run completed these stages successfully:

- Release configure and build
- Release CTest
- matcher smoke example
- software RX loopback
- order-flow example
- huge-page probe
- allocator benchmark
- strict 1 GiB huge-page check
- 50,000-operation benchmark
- ASan/UBSan build and tests

The values and terminal snapshots in this directory are the source material for the README figures.

The strict huge-page command intentionally returned exit code 2 because the runner did not have a 1 GiB HUGETLB page. This is a successful result for the strict-mode test: the program refused to fall back to normal pages.

The run used an Ubuntu 24.04 GitHub Actions runner with GCC 13.3.0. DPDK was not installed.
