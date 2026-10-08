# Performance and experiments

The repository separates correctness tests from machine-dependent measurements.

The regular benchmark measures:
- matcher throughput;
- p50 and p99 operation latency;
- trade count and live orders;
- fixed-pool mapping size and page mode;
- global C++ new calls during the measured matching loop;
- SPSC producer/consumer throughput.

The experiment runner repeats the matcher and allocator benchmarks at several sizes, compares normal/auto/2 MiB/1 GiB page modes, and measures three SPSC ring sizes. It writes the raw CSV data under docs/reference_run/current/.

The numbers in the repository are measurements from the recorded Linux CI run. They are not hardware-independent guarantees. CPU frequency, scheduling, cache state, kernel configuration, compiler version and process placement can change the results.

A real TLB comparison requires the same workload on the target machine with normal pages and strict 1 GiB HUGETLB pages while recording processor-specific perf counters. The repository does not invent a TLB reduction number when that experiment has not been run.
