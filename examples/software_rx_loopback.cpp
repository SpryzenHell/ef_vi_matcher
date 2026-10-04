#include "efvi/order_book.hpp"
#include "efvi/transport.hpp"

#include <array>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <vector>

int main() {
    using Book = efvi::OrderBook<256, 4096, 8192>;

    Book::Config cfg{};
    cfg.base_price = 10000;
    cfg.tick_size = 1;
    cfg.order_pool_pages = efvi::PageMode::Normal;

    Book book(cfg);
    std::array<efvi::PacketBuffer, 4> buffers{};
    efvi::SoftwareRxPath rx(buffers.data(), buffers.size());

    std::vector<efvi::Trade> trades;
    auto sink = [&](const efvi::Trade& trade) {
        trades.push_back(trade);
    };

    buffers[0].order.order_id = 1001;
    buffers[0].order.instrument = 7;
    buffers[0].order.quantity = 25;
    buffers[0].order.price = 10010;
    buffers[0].order.side = efvi::Side::Sell;

    const auto original_address =
        reinterpret_cast<std::uintptr_t>(&buffers[0].order);

    if (!rx.publish(0, 1)) {
        std::cerr << "failed to publish RX descriptor\n";
        return 1;
    }

    efvi::OrderRequest* received = nullptr;
    if (!rx.consume(received)) {
        std::cerr << "failed to consume RX descriptor\n";
        return 1;
    }

    const auto received_address =
        reinterpret_cast<std::uintptr_t>(received);

    const auto status = book.add(*received, sink);

    std::cout << "software RX loopback\n"
              << "  original_order_address: 0x" << std::hex
              << original_address << "\n"
              << "  matcher_order_address:  0x" << received_address << "\n"
              << std::dec
              << "  same_buffer:             " << std::boolalpha
              << (original_address == received_address) << "\n"
              << "  rested:                  " << status.rested << "\n"
              << "  live_orders:             " << book.live_orders() << "\n"
              << "  rx_pending:              " << rx.pending() << "\n";

    return (original_address == received_address &&
            status.rested &&
            book.live_orders() == 1)
               ? 0
               : 2;
}
