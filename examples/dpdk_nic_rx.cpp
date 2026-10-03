#include <iostream>

#ifdef EFVI_WITH_DPDK

#include <rte_eal.h>
#include <rte_errno.h>
#include <rte_ethdev.h>

int main(int argc, char** argv) {
    const int rc = rte_eal_init(argc, argv);
    if (rc < 0) {
        std::cerr << "EAL init failed: " << rte_strerror(rte_errno) << '
';
        return 2;
    }

    const auto ports = rte_eth_dev_count_avail();
    std::cout << "dpdk_ports=" << ports << '
';

    if (ports == 0) {
        rte_eal_cleanup();
        return 0;
    }

    std::cout
        << "A real NIC RX path should poll a configured port/queue with "
           "rte_eth_rx_burst(); this binary is a capability probe, not a "
           "fabricated NIC benchmark.\n";

    rte_eal_cleanup();
    return 0;
}

#else

int main() {
    std::cerr << "Built without DPDK
";
    return 2;
}

#endif
