# DPDK integration

The DPDK layer is optional so the deterministic matcher can still be built and tested on ordinary Linux machines.

DpdkPacketView keeps the rte_mbuf owner and the OrderRequest payload pointer together. The payload pointer refers directly to the mbuf data area.

The software loopback path writes the request once into the mbuf data area and then pushes only the descriptor pointer and sequence through the SPSC ring. A real RX path should parse the buffer returned by rte_eth_rx_burst.

CMake detects DPDK through pkg-config. When libdpdk is unavailable, the core matcher and normal benchmarks remain buildable.

With DPDK installed:

cmake -S . -B build-dpdk -DCMAKE_BUILD_TYPE=Release -DEFVI_ENABLE_DPDK=ON
cmake --build build-dpdk --parallel

The dpdk_nic_rx executable is a capability probe. NIC port/queue configuration remains host-specific.
