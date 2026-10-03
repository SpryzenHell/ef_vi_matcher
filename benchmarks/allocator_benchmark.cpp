#include "efvi/fixed_pool.hpp"
#include "efvi/types.hpp"

#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <vector>

int main(int argc, char** argv) {
    const std::size_t n = argc > 1 ? static_cast<std::size_t>(std::strtoull(argv[1], nullptr, 10)) : 1000000;
    efvi::FixedPool<efvi::OrderRequest> pool(n, efvi::PageMode::Auto, false);
    std::vector<efvi::OrderRequest*> ptrs;
    ptrs.reserve(n);

    const auto t0 = std::chrono::steady_clock::now();
    for (std::size_t i = 0; i < n; ++i) ptrs.push_back(pool.create());
    const auto t1 = std::chrono::steady_clock::now();

    for (auto* p : ptrs) pool.destroy(p);
    const auto t2 = std::chrono::steady_clock::now();

    const auto alloc_s = std::chrono::duration<double>(t1 - t0).count();
    const auto free_s = std::chrono::duration<double>(t2 - t1).count();

    std::cout << std::fixed << std::setprecision(3)
              << "objects=" << n
              << " alloc_ops_per_sec=" << static_cast<double>(n) / alloc_s
              << " free_ops_per_sec=" << static_cast<double>(n) / free_s
              << " stride=" << pool.stride()
              << " mapped_bytes=" << pool.mapped_bytes() << '\n';

    return pool.used() == 0 ? 0 : 1;
}