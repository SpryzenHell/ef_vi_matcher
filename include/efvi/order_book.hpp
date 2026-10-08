#pragma once

#include "efvi/fixed_pool.hpp"
#include "efvi/types.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <stdexcept>
#include <utility>

namespace efvi {

template <std::size_t MaxLevels = 4096,
          std::size_t MaxOrders = (1u << 20),
          std::size_t MaxOrderIndex = (1u << 21)>
class OrderBook final {
    struct alignas(64) OrderNode final {
        OrderId id{0};
        Sequence sequence{0};
        Quantity quantity{0};
        Quantity remaining{0};
        Price price{0};
        std::uint32_t instrument{0};
        std::uint32_t level{0};
        std::uint32_t prev{kNoOrder};
        std::uint32_t next{kNoOrder};
        Side side{Side::Buy};
        TimeInForce tif{TimeInForce::GoodTilCancel};
        bool live{false};
    };
    static_assert(sizeof(OrderNode) == 64, "OrderNode should occupy exactly one cache line");

    struct alignas(64) Level final {
        Price price{0};
        Quantity total{0};
        std::uint32_t head{kNoOrder};
        std::uint32_t tail{kNoOrder};
        std::uint32_t order_count{0};
        bool active{false};
    };
    static_assert(sizeof(Level) == 64, "Level should occupy exactly one cache line");

    struct IndexEntry final {
        OrderId key{0};
        std::uint32_t slot{kNoOrder};
        std::uint8_t state{0};
    };

    static constexpr std::size_t kIndexMask = MaxOrderIndex - 1;
    static_assert(MaxOrderIndex > 0 && (MaxOrderIndex & kIndexMask) == 0,
                  "MaxOrderIndex must be a non-zero power of two");
    static_assert(MaxLevels >= 2);

public:
    struct Config final {
        Price base_price{0};
        Price tick_size{1};
        PageMode order_pool_pages{PageMode::Auto};
        bool strict_huge_pages{false};
    };

    explicit OrderBook(Config cfg = {})
        : cfg_(checked_config(cfg)),
          order_pool_(MaxOrders, cfg_.order_pool_pages, cfg_.strict_huge_pages),
          index_(std::make_unique<IndexEntry[]>(MaxOrderIndex)) {
        for (std::size_t i = 0; i < MaxLevels; ++i) {
            levels_[i].price =
                cfg_.base_price + static_cast<Price>(i) * cfg_.tick_size;
        }
    }

    OrderBook(const OrderBook&) = delete;
    OrderBook& operator=(const OrderBook&) = delete;

    template <typename TradeSink>
    MatchStats add(const OrderRequest& req, TradeSink&& sink) {
        return add_impl(req, req.quantity, std::forward<TradeSink>(sink));
    }

    template <typename TradeSink>
    MatchStats add_impl(const OrderRequest& req,
                        Quantity initial_remaining,
                        TradeSink&& sink) {
        validate_request(req);

        if (initial_remaining == 0 || initial_remaining > req.quantity) {
            throw std::invalid_argument("invalid initial remaining quantity");
        }

        if (req.tif == TimeInForce::FillOrKill ||
            req.tif == TimeInForce::ImmediateOrCancel) {
            const auto available =
                available_crossing(req.side, req.price, initial_remaining);

            if (req.tif == TimeInForce::FillOrKill &&
                available < initial_remaining)
                return MatchStats{0, 0, false, false, true};

            if (req.tif == TimeInForce::ImmediateOrCancel && available == 0)
                return MatchStats{0, 0, false, false, true};
        }

        OrderNode* node = allocate_order(req, initial_remaining);
        if (!node) return MatchStats{0, 0, false, false, true};

        MatchStats out{};
        const auto incoming_slot = slot_of(node);

        while (node->remaining > 0) {
            const std::uint32_t passive_slot =
                best_crossing_slot(node->side, node->price);
            if (passive_slot == kNoOrder) break;

            auto& level = levels_[passive_slot];
            while (node->remaining > 0 && level.head != kNoOrder) {
                const auto passive_slot_idx = level.head;
                auto& passive = *node_at(passive_slot_idx);
                const Quantity qty = std::min(node->remaining, passive.remaining);

                node->remaining -= qty;
                passive.remaining -= qty;
                level.total -= qty;
                out.filled_quantity += qty;
                ++out.trade_count;
                sink(Trade{node->id, passive.id, level.price, qty,
                            ++last_trade_sequence_});

                if (passive.remaining == 0) {
                    unlink_from_level(passive_slot_idx);
                    erase_index(passive.id);
                    order_pool_.destroy(&passive);
                }
            }

            if (level.total == 0) {
                level.active = false;
                update_best_after_empty(node->side, passive_slot);
            }
        }

        if (node->remaining == 0) {
            out.fully_filled = true;
            erase_index(node->id);
            order_pool_.destroy(node);
        } else if (node->tif == TimeInForce::ImmediateOrCancel ||
                   node->tif == TimeInForce::FillOrKill) {
            out.cancelled = true;
            erase_index(node->id);
            order_pool_.destroy(node);
        } else {
            rest_order(incoming_slot);
            out.rested = true;
        }

        return out;
    }

    bool cancel(OrderId id) noexcept {
        const auto slot = find_index(id);
        if (slot == kNoOrder) return false;

        auto* node = node_at(slot);
        unlink_from_level(slot);
        erase_index(id);
        order_pool_.destroy(node);
        return true;
    }

    template <typename TradeSink>
    MatchStats replace(OrderId id,
                       Quantity new_total_quantity,
                       Price new_price,
                       TradeSink&& sink) {
        const auto slot = find_index(id);
        if (slot == kNoOrder) return {};

        auto* node = node_at(slot);
        const auto old = *node;
        const Quantity executed = old.quantity - old.remaining;

        if (new_total_quantity == 0) {
            throw std::invalid_argument("replacement quantity must be positive");
        }
        if (new_total_quantity < executed) {
            throw std::invalid_argument(
                "replacement quantity cannot be below already executed quantity");
        }
        if (new_price < 0) {
            throw std::invalid_argument("replacement price cannot be negative");
        }
        if (new_price == kMarketPrice &&
            old.tif == TimeInForce::GoodTilCancel) {
            throw std::invalid_argument("market replacement must be IOC or FOK");
        }
        if (new_price != kMarketPrice && level_for_price(new_price) < 0) {
            throw std::invalid_argument(
                "replacement price is outside the configured price ladder");
        }

        const bool size_reduction_only =
            new_price == old.price && new_total_quantity < old.quantity;

        if (size_reduction_only) {
            const Quantity delta = old.quantity - new_total_quantity;
            auto& level = levels_[old.level];
            level.total -= delta;
            node->quantity = new_total_quantity;
            node->remaining -= delta;

            if (node->remaining == 0) {
                cancel(id);
                return MatchStats{0, 0, false, false, true};
            }

            return MatchStats{0, 0, false, true, false};
        }

        if (new_price == old.price &&
            new_total_quantity == old.quantity) {
            return MatchStats{0, 0, false, true, false};
        }

        if (new_total_quantity == executed) {
            cancel(id);
            return MatchStats{0, 0, false, false, true};
        }

        cancel(id);

        OrderRequest req{};
        req.order_id = id;
        req.instrument = old.instrument;
        req.quantity = new_total_quantity;
        req.price = new_price;
        req.side = old.side;
        req.tif = old.tif;
        return add_impl(
            req,
            static_cast<Quantity>(new_total_quantity - executed),
            std::forward<TradeSink>(sink));
    }

    [[nodiscard]] Price best_bid() const noexcept {
        return best_bid_level_ == kNoOrder ? 0 : levels_[best_bid_level_].price;
    }

    [[nodiscard]] Price best_ask() const noexcept {
        return best_ask_level_ == kNoOrder ? 0 : levels_[best_ask_level_].price;
    }

    [[nodiscard]] Quantity best_bid_qty() const noexcept {
        return best_bid_level_ == kNoOrder ? 0 : levels_[best_bid_level_].total;
    }

    [[nodiscard]] Quantity best_ask_qty() const noexcept {
        return best_ask_level_ == kNoOrder ? 0 : levels_[best_ask_level_].total;
    }

    [[nodiscard]] std::size_t live_orders() const noexcept { return order_pool_.used(); }
    [[nodiscard]] std::size_t pool_used() const noexcept { return order_pool_.used(); }
    [[nodiscard]] std::size_t pool_capacity() const noexcept { return order_pool_.capacity(); }
    [[nodiscard]] std::size_t pool_mapped_bytes() const noexcept { return order_pool_.mapped_bytes(); }
    [[nodiscard]] std::size_t pool_page_size() const noexcept { return order_pool_.page_size(); }
    [[nodiscard]] bool pool_hugepage_backed() const noexcept {
        return order_pool_.hugepage_backed();
    }

    [[nodiscard]] std::array<BookLevel, 10> top_levels() const noexcept {
        std::array<BookLevel, 10> result{};
        std::size_t n = 0;

        auto bid = best_bid_level_;
        while (bid != kNoOrder && n < 5) {
            const auto& l = levels_[bid];
            if (l.active) result[n++] = {l.price, l.total, l.order_count};
            if (bid == 0) break;
            bid = previous_active(bid);
        }

        std::size_t a = 5;
        auto ask = best_ask_level_;
        while (ask != kNoOrder && a < 10) {
            const auto& l = levels_[ask];
            if (l.active) result[a++] = {l.price, l.total, l.order_count};
            ask = next_active(ask);
        }

        return result;
    }

private:
    static Config checked_config(Config cfg) {
        if (cfg.tick_size <= 0) {
            throw std::invalid_argument("tick_size must be positive");
        }
        if (cfg.base_price < 0) {
            throw std::invalid_argument("base_price must be non-negative");
        }

        const auto max_index = static_cast<std::uint64_t>(MaxLevels - 1);
        const auto tick = static_cast<std::uint64_t>(cfg.tick_size);
        const auto base = static_cast<std::uint64_t>(cfg.base_price);
        const auto max_price =
            static_cast<std::uint64_t>(std::numeric_limits<Price>::max());

        if (tick != 0 && max_index > (max_price - base) / tick) {
            throw std::invalid_argument(
                "price ladder exceeds the Price type range");
        }

        return cfg;
    }

    static std::uint64_t mix64(std::uint64_t x) noexcept {
        x ^= x >> 30;
        x *= 0xbf58476d1ce4e5b9ULL;
        x ^= x >> 27;
        x *= 0x94d049bb133111ebULL;
        x ^= x >> 31;
        return x;
    }

    void validate_request(const OrderRequest& req) const {
        if (req.order_id == 0) {
            throw std::invalid_argument("order_id must be non-zero");
        }
        if (req.quantity == 0) {
            throw std::invalid_argument("quantity must be positive");
        }
        if (req.price < 0) {
            throw std::invalid_argument("price cannot be negative");
        }
        if (req.price == kMarketPrice &&
            req.tif == TimeInForce::GoodTilCancel) {
            throw std::invalid_argument("market order must be IOC or FOK");
        }
        if (find_index(req.order_id) != kNoOrder) {
            throw std::invalid_argument("duplicate order_id");
        }
        if (req.price != kMarketPrice && level_for_price(req.price) < 0) {
            throw std::invalid_argument(
                "price is outside the configured price ladder");
        }
    }

    [[nodiscard]] int level_for_price(Price price) const noexcept {
        if (price < cfg_.base_price) return -1;

        const Price delta = price - cfg_.base_price;
        if (delta % cfg_.tick_size != 0) return -1;

        const auto idx = static_cast<std::size_t>(delta / cfg_.tick_size);
        return idx < MaxLevels ? static_cast<int>(idx) : -1;
    }

    OrderNode* allocate_order(const OrderRequest& req,
                              Quantity initial_remaining) {
        auto* node = order_pool_.create();
        if (!node) return nullptr;

        const auto slot = slot_of(node);
        *node = {};
        node->id = req.order_id;
        node->sequence = ++last_sequence_;
        node->quantity = req.quantity;
        node->remaining = initial_remaining;
        node->price = req.price;
        node->instrument = req.instrument;
        node->side = req.side;
        node->tif = req.tif;
        node->live = true;

        if (!insert_index(node->id, slot)) {
            order_pool_.destroy(node);
            return nullptr;
        }

        return node;
    }

    [[nodiscard]] std::uint32_t slot_of(const OrderNode* p) const noexcept {
        const auto base = reinterpret_cast<std::uintptr_t>(order_pool_.base());
        const auto addr = reinterpret_cast<std::uintptr_t>(p);
        return static_cast<std::uint32_t>(
            (addr - base) / FixedPool<OrderNode>::stride());
    }

    [[nodiscard]] OrderNode* node_at(std::uint32_t slot) noexcept {
        auto* base =
            static_cast<std::byte*>(const_cast<void*>(order_pool_.base()));
        return reinterpret_cast<OrderNode*>(
            base + static_cast<std::size_t>(slot) *
                       FixedPool<OrderNode>::stride());
    }

    [[nodiscard]] const OrderNode* node_at(std::uint32_t slot) const noexcept {
        auto* base = static_cast<const std::byte*>(order_pool_.base());
        return reinterpret_cast<const OrderNode*>(
            base + static_cast<std::size_t>(slot) *
                       FixedPool<OrderNode>::stride());
    }

    void rest_order(std::uint32_t slot) noexcept {
        auto& o = *node_at(slot);
        const auto level_idx =
            static_cast<std::uint32_t>(level_for_price(o.price));
        o.level = level_idx;

        auto& l = levels_[level_idx];
        o.prev = l.tail;
        o.next = kNoOrder;

        if (l.tail != kNoOrder) {
            node_at(l.tail)->next = slot;
        } else {
            l.head = slot;
        }

        l.tail = slot;
        l.total += o.remaining;
        ++l.order_count;
        l.active = true;

        if (o.side == Side::Buy) {
            if (best_bid_level_ == kNoOrder || level_idx > best_bid_level_)
                best_bid_level_ = level_idx;
        } else {
            if (best_ask_level_ == kNoOrder || level_idx < best_ask_level_)
                best_ask_level_ = level_idx;
        }
    }

    void unlink_from_level(std::uint32_t slot) noexcept {
        auto& o = *node_at(slot);
        auto& l = levels_[o.level];

        if (o.prev != kNoOrder) {
            node_at(o.prev)->next = o.next;
        } else {
            l.head = o.next;
        }

        if (o.next != kNoOrder) {
            node_at(o.next)->prev = o.prev;
        } else {
            l.tail = o.prev;
        }

        if (l.total >= o.remaining) {
            l.total -= o.remaining;
        } else {
            l.total = 0;
        }

        if (l.order_count > 0) --l.order_count;

        if (l.head == kNoOrder) {
            l.active = false;
            if (o.side == Side::Buy && best_bid_level_ == o.level)
                best_bid_level_ = previous_active(o.level);
            if (o.side == Side::Sell && best_ask_level_ == o.level)
                best_ask_level_ = next_active(o.level);
        }

        o.prev = o.next = kNoOrder;
    }

    void update_best_after_empty(Side aggressor,
                                 std::uint32_t level) noexcept {
        if (aggressor == Side::Buy) {
            if (best_ask_level_ == level)
                best_ask_level_ = next_active(level);
        } else {
            if (best_bid_level_ == level)
                best_bid_level_ = previous_active(level);
        }
    }

    [[nodiscard]] std::uint32_t best_crossing_slot(Side side,
                                                    Price price) const noexcept {
        if (side == Side::Buy) {
            const auto a = best_ask_level_;
            if (a == kNoOrder) return kNoOrder;
            if (price == kMarketPrice || levels_[a].price <= price) return a;
        } else {
            const auto b = best_bid_level_;
            if (b == kNoOrder) return kNoOrder;
            if (price == kMarketPrice || levels_[b].price >= price) return b;
        }

        return kNoOrder;
    }

    Quantity available_crossing(Side side,
                                Price price,
                                Quantity cap) const noexcept {
        std::uint64_t total = 0;

        if (side == Side::Buy) {
            for (auto idx = best_ask_level_;
                 idx != kNoOrder && total < cap;
                 idx = next_active(idx)) {
                const auto p = levels_[idx].price;
                if (price != kMarketPrice && p > price) break;
                total += levels_[idx].total;
            }
        } else {
            for (auto idx = best_bid_level_;
                 idx != kNoOrder && total < cap;
                 idx = previous_active(idx)) {
                const auto p = levels_[idx].price;
                if (price != kMarketPrice && p < price) break;
                total += levels_[idx].total;
            }
        }

        return static_cast<Quantity>(std::min<std::uint64_t>(total, cap));
    }

    [[nodiscard]] std::uint32_t next_active(std::uint32_t from) const noexcept {
        for (std::uint32_t i = from + 1; i < MaxLevels; ++i)
            if (levels_[i].active) return i;
        return kNoOrder;
    }

    [[nodiscard]] std::uint32_t previous_active(std::uint32_t from) const noexcept {
        if (from == 0 || from == kNoOrder) return kNoOrder;
        for (std::uint32_t i = from; i-- > 0;)
            if (levels_[i].active) return i;
        return kNoOrder;
    }

    bool insert_index(OrderId key, std::uint32_t slot) noexcept {
        auto idx = mix64(key) & kIndexMask;
        std::size_t first_tombstone = MaxOrderIndex;

        for (std::size_t probe = 0; probe < MaxOrderIndex; ++probe) {
            auto& e = index_[idx];

            if (e.state == 0) {
                const auto use =
                    first_tombstone == MaxOrderIndex ? idx : first_tombstone;
                index_[use] = {key, slot, 1};
                return true;
            }

            if (e.state == 2 && first_tombstone == MaxOrderIndex)
                first_tombstone = idx;

            if (e.state == 1 && e.key == key) return false;
            idx = (idx + 1) & kIndexMask;
        }

        if (first_tombstone != MaxOrderIndex) {
            index_[first_tombstone] = {key, slot, 1};
            return true;
        }

        return false;
    }

    void erase_index(OrderId key) noexcept {
        auto idx = mix64(key) & kIndexMask;

        for (std::size_t probe = 0; probe < MaxOrderIndex; ++probe) {
            auto& e = index_[idx];
            if (e.state == 0) return;

            if (e.state == 1 && e.key == key) {
                e.state = 2;
                e.slot = kNoOrder;
                return;
            }

            idx = (idx + 1) & kIndexMask;
        }
    }

    [[nodiscard]] std::uint32_t find_index(OrderId key) const noexcept {
        auto idx = mix64(key) & kIndexMask;

        for (std::size_t probe = 0; probe < MaxOrderIndex; ++probe) {
            const auto& e = index_[idx];
            if (e.state == 0) return kNoOrder;
            if (e.state == 1 && e.key == key) return e.slot;
            idx = (idx + 1) & kIndexMask;
        }

        return kNoOrder;
    }

    Config cfg_{};
    FixedPool<OrderNode> order_pool_;
    std::array<Level, MaxLevels> levels_{};
    std::unique_ptr<IndexEntry[]> index_;
    std::uint32_t best_bid_level_{kNoOrder};
    std::uint32_t best_ask_level_{kNoOrder};
    Sequence last_sequence_{0};
    Sequence last_trade_sequence_{0};
};

} // namespace efvi
