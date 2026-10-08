# Architecture

## Data path

NIC or DPDK input
-> RX buffer
-> cache-line-isolated SPSC ring
-> in-place OrderRequest
-> fixed order pool
-> price ladder and FIFO matching
-> fixed order-ID index
-> trade sink

The normal software loopback uses the same request address through the descriptor ring. The optional DPDK layer keeps the mbuf owner with the in-place request pointer.

## Main data structures

| Data | Size / property |
|---|---|
| OrderRequest | 64 bytes, 64-byte aligned |
| OrderNode | 64 bytes |
| Price Level | 64 bytes |
| SPSC slot | 64-byte aligned |
| RX descriptor | 16 bytes |
| Order-ID index | fixed open-addressed table |
| Order pool | fixed-capacity intrusive free list |

## Matching rules

Orders are matched by price first and arrival sequence second.

Supported order types:
- limit;
- market IOC/FOK;
- IOC;
- FOK.

Supported book operations:
- add;
- cancel;
- cancel/replace;
- partial fills;
- multi-level fills.

A size reduction at the same price keeps its queue position. A price change or size increase removes the old order and inserts a new one with a new sequence number.

There is no background matching thread in the reference implementation.
