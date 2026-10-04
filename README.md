# EF_VI Zero-Copy Matcher

<p align="center">
  <img src="main.png" alt="EF_VI Zero-Copy Matcher" width="920">
</p>

A C++17/Linux limit-order-book matcher built around a fixed memory model, a cache-line-isolated SPSC ring, and an optional DPDK ingress path.

The project is intended to make the data path easy to inspect and benchmark rather than hide it behind a large framework. The normal build has no external C++ dependencies beyond the compiler, CMake, and POSIX/Linux facilities.

## Current status

The software path is complete and runnable from a clean Linux checkout.

The repository includes two different dataplane levels:

1. A **software RX loopback** that uses the same in-place `OrderRequest` object before the matcher sees it. This can be run on any supported Linux machine and is the reference path for the zero-copy memory semantics.
2. An **optional DPDK adapter** using `rte_mbuf` and `rte_mempool`. This requires DPDK development packages. A real NIC path is host-specific and is not required for the matcher, tests, or reference benchmarks.

The code also supports explicit 2 MiB and 1 GiB Linux HUGETLB mappings. The benchmark reports which mapping was actually obtained. It does not label a run as a huge-page run when the host falls back to normal pages.

## What is in the repository

```text
include/efvi/
  types.hpp            request, trade, descriptor and book types
  spsc_ring.hpp        cache-line-isolated SPSC ring
  fixed_pool.hpp       fixed-capacity allocator and HUGETLB mapping
  order_book.hpp       deterministic price-time matcher
  transport.hpp        software RX buffer/descriptor path
  dpdk_adapter.hpp     optional DPDK interface

src/
  main.cpp             small matcher example
  hugepage_probe.cpp   Linux huge-page and data-layout probe
  dpdk_adapter.cpp     optional DPDK implementation

examples/
  order_flow_demo.cpp
  software_rx_loopback.cpp
  dpdk_nic_rx.cpp      optional DPDK capability probe

benchmarks/
  efvi_benchmark.cpp
  allocator_benchmark.cpp
  efvi_dpdk_benchmark.cpp

tests/
  efvi_tests.cpp

docs/
  architecture.md
  dpdk.md
  hugepages.md
  performance.md
  benchmark_300k_normal.txt
  allocator_200k.txt
  environment.txt
  images/

scripts/
  run.sh
  benchmark.sh
  hugepages_check.sh
  run_perf.sh
  bootstrap_ubuntu.sh

Dockerfile
CMakeLists.txt
```

## Quick start

The quickest path on Ubuntu or another Debian-based Linux machine is:

```bash
git clone https://github.com/SpryzenHell/ef_vi_matcher.git
cd ef_vi_matcher
./scripts/bootstrap_ubuntu.sh
./scripts/run.sh
```

`run.sh` configures the project with the portable software path, builds it, runs the test suite, and then executes the matcher, software RX loopback, order-flow example, and huge-page probe.

A working session ends with output similar to this:

```text
EF_VI Zero-Copy Matcher
  ask rested: true
  bid filled: true
  best ask:   10000 ticks
  ask qty:    40
  trades:     1
  pool used:  1
  pool bytes: 4194304

software RX loopback
  original_order_address: 0x...
  matcher_order_address:  0x...
  same_buffer:             true
  rested:                  true
  live_orders:             1
  rx_pending:              0
```

The addresses are process-specific and are expected to change between runs.

## Clean build without the helper script

Required tools:

| Requirement | Purpose |
|---|---|
| Linux | `mmap`, HUGETLB and DPDK support |
| C++17 compiler | Build the matcher |
| CMake 3.20+ | Configure the project |
| POSIX threads | SPSC benchmark and tests |

On Ubuntu:

```bash
sudo apt update
sudo apt install build-essential cmake pkg-config
```

Then:

```bash
cmake -S . -B build \
  -DCMAKE_BUILD_TYPE=Release \
  -DEFVI_ENABLE_DPDK=OFF \
  -DEFVI_BUILD_TESTS=ON \
  -DEFVI_BUILD_BENCHMARKS=ON \
  -DEFVI_BUILD_EXAMPLES=ON

cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

## Run the examples

### Matcher example

```bash
./build/efvi_matcher
```

It inserts one resting sell order and then submits a buy order against it. The expected final state is one trade and 40 units remaining at the ask.

### Order-flow example

```bash
./build/efvi_order_flow_demo
```

This demonstrates FIFO at a single price level and a partial fill across two resting orders.

### Software RX loopback

```bash
./build/efvi_software_rx_demo
```

The example is deliberately small. It writes an `OrderRequest` into a fixed RX buffer, publishes a descriptor containing the buffer address, consumes the descriptor, and passes the same object to the matcher. The important line is:

```text
same_buffer:             true
```

That is the software demonstration of the ownership/data-path contract. There is no second heap allocation for the request.

![Software RX loopback](docs/images/software_rx_demo.png)

## Tests

The test suite covers:

- price-time priority and partial fills;
- matching across multiple price levels;
- IOC and FOK handling;
- market IOC orders;
- cancel and cancel/replace;
- deterministic trade ordering;
- fixed-pool exhaustion and reuse;
- producer/consumer ordering in the SPSC ring;
- compile-time cache-line/size invariants.

Run it with:

```bash
ctest --test-dir build --output-on-failure
```

The project also has a sanitizer build:

```bash
cmake -S . -B build-asan \
  -DCMAKE_BUILD_TYPE=Debug \
  -DEFVI_ENABLE_DPDK=OFF \
  -DEFVI_ENABLE_SANITIZERS=ON

cmake --build build-asan --parallel
ctest --test-dir build-asan --output-on-failure
```

The repository's CI runs the same release and ASan/UBSan test stages.

![Test run](docs/images/tests_run.png)

## Performance benchmark

The reference benchmark measures two separate things:

1. matcher throughput and sampled p50/p99 latency;
2. SPSC producer/consumer exchange throughput.

Run:

```bash
./build/efvi_benchmark 300000 --normal
```

The benchmark also writes `efvi_benchmark.csv` in the current working directory.

The following numbers are from the local Linux run used for the repository documentation on 2026-10-04:

| Metric | Measured value |
|---|---:|
| Matcher operations | 300,000 |
| Matcher throughput | 20.66 M ops/s |
| Matcher p50 | 0.157 us |
| Matcher p99 | 1.648 us |
| SPSC exchange | 78.93 M items/s |
| Matching-loop global `new` calls | 0 |
| Allocator stride | 64 bytes |
| Benchmark pool mapping | 64 MiB |
| Actual page size | 4096 bytes |
| HUGETLB backing | false |

These measurements are a reference for the checked-in code, not a hardware-independent performance guarantee. CPU frequency, scheduling, compiler version, kernel configuration, cache state, and process placement all affect latency numbers.

![Matcher latency](docs/images/benchmark_latency.png)

![Measured throughput](docs/images/benchmark_throughput.png)

The raw command output is checked in as [`docs/benchmark_300k_normal.txt`](docs/benchmark_300k_normal.txt).

## Allocator benchmark

The allocator benchmark exercises the fixed pool directly:

```bash
./build/efvi_allocator_benchmark 200000
```

The local run used for the documentation produced:

```text
objects=200000 alloc_ops_per_sec=84416751.998 free_ops_per_sec=354869789.403 stride=64 mapped_bytes=12800000
```

Allocation and free operations are pointer manipulation on the pre-built intrusive free list. The mapping itself is created before the timed loop.

![Allocator benchmark](docs/images/allocator_run.png)

## Huge pages

The fixed pool supports four modes:

| Mode | Behaviour |
|---|---|
| `normal` | regular Linux pages / aligned heap fallback |
| `huge2m` | request 2 MiB HUGETLB pages |
| `huge1g` | request 1 GiB HUGETLB pages |
| `auto` | use the largest suitable HUGETLB mode available, then fall back |

Inspect the host:

```bash
./build/efvi_hugepage_probe
```

On the documentation host, the kernel reported zero reserved 1 GiB and 2 MiB hugetlb pages, so the allocator correctly used normal 4 KiB pages.

![Huge-page probe](docs/images/hugepage_probe.png)

Strict mode is useful when a benchmark must not silently fall back:

```bash
./build/efvi_benchmark 10000 --huge1g --strict
```

On a host without the required 1 GiB hugetlb page, the program exits with an explicit error rather than reporting a fake 1 GiB-page result.

![Strict huge-page check](docs/images/hugepage_strict_failure.png)

The project does not claim a TLB-miss reduction until a matched normal-page/1-GiB-page experiment has been collected with processor-specific `perf` counters.

See [docs/hugepages.md](docs/hugepages.md) and [docs/performance.md](docs/performance.md) for the measurement procedure.

## DPDK build

DPDK is optional. The default build is intentionally usable without it.

On Ubuntu 24.04, the distribution provides the `libdpdk-dev` development package. See the [Ubuntu package index](https://packages.ubuntu.com/libdpdk-dev).

Install it with:

```bash
sudo apt update
sudo apt install libdpdk-dev
```

Then configure the DPDK build:

```bash
cmake -S . -B build-dpdk \
  -DCMAKE_BUILD_TYPE=Release \
  -DEFVI_ENABLE_DPDK=ON

cmake --build build-dpdk --parallel
```

When CMake finds DPDK through `pkg-config`, it builds the optional `efvi_dpdk` library, DPDK benchmark, and NIC capability probe.

The DPDK code uses `rte_mempool`/`rte_mbuf` ownership and keeps the owner pointer beside the in-place request pointer. A real NIC receive loop should feed buffers from `rte_eth_rx_burst()` into the same ownership model.

The repository does **not** claim that a particular NIC was exercised by CI. NIC configuration, PCI binding, queues, NUMA placement and driver setup depend on the target machine.

See [docs/dpdk.md](docs/dpdk.md).

## Docker

For a clean reference build without installing a compiler on the host:

```bash
docker build -t efvi-zero-copy-matcher .
docker run --rm efvi-zero-copy-matcher
```

The Docker image runs the same software build and test path. DPDK and physical NIC access are intentionally outside the container's default path.

## Design notes

### Deterministic matching

The matcher uses a fixed price ladder. Each price level contains an intrusive FIFO of live orders. Matching starts at the best opposite-side price and consumes the oldest order at that level before moving to the next level.

A monotonic sequence number is assigned when an order is accepted. Trade events get their own monotonic sequence number. There is no background matching thread, timer, or worker pool in the reference implementation.

### Order lookup

Cancellations arrive by order ID, so the matcher keeps a fixed-capacity open-addressed lookup table alongside the price ladder. The table is created once and does not use `std::unordered_map` on the matching path.

### Fixed allocator

`FixedPool<T>` reserves the arena once and links free slots together inside the arena. After initialization, `allocate()` and `deallocate()` only manipulate pointers and a counter. This keeps the object allocation path bounded and avoids general-purpose allocator calls for new orders.

### Cache-line isolation

The SPSC ring aligns the ring object, producer cursor, consumer cursor and individual slots to 64-byte boundaries. Producer and consumer use acquire/release ordering and only one producer and one consumer are supported by design.

The alignment is an implementation choice for the target x86 cache-line assumption; it is not a claim that 64 bytes is universal for every architecture.

## Evidence and reproducibility

The repository keeps the raw benchmark outputs used for the README:

- [`docs/benchmark_300k_normal.txt`](docs/benchmark_300k_normal.txt)
- [`docs/allocator_200k.txt`](docs/allocator_200k.txt)
- [`docs/environment.txt`](docs/environment.txt)

The screenshots under `docs/images/` are generated from those actual command outputs, not from placeholder values.

![Application run](docs/images/application_run.png)

![Benchmark output](docs/images/benchmark_run.png)

![Local environment](docs/images/environment.png)

![Data layout](docs/images/data_layout.png)

## Local reference environment

```text
Linux 6.18.44 x86_64 GNU/Linux
c++ (Debian 14.2.0-19) 14.2.0
cmake version 3.31.6
DPDK: not installed
HugePages_Total: 0
HugePages_Free: 0
HugePages_Rsvd: 0
Hugepagesize: 2048 kB
Hugetlb: 0 kB
Intel Xeon Platinum 8573C
```

Exact environment output is kept in [docs/environment.txt](docs/environment.txt).

## Troubleshooting

### CMake cannot find a compiler

Install the base build toolchain:

```bash
sudo apt update
sudo apt install build-essential cmake pkg-config
```

### Huge-page mode exits with an error

That is expected in strict mode when the kernel has not reserved the requested HUGETLB page size. Check:

```bash
grep -E 'HugePages|Hugepagesize|Hugetlb' /proc/meminfo
./scripts/hugepages_check.sh
```

### DPDK targets do not appear

CMake only enables them when `libdpdk` is discoverable through `pkg-config`. Check:

```bash
pkg-config --modversion libdpdk
```

If it is missing, install the DPDK development package and configure again from a fresh `build-dpdk` directory.

### Running on Windows or macOS

The maintained project is Linux-only because the memory and dataplane work depends on Linux `mmap`/HUGETLB and optional DPDK support.

## Upstream components

The original project specification identifies these three repositories as inputs:

| Component | Repository | Used for |
|---|---|---|
| Liquibook | `objectcomputing/LiquiBook` / `enewhuis/liquibook` | order-book and matching behaviour |
| LightMatchingEngine | `gavincyi/LightMatchingEngine` | compact matching-engine examples and API ideas |
| atomic_queue | `max0x7ba/atomic_queue` | bounded queue, false-sharing and huge-page implementation ideas |

The maintained EFVI implementation is deliberately separated from the inherited source snapshot so that the active CMake build has one clear code path. See [`THIRD_PARTY_NOTICES.md`](THIRD_PARTY_NOTICES.md) before redistributing inherited source files.

## What is intentionally not claimed

This repository provides the implementation needed to run the software path, but some results depend on hardware that is not present on every machine:

- no Solarflare/Xilinx EF_VI NIC is assumed by the reference build;
- no DPDK NIC benchmark is reported unless DPDK and a configured NIC are actually available;
- no TLB-miss percentage is published without a real `perf` comparison;
- the reference benchmark numbers above are local measurements, not guarantees for a production trading system.

That separation is intentional. It keeps the source, benchmark output, and README consistent with what was actually executed.
