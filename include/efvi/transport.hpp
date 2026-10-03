#pragma once

#include "efvi/spsc_ring.hpp"
#include "efvi/types.hpp"

#include <cstddef>
#include <cstdint>

namespace efvi {

using DescriptorRing = SpscRing<RxDescriptor, 1u << 16>;

struct alignas(64) PacketBuffer final {
    OrderRequest order{};
};
static_assert(sizeof(PacketBuffer) == 64);

class SoftwareRxPath final {
public:
    explicit SoftwareRxPath(PacketBuffer* buffers, std::size_t count)
        : buffers_(buffers), count_(count) {}

    [[nodiscard]] bool publish(std::uint32_t buffer_index,
                               std::uint32_t sequence) noexcept {
        if (buffer_index >= count_) return false;
        return descriptors_.try_push(RxDescriptor{
            reinterpret_cast<std::uintptr_t>(&buffers_[buffer_index]),
            static_cast<std::uint16_t>(sizeof(OrderRequest)),
            0u,
            sequence
        });
    }

    [[nodiscard]] bool consume(OrderRequest*& order) noexcept {
        RxDescriptor d{};
        if (!descriptors_.try_pop(d)) return false;
        auto* buffer = reinterpret_cast<PacketBuffer*>(d.buffer);
        order = &buffer->order;
        return true;
    }

    [[nodiscard]] std::size_t pending() const noexcept {
        return descriptors_.approx_size();
    }

private:
    PacketBuffer* buffers_{nullptr};
    std::size_t count_{0};
    DescriptorRing descriptors_{};
};

} // namespace efvi
