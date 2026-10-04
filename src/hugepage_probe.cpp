#include "efvi/fixed_pool.hpp"
#include "efvi/types.hpp"

#include <fstream>
#include <iostream>
#include <string>

static std::string read_value(const char* path) {
    std::ifstream in(path);
    std::string line;
    std::getline(in, line);
    return line;
}

int main() {
    std::cout << "Linux hugepage probe\n";
    std::cout << "1GiB hugepage pool: "
              << read_value("/sys/kernel/mm/hugepages/hugepages-1048576kB/nr_hugepages") << "\n";
    std::cout << "2MiB hugepage pool: "
              << read_value("/sys/kernel/mm/hugepages/hugepages-2048kB/nr_hugepages") << "\n";

    try {
        efvi::FixedPool<efvi::OrderRequest> p(1024, efvi::PageMode::Auto, false);
        std::cout << "allocator_capacity=" << p.capacity()
                  << " mapped_bytes=" << p.mapped_bytes()
                  << " base=" << p.base()
                  << " page_size=" << p.page_size()
                  << " hugepage_backed=" << std::boolalpha << p.hugepage_backed() << "\n";
        std::cout << "allocator_mode=auto (1GiB -> 2MiB -> normal fallback)\n"
                  << "order_request_size=" << sizeof(efvi::OrderRequest)
                  << " order_request_align=" << alignof(efvi::OrderRequest)
                  << " rx_descriptor_size=" << sizeof(efvi::RxDescriptor)
                  << " rx_descriptor_align=" << alignof(efvi::RxDescriptor) << "\n";
    } catch (const std::exception& e) {
        std::cerr << "allocator probe failed: " << e.what() << '\n';
        return 1;
    }
}