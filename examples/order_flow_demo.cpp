#include "efvi/order_book.hpp"

#include <iostream>
#include <vector>

int main() {
    using Book = efvi::OrderBook<256, 4096, 8192>;
    Book::Config cfg{};
    cfg.base_price = 10000;
    cfg.tick_size = 1;
    cfg.order_pool_pages = efvi::PageMode::Normal;

    Book book(cfg);
    std::vector<efvi::Trade> trades;
    auto sink = [&](const efvi::Trade& t) { trades.push_back(t); };

    efvi::OrderRequest ask{};
    ask.order_id = 1;
    ask.instrument = 7;
    ask.quantity = 100;
    ask.price = 10010;
    ask.side = efvi::Side::Sell;
    book.add(ask, sink);

    efvi::OrderRequest ask2{};
    ask2.order_id = 2;
    ask2.instrument = 7;
    ask2.quantity = 50;
    ask2.price = 10010;
    ask2.side = efvi::Side::Sell;
    book.add(ask2, sink);

    efvi::OrderRequest buy{};
    buy.order_id = 3;
    buy.instrument = 7;
    buy.quantity = 125;
    buy.price = 10010;
    buy.side = efvi::Side::Buy;
    const auto st = book.add(buy, sink);

    std::cout << "filled=" << st.filled_quantity
              << " trades=" << st.trade_count
              << " best_ask=" << book.best_ask()
              << " best_ask_qty=" << book.best_ask_qty() << '\n';

    return (st.filled_quantity == 125 && book.best_ask_qty() == 25) ? 0 : 1;
}