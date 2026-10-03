#pragma once
#include <array>
#include <atomic>
#include <cstddef>
#include <type_traits>
namespace efvi {
template <typename T, std::size_t Capacity>
class alignas(64) SpscRing final {
    static_assert(Capacity >= 2 && (Capacity & (Capacity - 1)) == 0, "Capacity must be a power of two");
    static_assert(std::is_trivially_copyable_v<T>, "SpscRing requires trivially-copyable items");
    struct alignas(64) Slot final { T value{}; };
public:
    static constexpr std::size_t kCapacity = Capacity;
    [[nodiscard]] bool try_push(const T& value) noexcept {
        const auto head=head_.load(std::memory_order_relaxed); const auto tail=tail_.load(std::memory_order_acquire);
        if(head-tail==Capacity) return false; slots_[head&(Capacity-1)].value=value; head_.store(head+1,std::memory_order_release); return true;
    }
    [[nodiscard]] bool try_pop(T& value) noexcept {
        const auto tail=tail_.load(std::memory_order_relaxed); const auto head=head_.load(std::memory_order_acquire);
        if(tail==head) return false; value=slots_[tail&(Capacity-1)].value; tail_.store(tail+1,std::memory_order_release); return true;
    }
    [[nodiscard]] std::size_t approx_size() const noexcept {
        return static_cast<std::size_t>(head_.load(std::memory_order_acquire)-tail_.load(std::memory_order_acquire));
    }
    [[nodiscard]] bool empty() const noexcept { return approx_size()==0; }
    [[nodiscard]] bool full() const noexcept { return approx_size()==Capacity; }
    static_assert(alignof(Slot)>=64);
private:
    std::array<Slot,Capacity> slots_{};
    alignas(64) std::atomic<std::size_t> head_{0};
    alignas(64) std::atomic<std::size_t> tail_{0};
};
} // namespace efvi
