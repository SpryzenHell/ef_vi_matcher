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
#include <thread>
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
        make_order(21, 20, 1009, efvi::Side::Buy, efvi::TimeInForce::ImmediateOrCancel), s);
    assert(ioc.cancelled);
    assert(ioc.filled_quantity == 0);
    assert(b.best_bid() == 0);

    const auto fok = b.add(
        make_order(22, 20, 1010, efvi::Side::Buy, efvi::TimeInForce::FillOrKill), s);
    assert(fok.cancelled);
    assert(fok.filled_quantity == 0);
    assert(b.best_ask_qty() == 10);
}

static void test_cancel_and_replace() {
    Book::Config cfg{};
    cfg.base_price = 1000;
    cfg.order_pool_pages = efvi::PageMode::Normal;

    Book b(cfg);
    Sink s;
    b.add(make_order(30, 100, 1010, efvi::Side::Sell), s);
    assert(b.cancel(30));
    assert(!b.cancel(30));
    assert(b.best_ask() == 0);

    b.add(make_order(31, 100, 1010, efvi::Side::Sell), s);
    const auto r = b.replace(31, 60, 1010, s);
    assert(r.rested);
    assert(b.best_ask_qty() == 60);

    b.replace(31, 60, 1012, s);
    assert(b.best_ask() == 1012);
}

static void test_market_order() {
    Book::Config cfg{};
    cfg.base_price = 1000;
    cfg.order_pool_pages = efvi::PageMode::Normal;

    Book b(cfg);
    Sink s;
    b.add(make_order(40, 10, 1005, efvi::Side::Sell), s);

    const auto r = b.add(
        make_order(41, 10, efvi::kMarketPrice, efvi::Side::Buy, efvi::TimeInForce::ImmediateOrCancel), s);

    assert(r.fully_filled);
    assert(r.filled_quantity == 10);
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

static void test_pool_and_spsc() {
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

    efvi::SpscRing<efvi::RxDescriptor, 1024> q;
    constexpr std::uint32_t N = 200000;

    std::thread producer([&] {
        for (std::uint32_t i = 0; i < N;) {
            if (q.try_push(efvi::RxDescriptor{
                static_cast<std::uintptr_t>(0x1000u + i), 64, 0, i})) {
                ++i;
            } else {
                std::this_thread::yield();
            }
        }
    });

    efvi::RxDescriptor d{};
    for (std::uint32_t i = 0; i < N; ++i) {
        while (!q.try_pop(d)) std::this_thread::yield();
        assert(d.sequence == i);
    }

    producer.join();
    assert(q.empty());

    static_assert(alignof(decltype(q)) >= 64);
    static_assert(sizeof(efvi::OrderRequest) == 64);
}

int main() {
    test_price_time_and_partial_fill();
    test_cross_levels();
    test_ioc_and_fok();
    test_cancel_and_replace();
    test_market_order();
    test_determinism();
    test_pool_and_spsc();
    std::cout << "all EFVI correctness tests passed\n";
    return 0;
}