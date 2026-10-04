# EF_VI Zero-Copy Matcher

A Linux/x86-64-oriented deterministic limit-order-book matcher with an explicit low-latency memory path:

`DPDK RX/mempool (optional) -> fixed descriptor ring -> preallocated order pool -> price-time matcher -> trade sink`

The project targets the engineering ideas in the EF_VI resume entry without depending on a proprietary Solarflare/Xilinx NIC SDK. The DPDK layer models the important ownership pattern: RX buffers are fixed-capacity objects, descriptors carry pointers/metadata, and the matcher consumes the request directly from the buffer before returning the buffer to the pool.

## What is implemented

### 1. Deterministic matching core

`include/efvi/order_book.hpp` implements a single-instrument price ladder with:

- price-time priority using intrusive FIFO queues at each price level;
- limit, market-IOC, IOC and FOK orders;
- partial and multi-level fills;
- cancel and cancel/replace semantics;
- deterministic trade sequencing;
- bounded open-addressing order-ID lookup;
- top-of-book / depth snapshots;
- a preallocated order pool so the matching path does not call `malloc` after construction.

Liquibook is the behavioral reference for order properties, price-time matching, cancel/replace, FOK/IOC behavior and depth-level accounting.

### 2. O(1) allocator with optional 1 GiB HUGETLB backing

`include/efvi/fixed_pool.hpp` reserves the full object arena up front and stores an intrusive free list inside that arena. `allocate()` and `deallocate()` therefore only update pointers/counters after initialization.

The backing-page policy is explicit:

- `--normal`: 4 KiB/host page fallback;
- `--huge2m`: `MAP_HUGETLB | MAP_HUGE_2MB`;
- `--huge1g`: `MAP_HUGETLB | MAP_HUGE_1GB`;
- `Auto`: 1 GiB -> 2 MiB -> normal-page fallback.

The allocator reports the actual page size and whether the mapping is backed by hugetlb pages. Nothing is labeled as a huge-page result unless the OS actually supplied the mapping.

### 3. Cache-line-isolated SPSC ring

`include/efvi/spsc_ring.hpp` is a bounded single-producer/single-consumer queue. Producer and consumer cursors are separated with 64-byte alignment, and each ring slot occupies at least one 64-byte cache line. The implementation uses acquire/release atomics and never allocates after construction.

### 4. DPDK ingress adapter

`include/efvi/dpdk_adapter.hpp` and `src/dpdk_adapter.cpp` provide an optional DPDK path based on `rte_mempool` / `rte_mbuf`.

`DpdkPacketView` keeps the `rte_mbuf*` and the in-place `OrderRequest*` together so the application cannot lose ownership of the underlying RX buffer. The benchmark releases the exact same mbuf after matching.

This is a **software loopback / memory-semantics path**. It is not a claim that the test environment has a Solarflare NIC or that an EF_VI firmware datapath was exercised.

## Build

### Reference build

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

### ASan/UBSan

```bash
cmake -S . -B build-asan \
  -DCMAKE_BUILD_TYPE=Debug \
  -DEFVI_ENABLE_DPDK=OFF \
  -DEFVI_ENABLE_SANITIZERS=ON
cmake --build build-asan --parallel
ctest --test-dir build-asan --output-on-failure
```

### DPDK

Install DPDK development headers/pkg-config metadata on the target Linux host:

```bash
cmake -S . -B build-dpdk -DCMAKE_BUILD_TYPE=Release -DEFVI_ENABLE_DPDK=ON
cmake --build build-dpdk --parallel
```

When `libdpdk` is discoverable through pkg-config, CMake additionally builds `efvi_dpdk`, `efvi_dpdk_benchmark`, and `efvi_dpdk_nic_rx_probe`.

## Benchmarks

```bash
mkdir -p results
(cd results && ../build/efvi_benchmark 300000 --normal)
(cd results && ../build/efvi_allocator_benchmark 100000)
```

For a host with actual 1 GiB hugetlb pages:

```bash
(cd results && ../build/efvi_benchmark 1000000 --huge1g --strict)
```

The benchmark reports p50/p99 latency, throughput, SPSC exchange throughput, actual backing page size, hugetlb status, and a global C++ `new` counter for the measured matcher loop.

## TLB evidence

A TLB-miss reduction is a hardware-counter claim, not something to infer from source code. On the target CPU run both normal-page and strict 1 GiB-page benchmarks under processor-specific `perf stat` events from `perf list`. The repository intentionally does not publish a TLB-miss percentage until that experiment has actually been run.

## DPDK dataplane semantics

The DPDK flow is:

1. provision packet buffers with `rte_mempool`/`rte_mbuf`;
2. publish the mbuf pointer + sequence through the bounded descriptor ring;
3. obtain `OrderRequest*` directly from the mbuf data area;
4. match in-place;
5. return the same mbuf to the DPDK pool.

A production NIC path should substitute `rte_eth_rx_burst()` for the software submit/receive boundary while preserving the same ownership contract.

## Upstream components

The original project configuration names Liquibook, LightMatchingEngine and atomic_queue. Their roles are:

- Liquibook: matching/depth behavior;
- LightMatchingEngine: compact order/trade API and behavioral reference;
- atomic_queue: bounded lock-free queue patterns, cache-line contention avoidance and huge-page allocator techniques.

The repository already contains a pre-existing mechanically rewritten merge of those sources under `efviSrc/`, `lightmatchingengine/`, and `includes/atomic_queue/`. The maintained build path is the clean `include/efvi` + `src` implementation so the old rewrite cannot contaminate compilation.

See `THIRD_PARTY_NOTICES.md`.

## Verification note

The first local validation machine had GCC 14.2, no DPDK pkg-config package, and zero configured 1 GiB/2 MiB hugetlb pages. On that machine, the reference benchmark processed 300K matching operations at about 18M ops/s with p50 around 0.16 microseconds, p99 around 1.87 microseconds, zero measured global `new` calls in the matching loop, and roughly 20M SPSC exchanges/sec.

Those numbers are machine-specific development observations, not universal performance guarantees and not the original resume claims.
