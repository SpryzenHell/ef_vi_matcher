#include "efvi/order_book.hpp"
#include "efvi/spsc_ring.hpp"
#include "efvi/transport.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <new>
#include <string>
#include <thread>
#include <vector>

namespace {
std::atomic<bool> g_track_allocations{false};
std::atomic<std::uint64_t> g_hotpath_allocations{0};

void* allocate_or_throw(std::size_t n) {
    if (void* p = std::malloc(n)) {
        if (g_track_allocations.load(std::memory_order_relaxed)) ++g_hotpath_allocations;
        return p;
    }
    throw std::bad_alloc();
}
}
void* operator new(std::size_t n) { return allocate_or_throw(n); }
void* operator new[](std::size_t n) { return allocate_or_throw(n); }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
void operator delete[](void* p, std::size_t) noexcept { std::free(p); }
void* operator new(std::size_t n, const std::nothrow_t&) noexcept { return std::malloc(n); }
void* operator new[](std::size_t n, const std::nothrow_t&) noexcept { return std::malloc(n); }
void operator delete(void* p, const std::nothrow_t&) noexcept { std::free(p); }
void operator delete[](void* p, const std::nothrow_t&) noexcept { std::free(p); }

namespace {
using Clock = std::chrono::steady_clock;
using Book = efvi::OrderBook<4096, 1u << 20, 1u << 21>;

struct LatencySink {
    std::uint64_t trades{0};
    void operator()(const efvi::Trade&) noexcept { ++trades; }
};

template <typename Fn>
std::vector<double> sample_latency(std::size_t n, Fn&& fn) {
    std::vector<double> us;
    us.reserve(n);
    for (std::size_t i = 0; i < n; ++i) {
        const auto t0 = Clock::now();
        fn(i);
        const auto t1 = Clock::now();
        us.push_back(std::chrono::duration<double, std::micro>(t1 - t0).count());
    }
    return us;
}

double percentile(std::vector<double>& v, double p) {
    std::sort(v.begin(), v.end());
    const auto idx = static_cast<std::size_t>(p * static_cast<double>(v.size() - 1));
    return v[idx];
}

struct Options {
    std::size_t n{200000};
    efvi::PageMode page_mode{efvi::PageMode::Auto};
    bool strict{false};
};

Options parse_options(int argc, char** argv) {
    Options o{};
    for (int i = 1; i < argc; ++i) {
        const std::string arg(argv[i]);
        if (arg == "--huge1g") o.page_mode = efvi::PageMode::Huge1G;
        else if (arg == "--huge2m") o.page_mode = efvi::PageMode::Huge2M;
        else if (arg == "--normal") o.page_mode = efvi::PageMode::Normal;
        else if (arg == "--strict") o.strict = true;
        else o.n = static_cast<std::size_t>(std::strtoull(argv[i], nullptr, 10));
    }
    return o;
}
}

int main(int argc, char** argv) {
    try {
    const auto options = parse_options(argc, argv);

    Book::Config cfg{};
    cfg.base_price = 9000;
    cfg.tick_size = 1;
    cfg.order_pool_pages = options.page_mode;
    cfg.strict_huge_pages = options.strict;

    Book book(cfg);
    LatencySink sink;

    for (std::uint64_t i = 1; i <= 4096; ++i) {
        efvi::OrderRequest r{};
        r.order_id = i;
        r.instrument = 1;
        r.quantity = 10;
        r.price = 10000 + static_cast<efvi::Price>(i % 4);
        r.side = efvi::Side::Sell;
        book.add(r, sink);
    }

    const auto lat = sample_latency(options.n, [&](std::size_t i) {
        efvi::OrderRequest r{};
        r.order_id = 1000000 + i;
        r.instrument = 1;
        r.quantity = 1;
        r.side = (i & 1u) ? efvi::Side::Buy : efvi::Side::Sell;
        r.tif = efvi::TimeInForce::ImmediateOrCancel;
        r.price = (i & 1u) ? 10004 : 10000;
        book.add(r, sink);
    });
    auto sorted = lat;

    g_hotpath_allocations.store(0, std::memory_order_relaxed);
    g_track_allocations.store(true, std::memory_order_release);

    const auto t0 = Clock::now();
    for (std::size_t i = 0; i < options.n; ++i) {
        efvi::OrderRequest r{};
        r.order_id = 2000000 + i;
        r.instrument = 1;
        r.quantity = 1;
        r.side = (i & 1u) ? efvi::Side::Buy : efvi::Side::Sell;
        r.tif = efvi::TimeInForce::ImmediateOrCancel;
        r.price = (i & 1u) ? 10004 : 10000;
        book.add(r, sink);
    }
    const auto elapsed = std::chrono::duration<double>(Clock::now() - t0).count();
    g_track_allocations.store(false, std::memory_order_release);
    const auto hotpath_allocations = g_hotpath_allocations.load(std::memory_order_relaxed);

    std::ofstream out("efvi_benchmark.csv");
    out << "operations,elapsed_s,ops_per_sec,p50_us,p99_us,trades,live_orders,pool_used,pool_capacity,pool_mapped_bytes,pool_page_size,pool_hugepages,hotpath_allocations\n";
    out << options.n << ',' << elapsed << ',' << (static_cast<double>(options.n) / elapsed) << ','
        << percentile(sorted, .50) << ',' << percentile(sorted, .99) << ',' << sink.trades << ','
        << book.live_orders() << ',' << book.pool_used() << ',' << book.pool_capacity() << ','
        << book.pool_mapped_bytes() << ',' << book.pool_page_size() << ','
        << book.pool_hugepage_backed() << ',' << hotpath_allocations << '\n';

    std::cout << std::fixed << std::setprecision(3)
              << "operations=" << options.n
              << " elapsed_s=" << elapsed
              << " ops_per_sec=" << static_cast<double>(options.n) / elapsed
              << " p50_us=" << percentile(sorted, .50)
              << " p99_us=" << percentile(sorted, .99)
              << " trades=" << sink.trades
              << " live_orders=" << book.live_orders()
              << " mapped_bytes=" << book.pool_mapped_bytes()
              << " page_size=" << book.pool_page_size()
              << " hugepages=" << std::boolalpha << book.pool_hugepage_backed()
              << " hotpath_allocations=" << hotpath_allocations << '\n';

    efvi::SpscRing<efvi::RxDescriptor, 1u << 16> q;
    constexpr std::size_t qn = 1u << 20;
    std::size_t received = 0;
    const auto tq0 = Clock::now();

    std::thread producer([&] {
        for (std::size_t i = 0; i < qn;) {
            efvi::RxDescriptor d{0x1000u + i, 64, 0, static_cast<std::uint32_t>(i)};
            if (q.try_push(d)) ++i;
            else std::this_thread::yield();
        }
    });

    efvi::RxDescriptor d{};
    while (received < qn) {
        if (q.try_pop(d)) {
            if (d.sequence != received) {
                producer.join();
                return 3;
            }
            ++received;
        } else std::this_thread::yield();
    }
    producer.join();

    const auto qsec = std::chrono::duration<double>(Clock::now() - tq0).count();
    std::cout << "spsc_items=" << qn
              << " spsc_items_per_sec=" << static_cast<double>(qn) / qsec
              << " ring_alignment=" << alignof(decltype(q))
              << " slot_alignment=64\n";

    return hotpath_allocations == 0 ? 0 : 4;
    } catch (const std::exception& e) {
        std::cerr << "benchmark error: " << e.what() << "\n";
        return 2;
    }
}