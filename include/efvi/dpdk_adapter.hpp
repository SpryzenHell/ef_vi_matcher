#pragma once

#include "efvi/spsc_ring.hpp"
#include "efvi/types.hpp"

#include <cstddef>
#include <cstdint>

namespace efvi {

#ifdef EFVI_WITH_DPDK

struct rte_mbuf;
struct rte_mempool;

class DpdkPacketPool final {
public:
    DpdkPacketPool() = default;
    ~DpdkPacketPool();

    DpdkPacketPool(const DpdkPacketPool&) = delete;
    DpdkPacketPool& operator=(const DpdkPacketPool&) = delete;

    bool init(int argc, char** argv, unsigned count = 8191,
              unsigned data_room = 2048);
    void shutdown() noexcept;

    [[nodiscard]] rte_mbuf* acquire() noexcept;
    void release(rte_mbuf* mbuf) noexcept;
    [[nodiscard]] bool ready() const noexcept { return pool_ != nullptr; }
    [[nodiscard]] rte_mempool* raw_pool() const noexcept { return pool_; }

private:
    rte_mempool* pool_{nullptr};
    bool eal_started_{false};
};

struct alignas(64) DpdkDescriptor final {
    rte_mbuf* mbuf{nullptr};
    std::uint32_t sequence{0};
    std::uint32_t reserved{0};
    std::uint8_t padding[48]{};
};
static_assert(sizeof(DpdkDescriptor) == 64);

struct DpdkPacketView final {
    rte_mbuf* mbuf{nullptr};
    OrderRequest* order{nullptr};
    std::uint32_t sequence{0};
};

class DpdkLoopback final {
public:
    explicit DpdkLoopback(DpdkPacketPool& pool) : pool_(pool) {}

    bool submit(const OrderRequest& req, std::uint32_t sequence) noexcept;
    bool receive(DpdkPacketView& view) noexcept;

    [[nodiscard]] std::size_t pending() const noexcept {
        return ring_.approx_size();
    }

private:
    DpdkPacketPool& pool_;
    SpscRing<DpdkDescriptor, 1u << 14> ring_{};
};

#else

class DpdkPacketPool final {
public:
    bool init(int, char**, unsigned = 8191, unsigned = 2048) { return false; }
    void shutdown() noexcept {}
    [[nodiscard]] bool ready() const noexcept { return false; }
};

struct DpdkPacketView final {};

class DpdkLoopback final {
public:
    explicit DpdkLoopback(DpdkPacketPool&) {}
    bool submit(const OrderRequest&, std::uint32_t) noexcept { return false; }
    bool receive(DpdkPacketView&) noexcept { return false; }
    [[nodiscard]] std::size_t pending() const noexcept { return 0; }
};

#endif

} // namespace efvi
