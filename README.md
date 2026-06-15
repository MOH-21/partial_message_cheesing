# partial_message_cheesing

C++ proof-of-concept of the TCP partial-message pre-staging technique used by HFT firms to reduce order-submission latency.

The name "cheesing" comes from gaming slang for exploiting a mechanic in a way the designers didn't intend — here, exploiting TCP's stream semantics to get most of an order's bytes to the exchange before the trading decision is even made.

## The technique

A binary exchange order is mostly fixed: symbol, account, protocol fields. Only a handful of bits vary at decision time — typically the side (buy=0 / sell=1) packed into a single action byte at the end.

**Naive flow**

```
t=0  decision made
t=0  → send all N bytes  →  [network]  →  exchange receives complete order
         ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
         full serialisation + wire time on critical path
```

**Cheese flow**

```
t=-D  pre-stage N-1 bytes  →  [network]  →  arrive at exchange kernel buffer
t=0   decision made
t=0   → send 1 byte        →  [network]  →  exchange receives final byte, order complete
         ^^^^^^^^^^^^^^^^
         only 1 byte on critical path
```

The condition for cheese to win: **network latency < decision window D**. If the pre-staged bytes reach the exchange buffer before the decision fires, the critical-path wire cost shrinks from `transmit(N) + latency` to `transmit(1) + latency`.

## This implementation

- **64-byte order message** — the first 63 bytes are the pre-staged header (symbol, qty, price, account); the 64th byte is the action byte (buy=0 / sell=1) set at decision time.
- **`TCP_NODELAY`** is set on both ends so the kernel pushes pre-staged bytes onto the wire immediately on `send()`, without Nagle coalescing.
- **Server timestamps** the first and last byte of each message independently. In cheese mode the spread (`last_ns − first_ns`) is wide — the header arrived ~D µs before the action byte. In naive mode it is near zero (all bytes arrive together).
- **Metric**: `server_last_byte_ns − client_decision_ns`, comparable across processes on the same host because `CLOCK_MONOTONIC` shares an epoch.
- **Decision simulation** uses a spin-wait — no `usleep`, matching real HFT practice where algorithms never yield the CPU.

## Build

```sh
make        # produces bin/exchange_server and bin/cheese_client
make clean
```

Requires a C++17 compiler and POSIX sockets (Linux / macOS).

## Run

**Terminal 1** – start the mock exchange:
```sh
./bin/exchange_server
```

**Terminal 2** – run the client:
```sh
# default: 2000 trials, 100 µs decision window, both modes
./bin/cheese_client

# options
./bin/cheese_client --trials 5000 --decision-us 500 --mode cheese
./bin/cheese_client --host 192.168.1.10 --trials 1000 --decision-us 50
```

### Loopback results

On pure loopback with 64-byte messages the advantage is within OS scheduling noise — loopback latency is ~30 µs regardless of payload size, so both modes see similar numbers:

```
partial_message_cheesing demo
  server  : 127.0.0.1:9001
  msg     : 64 bytes (last byte = action)
  trials  : 2000 per mode
  decision: 100 µs (spin-wait)

  Metric: server_last_byte_ns - client_decision_ns

  Mode      min     mean      p50      p99      max
  ─────────────────────────────────────────────────────
  naive     min=  8737  mean= 31750  p50= 30334  p99= 75218  max=117110  ns
  cheese    min= 23647  mean= 36552  p50= 34083  p99= 93174  max=135939  ns
```

The structural difference is still visible: look at the server's `first_ns − last_ns` spread logged per-message — cheese mode shows the header arriving long before the action byte, confirming the pre-staging is working.

### Amplifying the effect with bandwidth limiting

The technique's advantage scales with message size and wire time. Simulate a slower link (requires root):

```sh
# throttle loopback to 1 Mbit/s — 64 bytes now takes ~500 µs to transmit
sudo tc qdisc add dev lo root netem rate 1mbit

# with a 2 ms decision window, pre-staged bytes fully arrive before decision
# cheese sends only 1 byte on the critical path (~8 µs) vs naive's 64 bytes (~500 µs)
./bin/cheese_client --decision-us 2000

# clean up
sudo tc qdisc del dev lo root
```

At 1 Mbit/s the p50 gap becomes clearly measurable (~490 µs). Real HFT gains the same proportion at wire speeds on large, near-MTU order messages.

## Caveats

- You commit to the first N−1 bytes before the decision. The technique only works when those bytes are truly invariant (header fields, symbol, quantity) and only the action byte varies.
- Real firms pre-stage both a buy order and a sell order simultaneously, releasing one and cancelling the other when the decision fires. Many modern exchanges detect this pattern and prohibit it.
- This PoC demonstrates only the single-order latency mechanic; cancel-on-release is not implemented.
