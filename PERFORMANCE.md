# Performance

This file replaces the old benchmark table with measurements produced by the maintained EF_VI build.

## Method

- Fixed-capacity matcher state is constructed before measurement.
- Latency is sampled with `std::chrono::steady_clock`.
- The benchmark has a global C++ `new` counter around the measured matching loop.
- SPSC throughput is measured independently from matcher latency.
- Huge-page mode is reported from the actual mapping.
- TLB claims require target-CPU performance counters.

## Local reference observation

| Metric | Local observation |
|---|---:|
| Matcher operations | 300,000 |
| Matcher throughput | ~18M ops/s |
| p50 | ~0.16 us |
| p99 | ~1.87 us |
| Matching-loop `new` calls | 0 |
| SPSC exchange | ~20M items/s |
| Huge pages available | No |
| DPDK available | No |

These values depend on CPU, kernel, build flags, scheduling and workload. They should be regenerated on the deployment host before being used in a resume or technical report.

## Original upstream numbers

Numbers shown in the historical upstream README/performance files are kept as provenance, but they are not relabeled as measurements of this revamp.
