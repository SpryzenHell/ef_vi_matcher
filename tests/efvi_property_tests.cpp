#include "efvi/order_book.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <functional>
#include <cstdint>
#include <iostream>
#include <limits>
#include <map>
#include <stdexcept>
#include <vector>

namespace {

using Book = efvi::OrderBook<32, 128, 256>;
using Price = efvi::Price;
using Quantity = efvi::Quantity;
using Request = efvi::OrderRequest;

struct ModelOrder {
    std::uint64_t id{0};
    std::uint64_t sequence{0};
    Quantity quantity{0};
    Quantity remaining{0};
    Price price{0};
    efvi::Side side{efvi::Side::Buy};
    efvi::TimeInForce tif{efvi::TimeInForce::GoodTilCancel};
};

struct ModelTrade {
    std::uint64_t aggressor{0};
    std::uint64_t resting{0};
    Price price{0};
    Quantity quantity{0};
    std::uint64_t sequence{0};
};

struct ModelStats {
    Quantity filled{0};
    std::uint32_t trades{0};
    bool fully_filled{false};
    bool rested{false};
    bool cancelled{false};
};

struct Model {
    std::vector<ModelOrder> orders;
    std::vector<ModelTrade> trades;
    std::uint64_t next_sequence{0};
    std::uint64_t next_trade_sequence{0};

    bool has(std::uint64_t id) const {
        for (const auto& o : orders)
            if (o.id == id) return true;
        return false;
    }

    std::size_t index_of(std::uint64_t id) const {
        for (std::size_t i = 0; i < orders.size(); ++i)
            if (orders[i].id == id) return i;
        return orders.size();
    }

    Quantity available(efvi::Side side, Price price, Quantity cap) const {
        std::uint64_t total = 0;
        for (const auto& o : orders) {
            if (o.remaining == 0 || o.side == side) continue;
            const bool crosses =
                price == efvi::kMarketPrice
                    ? true
                    : side == efvi::Side::Buy ? o.price <= price
                                              : o.price >= price;
            if (crosses) total += o.remaining;
            if (total >= cap) break;
        }
        return static_cast<Quantity>(std::min<std::uint64_t>(total, cap));
    }

    std::size_t best_match(efvi::Side side, Price price) const {
        std::size_t best = orders.size();

        for (std::size_t i = 0; i < orders.size(); ++i) {
            const auto& o = orders[i];
            if (o.remaining == 0 || o.side == side) continue;

            const bool crosses =
                price == efvi::kMarketPrice
                    ? true
                    : side == efvi::Side::Buy ? o.price <= price
                                              : o.price >= price;
            if (!crosses) continue;

            if (best == orders.size()) {
                best = i;
                continue;
            }

            const auto& b = orders[best];
            if (side == efvi::Side::Buy) {
                if (o.price < b.price ||
                    (o.price == b.price && o.sequence < b.sequence)) {
                    best = i;
                }
            } else {
                if (o.price > b.price ||
                    (o.price == b.price && o.sequence < b.sequence)) {
                    best = i;
                }
            }
        }

        return best;
    }

    ModelStats add(const Request& req) {
        ModelStats out{};

        if (req.tif == efvi::TimeInForce::FillOrKill ||
            req.tif == efvi::TimeInForce::ImmediateOrCancel) {
            const auto available_qty = available(req.side, req.price, req.quantity);
            if ((req.tif == efvi::TimeInForce::FillOrKill &&
                 available_qty < req.quantity) ||
                (req.tif == efvi::TimeInForce::ImmediateOrCancel &&
                 available_qty == 0)) {
                out.cancelled = true;
                return out;
            }
        }

        ModelOrder in{};
        in.id = req.order_id;
        in.sequence = ++next_sequence;
        in.quantity = req.quantity;
        in.remaining = req.quantity;
        in.price = req.price;
        in.side = req.side;
        in.tif = req.tif;

        while (in.remaining > 0) {
            const std::size_t idx = best_match(in.side, in.price);
            if (idx == orders.size()) break;

            auto& passive = orders[idx];
            const Quantity qty = std::min(in.remaining, passive.remaining);
            in.remaining -= qty;
            passive.remaining -= qty;

            out.filled += qty;
            ++out.trades;
            trades.push_back(
                ModelTrade{in.id, passive.id, passive.price, qty,
                           ++next_trade_sequence});

            if (passive.remaining == 0) {
                orders.erase(orders.begin() + static_cast<std::ptrdiff_t>(idx));
            }
        }

        if (in.remaining == 0) {
            out.fully_filled = true;
        } else if (in.tif == efvi::TimeInForce::ImmediateOrCancel ||
                   in.tif == efvi::TimeInForce::FillOrKill) {
            out.cancelled = true;
        } else {
            orders.push_back(in);
            out.rested = true;
        }

        return out;
    }

    bool cancel(std::uint64_t id) {
        const auto idx = index_of(id);
        if (idx == orders.size()) return false;
        orders.erase(orders.begin() + static_cast<std::ptrdiff_t>(idx));
        return true;
    }

    ModelStats replace(std::uint64_t id,
                       Quantity new_total,
                       Price new_price) {
        const auto idx = index_of(id);
        if (idx == orders.size()) return {};

        const auto old = orders[idx];
        const Quantity executed = old.quantity - old.remaining;
        if (new_total == 0 || new_total < executed) {
            throw std::invalid_argument("invalid replacement quantity");
        }

        if (new_price == old.price && new_total < old.quantity) {
            const Quantity delta = old.quantity - new_total;
            orders[idx].quantity = new_total;
            orders[idx].remaining -= delta;
            if (orders[idx].remaining == 0) {
                orders.erase(orders.begin() + static_cast<std::ptrdiff_t>(idx));
                return ModelStats{0, 0, false, false, true};
            }
            return ModelStats{0, 0, false, true, false};
        }

        if (new_price == old.price && new_total == old.quantity) {
            return ModelStats{0, 0, false, true, false};
        }

        orders.erase(orders.begin() + static_cast<std::ptrdiff_t>(idx));

        Request req{};
        req.order_id = id;
        req.instrument = 1;
        req.quantity = new_total;
        req.price = new_price;
        req.side = old.side;
        req.tif = old.tif;
        return add(req);
    }
};

struct Sink {
    std::vector<efvi::Trade> trades;

    void operator()(const efvi::Trade& t) {
        trades.push_back(t);
    }
};

struct Rng {
    std::uint64_t state;
    explicit Rng(std::uint64_t seed) : state(seed) {}

    std::uint64_t next() {
        std::uint64_t x = state;
        x ^= x >> 12;
        x ^= x << 25;
        x ^= x >> 27;
        state = x;
        return x * 2685821657736338717ULL;
    }

    std::uint32_t uniform(std::uint32_t n) {
        return static_cast<std::uint32_t>(next() % n);
    }
};

Request make_random_request(Rng& rng, std::uint64_t id) {
    Request r{};
    r.order_id = id;
    r.instrument = 1;
    r.quantity = 1 + rng.uniform(20);
    r.side = (rng.uniform(2) == 0) ? efvi::Side::Buy : efvi::Side::Sell;

    const auto tif_roll = rng.uniform(20);
    if (tif_roll == 0) {
        r.tif = efvi::TimeInForce::FillOrKill;
    } else if (tif_roll <= 3) {
        r.tif = efvi::TimeInForce::ImmediateOrCancel;
    } else {
        r.tif = efvi::TimeInForce::GoodTilCancel;
    }

    if (r.tif != efvi::TimeInForce::GoodTilCancel && rng.uniform(10) == 0) {
        r.price = efvi::kMarketPrice;
        if (r.tif == efvi::TimeInForce::GoodTilCancel)
            r.tif = efvi::TimeInForce::ImmediateOrCancel;
    } else {
        r.price = 1000 + static_cast<Price>(rng.uniform(16));
    }

    return r;
}

void check_state(const Book& book, const Model& model) {
    if (book.live_orders() != model.orders.size()) {
        throw std::runtime_error("live-order count mismatch");
    }

    Price best_bid = 0;
    Quantity best_bid_qty = 0;
    Price best_ask = 0;
    Quantity best_ask_qty = 0;

    for (const auto& o : model.orders) {
        if (o.side == efvi::Side::Buy) {
            if (best_bid == 0 || o.price > best_bid) best_bid = o.price;
        } else {
            if (best_ask == 0 || o.price < best_ask) best_ask = o.price;
        }
    }

    for (const auto& o : model.orders) {
        if (o.side == efvi::Side::Buy && o.price == best_bid) {
            best_bid_qty += o.remaining;
        }
        if (o.side == efvi::Side::Sell && o.price == best_ask) {
            best_ask_qty += o.remaining;
        }
    }

    if (book.best_bid() != best_bid || book.best_bid_qty() != best_bid_qty ||
        book.best_ask() != best_ask || book.best_ask_qty() != best_ask_qty) {
        throw std::runtime_error("best-level mismatch");
    }

    std::array<efvi::BookLevel, 10> expected{};
    std::map<Price, efvi::BookLevel, std::greater<Price>> bids;
    std::map<Price, efvi::BookLevel> asks;

    for (const auto& o : model.orders) {
        if (o.side == efvi::Side::Buy) {
            auto& l = bids[o.price];
            l.price = o.price;
            l.quantity += o.remaining;
            ++l.order_count;
        } else {
            auto& l = asks[o.price];
            l.price = o.price;
            l.quantity += o.remaining;
            ++l.order_count;
        }
    }

    std::size_t n = 0;
    for (const auto& entry : bids) {
        if (n == 5) break;
        expected[n++] = entry.second;
    }
    n = 5;
    for (const auto& entry : asks) {
        if (n == 10) break;
        expected[n++] = entry.second;
    }

    const auto actual = book.top_levels();
    for (std::size_t i = 0; i < expected.size(); ++i) {
        if (actual[i].price != expected[i].price ||
            actual[i].quantity != expected[i].quantity ||
            actual[i].order_count != expected[i].order_count) {
            throw std::runtime_error("top-level snapshot mismatch");
        }
    }
}

void test_seed(std::uint64_t seed) {
    Book::Config cfg{};
    cfg.base_price = 1000;
    cfg.tick_size = 1;
    cfg.order_pool_pages = efvi::PageMode::Normal;

    Book book(cfg);
    Model model;
    Sink sink;
    Rng rng(seed);
    std::uint64_t next_id = 1;

    for (std::size_t step = 0; step < 1000; ++step) {
        const auto op = rng.uniform(100);

        if (op < 55 || model.orders.empty()) {
            const auto req = make_random_request(rng, next_id++);
            const auto before = sink.trades.size();
            const auto expected = model.add(req);
            const auto actual = book.add(req, sink);

            if (actual.filled_quantity != expected.filled ||
                actual.trade_count != expected.trades ||
                actual.fully_filled != expected.fully_filled ||
                actual.rested != expected.rested ||
                actual.cancelled != expected.cancelled) {
                throw std::runtime_error("match-status mismatch");
            }

            const auto produced = sink.trades.size() - before;
            if (produced != expected.trades) {
                throw std::runtime_error("trade count mismatch");
            }

            for (std::size_t i = before; i < sink.trades.size(); ++i) {
                const auto& a = sink.trades[i];
                const auto& e = model.trades[i];
                if (a.aggressor_id != e.aggressor ||
                    a.resting_id != e.resting ||
                    a.price != e.price ||
                    a.quantity != e.quantity ||
                    a.sequence != e.sequence) {
                    throw std::runtime_error("trade sequence mismatch");
                }
            }
        } else if (op < 78) {
            const std::size_t idx =
                rng.uniform(static_cast<std::uint32_t>(model.orders.size()));
            const auto id = model.orders[idx].id;
            const bool expected = model.cancel(id);
            const bool actual = book.cancel(id);
            if (expected != actual) {
                throw std::runtime_error("cancel result mismatch");
            }
        } else {
            const std::size_t idx =
                rng.uniform(static_cast<std::uint32_t>(model.orders.size()));
            const auto id = model.orders[idx].id;
            const auto old = model.orders[idx];
            const Quantity executed = old.quantity - old.remaining;
            const Quantity new_total =
                executed + 1 + rng.uniform(20);
            const Price new_price =
                1000 + static_cast<Price>(rng.uniform(16));

            const auto before = sink.trades.size();
            const auto expected = model.replace(id, new_total, new_price);

            ModelStats actual_expected = expected;
            const auto actual =
                book.replace(id, new_total, new_price, sink);

            if (actual.filled_quantity != actual_expected.filled ||
                actual.trade_count != actual_expected.trades ||
                actual.fully_filled != actual_expected.fully_filled ||
                actual.rested != actual_expected.rested ||
                actual.cancelled != actual_expected.cancelled) {
                throw std::runtime_error("replace-status mismatch");
            }

            for (std::size_t i = before; i < sink.trades.size(); ++i) {
                const auto& a = sink.trades[i];
                const auto& e = model.trades[i];
                if (a.aggressor_id != e.aggressor ||
                    a.resting_id != e.resting ||
                    a.price != e.price ||
                    a.quantity != e.quantity ||
                    a.sequence != e.sequence) {
                    throw std::runtime_error("replace trade mismatch");
                }
            }
        }

        check_state(book, model);
    }
}

} // namespace

int main() {
    try {
        constexpr std::uint64_t kSeeds[] = {
            0x1ULL, 0x2ULL, 0x12345678ULL, 0xC0FFEEULL,
            0xDEADBEEFULL, 0xCAFEBABEULL, 0xA5A5A5A5ULL,
            0x31415926ULL
        };

        for (const auto seed : kSeeds) test_seed(seed);

        std::cout << "randomized model comparison passed for "
                  << (sizeof(kSeeds) / sizeof(kSeeds[0]))
                  << " seeds x 1000 operations\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "property test failure: " << e.what() << '\n';
        return 1;
    }
}
