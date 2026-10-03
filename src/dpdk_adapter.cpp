#include "efvi/dpdk_adapter.hpp"

#ifdef EFVI_WITH_DPDK

#include <rte_eal.h>
#include <rte_errno.h>
#include <rte_mbuf.h>
#include <rte_mempool.h>

#include <cstring>

namespace efvi {

DpdkPacketPool::~DpdkPacketPool() { shutdown(); }

bool DpdkPacketPool::init(int argc, char** argv, unsigned count, unsigned data_room) {
    if (pool_) return true;

    const int rc = rte_eal_init(argc, argv);
    if (rc < 0) return false;

    eal_started_ = true;
    pool_ = rte_pktmbuf_pool_create(
        "efvi_mbuf_pool", count, 256, 0, data_room, rte_socket_id());

    if (!pool_) {
        shutdown();
        return false;
    }
    return true;
}

void DpdkPacketPool::shutdown() noexcept {
    if (pool_) rte_mempool_free(pool_);
    pool_ = nullptr;

    if (eal_started_) rte_eal_cleanup();
    eal_started_ = false;
}

rte_mbuf* DpdkPacketPool::acquire() noexcept {
    return pool_ ? rte_pktmbuf_alloc(pool_) : nullptr;
}

void DpdkPacketPool::release(rte_mbuf* mbuf) noexcept {
    if (mbuf) rte_pktmbuf_free(mbuf);
}

bool DpdkLoopback::submit(const OrderRequest& req, std::uint32_t sequence) noexcept {
    auto* mbuf = pool_.acquire();
    if (!mbuf) return false;

    void* dst = rte_pktmbuf_append(mbuf, sizeof(OrderRequest));
    if (!dst) {
        pool_.release(mbuf);
        return false;
    }

    std::memcpy(dst, &req, sizeof(req));

    if (!ring_.try_push(DpdkDescriptor{mbuf, sequence, 0, {}})) {
        pool_.release(mbuf);
        return false;
    }
    return true;
}

bool DpdkLoopback::receive(DpdkPacketView& view) noexcept {
    DpdkDescriptor descriptor{};
    if (!ring_.try_pop(descriptor)) return false;

    view.mbuf = descriptor.mbuf;
    view.sequence = descriptor.sequence;
    view.order = rte_pktmbuf_mtod(descriptor.mbuf, OrderRequest*);
    return true;
}

} // namespace efvi

#endif
