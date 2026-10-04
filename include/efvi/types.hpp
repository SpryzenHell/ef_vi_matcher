#pragma once
#include <cstdint>
#include <limits>
#include <type_traits>
namespace efvi {
enum class Side : std::uint8_t { Buy = 0, Sell = 1 };
enum class TimeInForce : std::uint8_t { GoodTilCancel = 0, ImmediateOrCancel = 1, FillOrKill = 2 };
enum class OrderStatus : std::uint8_t { Accepted, PartiallyFilled, Filled, Cancelled, Rejected };
using OrderId = std::uint64_t; using Sequence = std::uint64_t; using Quantity = std::uint32_t; using Price = std::int64_t;
inline constexpr Price kMarketPrice = 0;
inline constexpr std::uint32_t kNoOrder = std::numeric_limits<std::uint32_t>::max();
struct alignas(64) OrderRequest final {
    OrderId order_id{0}; std::uint64_t ingress_timestamp_ns{0}; std::uint32_t instrument{0}; Quantity quantity{0}; Price price{0};
    Side side{Side::Buy}; TimeInForce tif{TimeInForce::GoodTilCancel}; std::uint16_t flags{0}; std::uint32_t checksum{0};
    std::uint8_t reserved[24]{};
};
static_assert(std::is_trivially_copyable_v<OrderRequest>); static_assert(sizeof(OrderRequest) == 64);
struct Trade final { OrderId aggressor_id{0}; OrderId resting_id{0}; Price price{0}; Quantity quantity{0}; Sequence sequence{0}; };
struct MatchStats final { Quantity filled_quantity{0}; std::uint32_t trade_count{0}; bool fully_filled{false}; bool rested{false}; bool cancelled{false}; };
struct BookLevel final { Price price{0}; Quantity quantity{0}; std::uint32_t order_count{0}; };
struct RxDescriptor final { std::uintptr_t buffer{0}; std::uint16_t length{0}; std::uint16_t flags{0}; std::uint32_t sequence{0}; };
static_assert(std::is_trivially_copyable_v<RxDescriptor>);
} // namespace efvi
