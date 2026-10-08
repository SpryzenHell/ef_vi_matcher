#ifdef NDEBUG
#undef NDEBUG
#endif

#include "efvi/fixed_pool.hpp"
#include "efvi/order_book.hpp"
#include "efvi/spsc_ring.hpp"
#include "efvi/transport.hpp"

#include <cassert>
#include <cstdint>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <thread>
#include <type_traits>
#include <vector>

using Book = efvi::OrderBook<256, 1024, 2048>;

struct Sink {
    std::uint32_t n{0};
    std::vector<efvi::Trade> trades;

    void operator()(const efvi::Trade& t) noexcept {
        assert(t.quantity > 0);
        ++n;
        trades.push_back(t);
    }
};

static efvi::OrderRequest make_order(
    efvi::OrderId id,
    efvi::Quantity qty,
    efvi::Price price,
    efvi::Side side,
    efvi::TimeInForce tif = efvi::TimeInForce::GoodTilCancel) {
    efvi::OrderRequest r{};
    r.order_id = id;
    r.quantity = qty;
    r.price = price;
    r.side = side;
    r.tif = tif;
    r.instrument = 7;
    return r;
}

static void test_price_time_and_partial_fill() {
    Book::Config cfg{};
    cfg.base_price = 1000;
    cfg.tick_size = 1;
    cfg.order_pool_pages = efvi::PageMode::Normal;

    Book b(cfg);
    Sink s;
    b.add(make_order(1, 100, 1010, efvi::Side::Sell), s);
    b.add(make_order(2, 50, 1010, efvi::Side::Sell), s);
    b.add(make_order(3, 75, 1010, efvi::Side::Buy), s);

    assert(s.n == 1);
    assert(s.trades[0].resting_id == 1);
    assert(s.trades[0].quantity == 75);
    assert(b.best_ask() == 1010);
    assert(b.best_ask_qty() == 75);
}

static void test_cross_levels() {
    Book::Config cfg{};
    cfg.base_price = 1000;
    cfg.order_pool_pages = efvi::PageMode::Normal;

    Book b(cfg);
    Sink s;
    b.add(make_order(10, 40, 1008, efvi::Side::Sell), s);
    b.add(make_order(11, 40, 1010, efvi::Side::Sell), s);

    const auto st = b.add(make_order(12, 70, 1010, efvi::Side::Buy), s);
    assert(st.filled_quantity == 70);
    assert(st.trade_count == 2);
    assert(b.best_ask() == 1010);
    assert(b.best_ask_qty() == 10);
}

static void test_ioc_and_fok() {
    Book::Config cfg{};
    cfg.base_price = 1000;
    cfg.order_pool_pages = efvi::PageMode::Normal;

    Book b(cfg);
    Sink s;
    b.add(make_order(20, 10, 1010, efvi::Side::Sell), s);

    const auto ioc = b.add(
        make_order(21, 20, 1009, efvi::Side::Buy,
                   efvi::TimeInForce::ImmediateOrCancel), s);
    assert(ioc.cancelled);
    assert(ioc.filled_quantity == 0);
    assert(b.best_bid() == 0);

    const auto fok = b.add(
        make_order(22, 20, 1010, efvi::Side::Buy,
                   efvi::TimeInForce::FillOrKill), s);
    assert(fok.cancelled);
    assert(fok.filled_quantity == 0);
    assert(b.best_ask_qty() == 10);
}

static void test_market_order() {
    Book::Config cfg{};
    cfg.base_price = 1000;
    cfg.order_pool_pages = efvi::PageMode::Normal;

    Book b(cfg);
    Sink s;
    b.add(make_order(40, 10, 1005, efvi::Side::Sell), s);

    const auto r = b.add(
        make_order(41, 10, efvi::kMarketPrice, efvi::Side::Buy,
                   efvi::TimeInForce::ImmediateOrCancel), s);

    assert(r.fully_filled);
    assert(r.filled_quantity == 10);
    assert(b.best_ask() == 0);
}

static void test_cancel_replace_priority() {
    Book::Config cfg{};
    cfg.base_price = 1000;
    cfg.order_pool_pages = efvi::PageMode::Normal;

    Book b(cfg);
    Sink s;
    b.add(make_order(30, 100, 1010, efvi::Side::Sell), s);
    b.add(make_order(31, 100, 1010, efvi::Side::Sell), s);

    const auto r = b.replace(30, 90, 1010, s);
    assert(r.rested);
    assert(b.best_ask_qty() == 190);

    const auto old_priority = b.add(make_order(32, 150, 1010, efvi::Side::Buy), s);
    assert(old_priority.trade_count == 2);
    assert(s.trades[0].resting_id == 30);
    assert(s.trades[1].resting_id == 31);

    b.add(make_order(33, 100, 1010, efvi::Side::Sell), s);
    const auto repl = b.replace(33, 100, 1011, s);
    assert(repl.rested);
    const auto buy = b.add(make_order(34, 100, 1011, efvi::Side::Buy), s);
    assert(buy.filled_quantity == 100);
    assert(s.trades.back().resting_id == 33);
}

static void test_partial_fill_replace_bounds() {
    Book::Config cfg{};
    cfg.base_price = 1000;
    cfg.order_pool_pages = efvi::PageMode::Normal;

    Book b(cfg);
    Sink s;

    b.add(make_order(60, 100, 1010, efvi::Side::Sell), s);
    b.add(make_order(61, 30, 1010, efvi::Side::Buy), s);

    bool threw = false;
    try {
        (void)b.replace(60, 20, 1010, s);
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    assert(threw);
    assert(b.best_ask_qty() == 70);

    const auto r = b.replace(60, 70, 1010, s);
    assert(r.rested);
    assert(b.best_ask_qty() == 40);

    const auto c = b.replace(60, 30, 1010, s);
    assert(c.cancelled);
    assert(b.best_ask() == 0);
}

static void test_determinism() {
    Book::Config cfg{};
    cfg.base_price = 1000;
    cfg.order_pool_pages = efvi::PageMode::Normal;

    Book a(cfg), b(cfg);
    Sink sa, sb;

    const auto orders = std::vector<efvi::OrderRequest>{
        make_order(50, 15, 1010, efvi::Side::Sell),
        make_order(51, 10, 1011, efvi::Side::Sell),
        make_order(52, 20, 1011, efvi::Side::Buy),
        make_order(53, 5, 1010, efvi::Side::Buy)
    };

    for (const auto& r : orders) {
        a.add(r, sa);
        b.add(r, sb);
    }

    assert(sa.trades.size() == sb.trades.size());
    for (std::size_t i = 0; i < sa.trades.size(); ++i) {
        assert(sa.trades[i].aggressor_id == sb.trades[i].aggressor_id);
        assert(sa.trades[i].resting_id == sb.trades[i].resting_id);
        assert(sa.trades[i].price == sb.trades[i].price);
        assert(sa.trades[i].quantity == sb.trades[i].quantity);
        assert(sa.trades[i].sequence == sb.trades[i].sequence);
    }

    assert(a.best_bid() == b.best_bid());
    assert(a.best_ask() == b.best_ask());
}

struct alignas(128) WideObject {
    std::uint64_t value{0};
};

static void test_pool_edges_and_alignment() {
    efvi::FixedPool<efvi::OrderRequest> pool(64, efvi::PageMode::Normal);
    std::vector<efvi::OrderRequest*> ptrs;
    ptrs.reserve(64);

    for (int i = 0; i < 64; ++i) {
        auto* x = pool.create();
        assert(x != nullptr);
        ptrs.push_back(x);
    }
    assert(pool.used() == 64);
    assert(pool.free_count() == 0);
    assert(pool.create() == nullptr);

    for (auto* x : ptrs) pool.destroy(x);
    assert(pool.used() == 0);
    assert(pool.free_count() == 64);

    efvi::FixedPool<WideObject> wide(2, efvi::PageMode::Normal);
    auto* w0 = wide.create();
    auto* w1 = wide.create();
    assert(w0 != nullptr && w1 != nullptr);
    assert(reinterpret_cast<std::uintptr_t>(w0) % alignof(WideObject) == 0);
    assert(reinterpret_cast<std::uintptr_t>(w1) % alignof(WideObject) == 0);
    wide.destroy(w1);
    wide.destroy(w0);

    bool overflow = false;
    try {
        (void)efvi::round_up(std::numeric_limits<std::size_t>::max(), 2);
    } catch (const std::overflow_error&) {
        overflow = true;
    }
    assert(overflow);
}

static void test_order_book_validation_and_capacity() {
    bool bad_tick = false;
    try {
        Book::Config cfg{};
        cfg.tick_size = 0;
        Book b(cfg);
        (void)b;
    } catch (const std::invalid_argument&) {
        bad_tick = true;
    }
    assert(bad_tick);

    bool bad_base = false;
    try {
        Book::Config cfg{};
        cfg.base_price = -1;
        Book b(cfg);
        (void)b;
    } catch (const std::invalid_argument&) {
        bad_base = true;
    }
    assert(bad_base);

    bool bad_ladder = false;
    try {
        using Tiny = efvi::OrderBook<4, 8, 16>;
        Tiny::Config cfg{};
        cfg.base_price = std::numeric_limits<efvi::Price>::max() - 1;
        cfg.tick_size = 2;
        Tiny b(cfg);
        (void)b;
    } catch (const std::invalid_argument&) {
        bad_ladder = true;
    }
    assert(bad_ladder);

    Book::Config validation_cfg{};
    validation_cfg.base_price = 1000;
    validation_cfg.order_pool_pages = efvi::PageMode::Normal;
    Book b(validation_cfg);
    Sink s;

    bool zero_id = false;
    try {
        b.add(make_order(0, 1, 1000, efvi::Side::Buy), s);
    } catch (const std::invalid_argument&) {
        zero_id = true;
    }
    assert(zero_id);

    bool zero_qty = false;
    try {
        b.add(make_order(70, 0, 1000, efvi::Side::Buy), s);
    } catch (const std::invalid_argument&) {
        zero_qty = true;
    }
    assert(zero_qty);

    bool bad_market = false;
    try {
        b.add(make_order(71, 1, efvi::kMarketPrice, efvi::Side::Buy), s);
    } catch (const std::invalid_argument&) {
        bad_market = true;
    }
    assert(bad_market);

    bool bad_price = false;
    try {
        b.add(make_order(72, 1, 999, efvi::Side::Buy), s);
    } catch (const std::invalid_argument&) {
        bad_price = true;
    }
    assert(bad_price);

    b.add(make_order(73, 1, 1000, efvi::Side::Buy), s);
    bool duplicate = false;
    try {
        b.add(make_order(73, 1, 1000, efvi::Side::Buy), s);
    } catch (const std::invalid_argument&) {
        duplicate = true;
    }
    assert(duplicate);

    using SmallBook = efvi::OrderBook<8, 4, 8>;
    SmallBook::Config small_cfg{};
    small_cfg.base_price = 1000;
    small_cfg.order_pool_pages = efvi::PageMode::Normal;
    SmallBook small(small_cfg);
    for (std::uint64_t id = 1; id <= 4; ++id) {
        const auto r =
            small.add(make_order(id, 1, 1000, efvi::Side::Buy), s);
        assert(r.rested);
    }
    const auto rejected =
        small.add(make_order(5, 1, 1000, efvi::Side::Buy), s);
    assert(rejected.cancelled);
    assert(small.live_orders() == 4);
}

static void test_index_tombstones_and_reuse() {
    Book::Config cfg{};
    cfg.base_price = 1000;
    cfg.order_pool_pages = efvi::PageMode::Normal;

    Book b(cfg);
    Sink s;

    for (std::uint64_t i = 100; i < 180; ++i) {
        b.add(make_order(i, 1, 1000, efvi::Side::Buy), s);
    }

    for (std::uint64_t i = 100; i < 180; i += 2) {
        assert(b.cancel(i));
    }

    for (std::uint64_t i = 1000; i < 1040; ++i) {
        const auto r = b.add(make_order(i, 1, 1000, efvi::Side::Buy), s);
        assert(r.rested);
    }

    for (std::uint64_t i = 1010; i < 1040; ++i) {
        assert(b.cancel(i));
    }

    for (std::uint64_t i = 2000; i < 2030; ++i) {
        const auto r = b.add(make_order(i, 1, 1000, efvi::Side::Buy), s);
        assert(r.rested);
    }
}

static void test_spsc() {
    efvi::SpscRing<efvi::RxDescriptor, 8> q;

    for (std::uint32_t i = 0; i < 8; ++i) {
        assert(q.try_push(efvi::RxDescriptor{
            static_cast<std::uintptr_t>(0x1000u + i), 64, 0, i}));
    }
    assert(!q.try_push(efvi::RxDescriptor{0, 64, 0, 8}));
    assert(q.full());

    efvi::RxDescriptor d{};
    for (std::uint32_t i = 0; i < 8; ++i) {
        assert(q.try_pop(d));
        assert(d.sequence == i);
    }
    assert(!q.try_pop(d));
    assert(q.empty());

    efvi::SpscRing<efvi::RxDescriptor, 1024> q2;
    constexpr std::uint32_t N = 300000;

    std::thread producer([&] {
        for (std::uint32_t i = 0; i < N;) {
            if (q2.try_push(efvi::RxDescriptor{
                    static_cast<std::uintptr_t>(0x1000u + i), 64, 0, i})) {
                ++i;
            } else {
                std::this_thread::yield();
            }
        }
    });

    for (std::uint32_t i = 0; i < N; ++i) {
        while (!q2.try_pop(d)) std::this_thread::yield();
        assert(d.sequence == i);
    }

    producer.join();
    assert(q2.empty());
    static_assert(alignof(decltype(q2)) >= 64);
    static_assert(alignof(efvi::OrderRequest) >= 64);
    static_assert(sizeof(efvi::OrderRequest) == 64);
    static_assert(std::is_trivially_copyable_v<efvi::OrderRequest>);
}

int main() {
    test_price_time_and_partial_fill();
    std::cout << "PASS price_time_partial\n";
    test_cross_levels();
    std::cout << "PASS cross_levels\n";
    test_ioc_and_fok();
    std::cout << "PASS ioc_fok\n";
    test_market_order();
    std::cout << "PASS market_order\n";
    test_cancel_replace_priority();
    std::cout << "PASS cancel_replace_priority\n";
    test_partial_fill_replace_bounds();
    std::cout << "PASS partial_replace_bounds\n";
    test_determinism();
    std::cout << "PASS deterministic_trades\n";
    test_pool_edges_and_alignment();
    std::cout << "PASS pool_exhaustion_alignment\n";
    test_order_book_validation_and_capacity();
    std::cout << "PASS validation_capacity\n";
    test_index_tombstones_and_reuse();
    std::cout << "PASS index_tombstone_reuse\n";
    test_spsc();
    std::cout << "PASS spsc_ordering_full_empty\n";
    std::cout << "all EFVI correctness tests passed\n";
    return 0;
}
