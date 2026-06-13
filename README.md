# partial_message_cheesing

C++ proof-of-concept of the TCP partial-message pre-staging technique used by HFT firms to minimise order-submission latency.

## The technique

A fixed-format exchange order has one byte that varies at decision time (buy vs sell, or a checksum that covers the action). Everything else is known in advance.

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
t=0   → send 1 byte        →  [network]  →  exchange receives final byte
                                              order is now complete
         ^^^^^^^^^^^^^^^^^^^^^^^^
         only 1 byte on critical path
```

The critical-path time from decision to exchange-complete shrinks from `transmit(N) + latency` to `transmit(1) + latency`. On a bandwidth-constrained or high-latency link the saving is proportional to message size.

## Implementation details

- `TCP_NODELAY` is set on both ends — Nagle's algorithm is disabled so the pre-staged bytes are pushed to the wire immediately on `send()`, not coalesced.
- The server timestamps the **first** and **last** byte of each message independently. The spread (`last_ns − first_ns`) reveals the pre-staging in cheese mode: the first 63 bytes arrive long before the decision, and only the final byte lands after.
- The client measures `server_last_byte_ns − client_decision_ns` across-process using `CLOCK_MONOTONIC`, which has a shared epoch on the same host.
- Decision computation is simulated with a spin-wait (no `usleep`), matching real HFT practice.

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
./bin/cheese_client --host 192.168.1.10 --trials 1000
```

### Amplifying the effect with simulated network latency

On pure loopback the difference is visible but modest (~10–40 ns at p50). To exaggerate it, add artificial latency on the loopback interface (requires root):

```sh
# add 500 µs one-way delay on loopback
sudo tc qdisc add dev lo root netem delay 500us

# run the demo — cheese mode should now show ~500 µs vs naive ~500 µs + serialisation overhead
./bin/cheese_client --decision-us 2000

# clean up
sudo tc qdisc del dev lo root
```

The key condition for cheese to win: **network latency < decision window**. If the pre-staged bytes reach the exchange before the decision fires, only 1 byte crosses the wire on the critical path.

## Example output (loopback, no netem)

```
partial_message_cheesing demo
  server  : 127.0.0.1:9001
  msg     : 64 bytes (last byte = action)
  trials  : 2000 per mode
  decision: 100 µs (spin-wait)

  Metric: server_last_byte_ns - client_decision_ns

  Mode      min     mean      p50      p99      max
  ─────────────────────────────────────────────────────
  naive     min=  5822  mean= 47389  p50= 37397  p99=153760  max=231246  ns
  cheese    min=  4100  mean= 28032  p50= 26012  p99= 57164  max= 92205  ns

  cheese p50 advantage: 11385 ns (1.4x faster at median)
```

Cheese mode also shows tighter variance — the pre-staged bytes absorb scheduling jitter during the decision window, leaving only the final 1-byte send on the critical path.

## Caveats

- You commit to the first N−1 bytes before the decision. The technique works only when those bytes are truly invariant (header fields, instrument ID, etc.) and only the action byte varies.
- Real firms pre-stage multiple orders (buy *and* sell) and release one, cancelling the other — requiring the exchange to support conditional or cancel-on-release semantics. Many modern exchanges detect and prohibit this pattern.
- This PoC does not implement cancel-on-release; it demonstrates only the latency mechanic.
