#include "efvi/dpdk_adapter.hpp"
#include "efvi/order_book.hpp"

#include <chrono>
#include <cstdint>
#include <iostream>
#include <thread>

using Book = efvi::OrderBook<4096, 1u << 18, 1u << 19>;

int main(int argc, char** argv) {
    efvi::DpdkPacketPool pool;
    if (!pool.init(argc, argv, 8191, 2048)) {
        std::cerr << "DPDK EAL/mempool initialization failed\n";
        return 2;
    }

    efvi::DpdkLoopback io(pool);
    Book::Config cfg{};
    cfg.base_price = 9000;
    cfg.order_pool_pages = efvi::PageMode::Auto;
    Book book(cfg);

    std::uint64_t n = 500000;
    std::uint64_t submitted = 0, received = 0, trade_count = 0;

    const auto t0 = std::chrono::steady_clock::now();
    for (std::uint64_t i = 0; i < n; ++i) {
        efvi::OrderRequest req{};
        req.order_id = 10000000 + i;
        req.instrument = 1;
        req.quantity = 1;
        req.price = 10000;
        req.side = (i & 1u) ? efvi::Side::Buy : efvi::Side::Sell;
        req.tif = efvi::TimeInForce::ImmediateOrCancel;

        while (!io.submit(req, static_cast<std::uint32_t>(i)))
            std::this_thread::yield();
        ++submitted;

        efvi::DpdkPacketView view{};
        while (!io.receive(view))
            std::this_thread::yield();

        struct Sink {
            std::uint64_t& count;
            void operator()(const efvi::Trade&) noexcept { ++count; }
        } sink{trade_count};

        book.add(*view.order, sink);
        ++received;
        pool.release(view.mbuf);
    }

    const auto sec = std::chrono::duration<double>(
        std::chrono::steady_clock::now() - t0).count();

    std::cout << "submitted=" << submitted
              << " received=" << received
              << " trades=" << trade_count
              << " msgs_per_sec=" << received / sec << '
';

    pool.shutdown();
    return 0;
}
