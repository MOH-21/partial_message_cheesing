/*
 * ============================================================
 *  partial_message_cheesing — Assignment Template
 * ============================================================
 *
 * Background
 * ----------
 * High-frequency trading firms need to submit orders to an exchange
 * as fast as possible. A binary order message looks like this:
 *
 *   [ symbol (8 B) | qty (8 B) | price (8 B) | account (8 B) |
 *     misc fields (31 B) | action (1 B) ]   <- total: 64 bytes
 *
 * The first 63 bytes are known before the trading decision is made.
 * Only the final "action" byte varies (0 = buy, 1 = sell).
 *
 * The technique ("cheesing"): pre-send the first 63 bytes onto the
 * wire BEFORE deciding. When the decision fires, only 1 byte needs
 * to cross the network. The exchange receives the full order the
 * instant that last byte arrives.
 *
 * Your Task
 * ---------
 * Implement three functions marked TODO below:
 *
 *   1. make_socket()  — connect to the mock exchange
 *   2. run_naive()    — baseline: send all bytes after decision
 *   3. run_cheese()   — optimised: pre-stage N-1 bytes, release 1 byte
 *
 * Everything else is provided. Do not modify main() or the helpers.
 *
 * Build & Run
 * -----------
 *   make
 *   ../common/bin/exchange_server &    # start the exchange
 *   ./bin/cheese_client                # run your implementation
 *
 * Testing
 * -------
 *   bash ../../tests/compare.sh        # grade against reference solution
 *
 * Extension Challenges
 * --------------------
 *   - Try using MSG_MORE on the pre-stage send. Does it help or hurt? Why?
 *   - Increase MSG_SIZE in both files. When does the advantage grow?
 *   - Pre-stage a buy AND a sell order simultaneously on two connections,
 *     releasing one and closing the other (cancel-on-release).
 * ============================================================
 */

#include <algorithm>
#include <arpa/inet.h>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>
#include <vector>

static constexpr int PORT     = 9001;
static constexpr int MSG_SIZE = 64;   // bytes per order; last byte = action (buy/sell)
static constexpr int WARMUP   = 50;   // discard first N results (let kernel settle)

// ── Provided helpers — do not modify ────────────────────────────────────────

static int64_t now_ns() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1'000'000'000LL + ts.tv_nsec;
}

// Busy-spin for exactly `ns` nanoseconds. No OS sleep — matches HFT practice
// where giving up the CPU even briefly adds unpredictable latency.
static void spin_ns(int64_t ns) {
    const int64_t deadline = now_ns() + ns;
    while (now_ns() < deadline);
}

// Receive exactly `len` bytes, looping if the OS returns a short read.
static bool recv_all(int fd, void* buf, int len) {
    int got = 0;
    while (got < len) {
        int n = recv(fd, (char*)buf + got, len - got, 0);
        if (n <= 0) return false;
        got += n;
    }
    return true;
}

// The exchange server replies with two timestamps after each complete message.
struct Reply {
    int64_t first_ns;  // when the server saw the FIRST byte of this message
    int64_t last_ns;   // when the server saw the LAST  byte of this message
    //
    // In cheese mode, (last_ns - first_ns) should be close to your decision
    // window — proof that the header bytes arrived long before the action byte.
    // In naive mode, (last_ns - first_ns) should be near zero — all bytes
    // arrived in a single burst.
};

static void print_stats(const char* label, std::vector<int64_t>& v) {
    std::sort(v.begin(), v.end());
    int64_t sum = 0;
    for (auto x : v) sum += x;
    const double mean = (double)sum / (double)v.size();
    printf("  %-8s  min=%6.0f  mean=%6.0f  p50=%6.0f  p99=%6.0f  max=%6.0f  ns\n",
           label,
           (double)v.front(), mean,
           (double)v[v.size() / 2],
           (double)v[(size_t)(v.size() * 0.99)],
           (double)v.back());
}

// ── TODO 1: make_socket ─────────────────────────────────────────────────────
// Difficulty: ★☆☆
//
// Create a TCP socket and connect it to the exchange server at `host:PORT`.
//
// Important: disable Nagle's algorithm by setting TCP_NODELAY on the socket.
// Nagle coalesces small writes into one larger packet — useful for bulk
// transfers, but fatal here. We need every send() call to flush immediately.
//
// Steps:
//   a) socket(AF_INET, SOCK_STREAM, 0)
//   b) setsockopt(..., IPPROTO_TCP, TCP_NODELAY, ...)
//   c) Fill in a sockaddr_in with AF_INET, host (use inet_pton), PORT
//   d) connect(fd, ...)
//   e) Return fd
//
// On connect failure, print an error and exit(1).
//
static int make_socket(const char* host) {
    // WRITE YOUR CODE HERE

    (void)host;   // remove this line once you use `host`
    return -1;    // replace with the connected file descriptor
}

// ── TODO 2: run_naive ───────────────────────────────────────────────────────
// Difficulty: ★★☆
//
// Baseline: the complete order is sent AFTER the trading decision.
//
// For each trial:
//   1. Vary the action byte:  msg[MSG_SIZE - 1] = (char)(i & 0xFF)
//   2. Spin-wait `decision_ns` nanoseconds  (simulates the algorithm thinking)
//   3. Record:  int64_t decision_time = now_ns()
//   4. send(fd, msg, MSG_SIZE, 0)
//   5. recv_all(fd, &reply, sizeof(reply))
//   6. Append  reply.last_ns - decision_time  to `lat`
//
// Run the same steps WARMUP times first, discarding results.
//
// Return `lat`.
//
static std::vector<int64_t> run_naive(const char* host, int trials, int64_t decision_ns) {
    int fd = make_socket(host);
    char msg[MSG_SIZE]{};
    Reply reply;

    // Warm up — same logic as the main loop but results are thrown away
    for (int i = 0; i < WARMUP; ++i) {
        // WRITE YOUR CODE HERE
    }

    std::vector<int64_t> lat;
    lat.reserve(trials);

    for (int i = 0; i < trials; ++i) {
        msg[MSG_SIZE - 1] = (char)(i & 0xFF);
        // WRITE YOUR CODE HERE
    }

    close(fd);
    return lat;
}

// ── TODO 3: run_cheese ──────────────────────────────────────────────────────
// Difficulty: ★★★
//
// Optimised: pre-stage MSG_SIZE-1 bytes BEFORE the decision; release 1 byte after.
//
// For each trial:
//   1. Vary the action byte:  msg[MSG_SIZE - 1] = (char)(i & 0xFF)
//   2. send(fd, msg, MSG_SIZE - 1, 0)      ← pre-stage header (goes on wire NOW)
//   3. Spin-wait `decision_ns` nanoseconds  ← algorithm thinks; bytes already travelling
//   4. Record:  int64_t decision_time = now_ns()
//   5. send(fd, msg + MSG_SIZE - 1, 1, 0)  ← release action byte
//   6. recv_all(fd, &reply, sizeof(reply))
//   7. Append  reply.last_ns - decision_time  to `lat`
//
// Hint: do NOT pass MSG_MORE to the pre-stage send. MSG_MORE tells the kernel
// to hold the data until more arrives — the opposite of what we want. We need
// those bytes on the wire immediately (TCP_NODELAY handles this).
//
// Run WARMUP iterations first, discarding results.
//
// Return `lat`.
//
static std::vector<int64_t> run_cheese(const char* host, int trials, int64_t decision_ns) {
    int fd = make_socket(host);
    char msg[MSG_SIZE]{};
    Reply reply;

    // Warm up
    for (int i = 0; i < WARMUP; ++i) {
        // WRITE YOUR CODE HERE
    }

    std::vector<int64_t> lat;
    lat.reserve(trials);

    for (int i = 0; i < trials; ++i) {
        msg[MSG_SIZE - 1] = (char)(i & 0xFF);
        // WRITE YOUR CODE HERE
    }

    close(fd);
    return lat;
}

// ── Main — do not modify ─────────────────────────────────────────────────────

static void usage(const char* prog) {
    fprintf(stderr,
        "Usage: %s [options]\n"
        "  --host HOST          server address        (default: 127.0.0.1)\n"
        "  --trials N           messages per mode     (default: 2000)\n"
        "  --decision-us US     decision window in µs (default: 100)\n"
        "  --mode naive|cheese|both\n",
        prog);
}

int main(int argc, char* argv[]) {
    const char* host        = "127.0.0.1";
    int         trials      = 2000;
    int64_t     decision_us = 100;
    const char* mode        = "both";

    for (int i = 1; i < argc; ++i) {
        if      (!strcmp(argv[i], "--host")        && i+1 < argc) host        = argv[++i];
        else if (!strcmp(argv[i], "--trials")      && i+1 < argc) trials      = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--decision-us") && i+1 < argc) decision_us = atoll(argv[++i]);
        else if (!strcmp(argv[i], "--mode")        && i+1 < argc) mode        = argv[++i];
        else if (!strcmp(argv[i], "--help")) { usage(argv[0]); return 0; }
    }

    printf("partial_message_cheesing\n");
    printf("  server  : %s:%d\n", host, PORT);
    printf("  msg     : %d bytes (last byte = action)\n", MSG_SIZE);
    printf("  trials  : %d per mode\n", trials);
    printf("  decision: %ld µs (spin-wait)\n\n", (long)decision_us);

    const int64_t decision_ns = decision_us * 1000LL;

    std::vector<int64_t> naive_lat, cheese_lat;

    if (!strcmp(mode, "naive") || !strcmp(mode, "both"))
        naive_lat  = run_naive (host, trials, decision_ns);
    if (!strcmp(mode, "cheese") || !strcmp(mode, "both"))
        cheese_lat = run_cheese(host, trials, decision_ns);

    printf("  Metric: server_last_byte_ns - client_decision_ns\n\n");
    printf("  Mode      min     mean      p50      p99      max\n");
    printf("  ─────────────────────────────────────────────────────\n");

    if (!naive_lat.empty())  print_stats("naive",  naive_lat);
    if (!cheese_lat.empty()) print_stats("cheese", cheese_lat);

    if (!naive_lat.empty() && !cheese_lat.empty()) {
        std::sort(naive_lat.begin(),  naive_lat.end());
        std::sort(cheese_lat.begin(), cheese_lat.end());
        const double naive_p50  = naive_lat [naive_lat.size()  / 2];
        const double cheese_p50 = cheese_lat[cheese_lat.size() / 2];
        printf("\n  cheese p50 advantage: %.0f ns (%.1fx faster at median)\n",
               naive_p50 - cheese_p50,
               cheese_p50 > 0 ? naive_p50 / cheese_p50 : 0.0);
    }

    return 0;
}
