#include "efvi/order_book.hpp"

#include <cstdint>
#include <iostream>
#include <vector>

using Book = efvi::OrderBook<4096, 1u << 16, 1u << 17>;

int main() {
    Book::Config cfg{};
    cfg.base_price = 9000;
    cfg.tick_size = 1;
    cfg.order_pool_pages = efvi::PageMode::Auto;

    Book book(cfg);
    std::vector<efvi::Trade> trades;
    trades.reserve(32);
    auto sink = [&](const efvi::Trade& t) { trades.push_back(t); };

    efvi::OrderRequest ask{};
    ask.order_id = 1;
    ask.instrument = 42;
    ask.quantity = 100;
    ask.price = 10000;
    ask.side = efvi::Side::Sell;

    efvi::OrderRequest bid{};
    bid.order_id = 2;
    bid.instrument = 42;
    bid.quantity = 60;
    bid.price = 10000;
    bid.side = efvi::Side::Buy;

    const auto ask_status = book.add(ask, sink);
    const auto bid_status = book.add(bid, sink);

    std::cout << "EF_VI Zero-Copy Matcher\n"
              << "  ask rested: " << std::boolalpha << ask_status.rested << "\n"
              << "  bid filled: " << bid_status.fully_filled << "\n"
              << "  best ask:   " << book.best_ask() << " ticks\n"
              << "  ask qty:    " << book.best_ask_qty() << "\n"
              << "  trades:     " << trades.size() << "\n"
              << "  pool used:  " << book.pool_used() << "\n"
              << "  pool bytes: " << book.pool_mapped_bytes() << "\n";

    return (trades.size() == 1 && book.best_ask_qty() == 40) ? 0 : 1;
}
