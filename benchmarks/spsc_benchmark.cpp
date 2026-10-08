#include "efvi/spsc_ring.hpp"
#include "efvi/types.hpp"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <stdexcept>
#include <iomanip>
#include <iostream>
#include <thread>

namespace {

using Clock = std::chrono::steady_clock;

template <std::size_t Capacity>
double run_once(std::size_t n) {
    efvi::SpscRing<efvi::RxDescriptor, Capacity> q;
    std::size_t received = 0;
    const auto t0 = Clock::now();

    std::thread producer([&] {
        for (std::size_t i = 0; i < n;) {
            efvi::RxDescriptor d{
                static_cast<std::uintptr_t>(i + 0x1000u),
                64,
                0,
                static_cast<std::uint32_t>(i)};
            if (q.try_push(d)) {
                ++i;
            } else {
                std::this_thread::yield();
            }
        }
    });

    efvi::RxDescriptor d{};
    while (received < n) {
        if (q.try_pop(d)) {
            if (d.sequence != static_cast<std::uint32_t>(received)) {
                producer.join();
                throw std::runtime_error("SPSC sequence mismatch");
            }
            ++received;
        } else {
            std::this_thread::yield();
        }
    }

    producer.join();
    const double seconds =
        std::chrono::duration<double>(Clock::now() - t0).count();
    return static_cast<double>(n) / seconds;
}

template <std::size_t Capacity>
void print_result(std::size_t n) {
    const double rate = run_once<Capacity>(n);
    std::cout << Capacity << ',' << n << ',' << std::fixed
              << std::setprecision(3) << rate << ',' << alignof(efvi::SpscRing<efvi::RxDescriptor, Capacity>)
              << ",64\n";
}

}

int main(int argc, char** argv) {
    const std::size_t n =
        argc > 1 ? static_cast<std::size_t>(std::strtoull(argv[1], nullptr, 10))
                 : 1000000;

    std::cout << "capacity,items,items_per_sec,ring_alignment,slot_alignment\n";
    print_result<1024>(n);
    print_result<4096>(n);
    print_result<65536>(n);
    return 0;
}
