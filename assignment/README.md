# Assignment: Partial Message Cheesing

## Background

High-frequency trading (HFT) firms co-locate servers next to stock exchanges
and compete on microsecond-level order submission speed. One technique they
developed is **partial message pre-staging**: pre-sending most of an order's
bytes before the trading decision is made, so that only a single byte crosses
the network on the critical path.

A binary exchange order looks like this:

```
Byte  0– 7:  symbol        (e.g. "AAPL    ")
Byte  8–15:  quantity      (uint64, big-endian)
Byte 16–23:  price         (uint64, fixed-point)
Byte 24–31:  account ID    (uint64)
Byte 32–62:  reserved / protocol fields
Byte 63:     action        ← 0 = buy, 1 = sell  (decided at the last moment)
```

The first 63 bytes are known before the decision. Only the action byte varies.

### Naive approach

```
t = 0   decision made
t = 0   client sends all 64 bytes ──────────────────► exchange has full order
        ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
        64 bytes on the critical path
```

### Cheese approach

```
t = -D  client sends bytes 0–62 ──► arrive at exchange buffer (before decision)
t =  0  decision made
t =  0  client sends byte 63   ──► exchange has full order
        ^^^^^^^^^^^^^^^^^^^^^^^^^^
        1 byte on the critical path
```

The critical-path cost drops from `transmit(64 bytes) + latency` to
`transmit(1 byte) + latency`. On a bandwidth-limited link this is a large win;
on loopback the numbers are close but the structure is correct either way.

---

## Repository layout

```
assignment/
  README.md          ← this file
  common/
    exchange.cpp     ← mock exchange server (given — do not modify)
    Makefile
  template/
    src/client.cpp   ← YOUR implementation goes here
    Makefile
  solution/
    src/client.cpp   ← reference solution (peek only after you've tried)
    Makefile
  tests/
    compare.sh       ← automated grader
```

---

## Getting started

**Step 1 — build the exchange server**

```sh
cd common && make && cd ..
```

**Step 2 — start the exchange server** (leave this terminal open)

```sh
./common/bin/exchange_server
```

**Step 3 — open a second terminal, implement the three functions in:**

```
template/src/client.cpp
```

The file has detailed comments for each function. Functions to implement:

| Function | Difficulty | What it does |
|---|---|---|
| `make_socket()` | ★☆☆ | Create a TCP socket and connect to the server |
| `run_naive()` | ★★☆ | Send the full order after the decision |
| `run_cheese()` | ★★★ | Pre-stage the header, release 1 byte after decision |

**Step 4 — build and run your implementation**

```sh
cd template && make
./bin/cheese_client
```

**Step 5 — compare against the reference solution**

```sh
# from the assignment/ directory
bash tests/compare.sh
```

---

## What the grader checks

| Check | How it grades |
|---|---|
| Compiles | Pass / Fail |
| Naive mode produces valid latency | > 0 ns and < 10 ms |
| Cheese mode produces valid latency | > 0 ns and < 10 ms |
| Cheese p50 within 3× of reference | Structural correctness, not raw speed |
| Cheese p50 < naive p50 | Technique is reducing critical-path latency |
| Cheese p50 ≤ reference cheese p50 | ★ Bonus — you beat the reference |

The 3× threshold for the third check exists because loopback scheduling noise
can easily double numbers between runs. The grader cares that your
implementation is structurally correct, not that you hit an exact number.

---

## Making the effect clearly visible

On pure loopback with 64-byte messages the advantage can be within noise.
To amplify it, simulate a bandwidth-limited link (requires root):

```sh
# throttle loopback to 1 Mbit/s — makes 64 bytes take ~500 µs
sudo tc qdisc add dev lo root netem rate 1mbit

# run with a long decision window so pre-staged bytes definitely arrive first
bash tests/compare.sh --decision-us 2000

# clean up
sudo tc qdisc del dev lo root
```

---

## Extension challenges

Once you have a working implementation, try these:

1. **MSG_MORE experiment** — change the pre-stage `send()` in `run_cheese()`
   to use `MSG_MORE` as a flag. Run the grader. Does it help or hurt? Why?
   (Hint: check the `MSG_MORE` man page and think about what "hold until more
   arrives" means for pre-staging.)

2. **Larger messages** — increase `MSG_SIZE` to 512 or 1400 (near Ethernet MTU)
   in both `template/src/client.cpp` and `common/exchange.cpp`. Re-run the
   grader with bandwidth limiting. How does the advantage scale?

3. **Cancel-on-release** — open *two* connections before the decision: one
   pre-staged for buy, one for sell. After the decision, send the action byte
   on one and close the other. This is closer to how real firms used the
   technique. What protocol changes does the exchange server need to support this?

4. **Beat the reference** — look at what the reference solution does after your
   attempt. Can you find a faster implementation? Ideas: different socket
   buffer sizes (`SO_SNDBUF`), `TCP_QUICKACK` on the server, pinning the process
   to a core with `taskset`.
