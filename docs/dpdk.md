# DPDK integration

DPDK is optional.

The default build does not require DPDK. When DPDK development files are found through pkg-config, the build adds the DPDK library, benchmark and NIC capability probe.

Configure:

    cmake -S . -B build-dpdk -DCMAKE_BUILD_TYPE=Release -DEFVI_ENABLE_DPDK=ON
    cmake --build build-dpdk --parallel

The DPDK adapter keeps the rte_mbuf owner together with the in-place OrderRequest pointer.

A real NIC run still depends on the host: device binding, driver, queue setup, NUMA placement, huge pages and port configuration are machine-specific.
