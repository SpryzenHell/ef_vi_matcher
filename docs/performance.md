# Performance methodology

The benchmark separates functional checks from machine-dependent measurements.

Functional coverage includes deterministic trade ordering, partial and multi-level fills, IOC/FOK, market IOC, cancel/replace, pool exhaustion and reuse, concurrent SPSC sequencing, and zero global C++ new calls during the measured matcher loop.

Latency is sampled with std::chrono steady_clock and reports p50 and p99. For serious low-latency claims, rerun on an isolated target CPU using cycle or invariant-TSC measurements and repeated trials.

Local development observation:
- 300,000 matcher operations
- about 18M operations per second
- p50 about 0.16 microseconds
- p99 about 1.87 microseconds
- 0 hot-path global new calls
- about 20M SPSC exchanges per second
- actual allocation page backing: 4096 bytes

These are local development observations, not universal performance guarantees.
