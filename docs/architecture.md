# Architecture

## Runtime path

Optional DPDK/NIC ingress
  -> fixed RX buffer / rte_mbuf
  -> 64-byte descriptor
  -> cache-line-isolated SPSC ring
  -> in-place OrderRequest
  -> deterministic price ladder + FIFO
  -> fixed OrderNode pool
  -> open-addressed order-ID index
  -> TradeSink

## Data structures

OrderRequest is exactly 64 bytes and trivially copyable.

OrderNode is exactly one 64-byte cache line and stores order identity, sequence, remaining quantity, price-level index and intrusive FIFO links.

Level is exactly one 64-byte cache line and stores aggregate quantity plus FIFO head/tail.

The order-ID index is a fixed-capacity open-addressing table allocated once at construction.

## Matching semantics

The matcher supports limit, market IOC/FOK, IOC and FOK requests; partial and multi-level fills; cancel; and cancel/replace. A quantity-only reduction at unchanged price preserves queue priority. Price changes and size increases are implemented as cancel/replace and receive a new sequence number.

There is no background matcher thread. Order sequence and trade sequence are explicit monotonic counters, so identical input streams produce identical trade streams.
