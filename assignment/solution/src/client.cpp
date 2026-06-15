// Cheese client: demonstrates partial-message pre-staging vs naive send-after-decision.
//
// Naive mode  – entire message is sent after the trading decision is made.
// Cheese mode – all bytes except the final "action" byte are sent before the decision.
//               Only that last byte is sent on the critical path.
//
// Metric: server_last_byte_ns − client_decision_ns
//   (valid cross-process on the same host; CLOCK_MONOTONIC shares the same epoch).

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

static constexpr int PORT      = 9001;
static constexpr int MSG_SIZE  = 64;   // last byte = action (buy=0 / sell=1)
static constexpr int WARMUP    = 50;

static int64_t now_ns() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1'000'000'000LL + ts.tv_nsec;
}

// Busy-spin for exactly `ns` nanoseconds — no OS scheduling jitter.
static void spin_ns(int64_t ns) {
    const int64_t deadline = now_ns() + ns;
    while (now_ns() < deadline);
}

struct Reply { int64_t first_ns, last_ns; };

static bool recv_all(int fd, void* buf, int len) {
    int got = 0;
    while (got < len) {
        int n = recv(fd, (char*)buf + got, len - got, 0);
        if (n <= 0) return false;
        got += n;
    }
    return true;
}

static int make_socket(const char* host) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    int nd = 1;
    setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &nd, sizeof(nd));  // no Nagle

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    inet_pton(AF_INET, host, &addr.sin_addr);
    addr.sin_port = htons(PORT);

    if (connect(fd, (sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("connect");
        exit(1);
    }
    return fd;
}

// ── Naive mode ─────────────────────────────────────────────────────────────
// decision → send full message → wait for server ack
static std::vector<int64_t> run_naive(const char* host, int trials, int64_t decision_ns) {
    int fd = make_socket(host);

    char msg[MSG_SIZE]{};
    Reply reply;

    // Warm up — let the kernel and server settle.
    for (int i = 0; i < WARMUP; ++i) {
        spin_ns(decision_ns);
        send(fd, msg, MSG_SIZE, 0);
        recv_all(fd, &reply, sizeof(reply));
    }

    std::vector<int64_t> lat;
    lat.reserve(trials);

    for (int i = 0; i < trials; ++i) {
        msg[MSG_SIZE - 1] = (char)(i & 0xFF);   // vary action byte

        spin_ns(decision_ns);
        const int64_t decision_time = now_ns();

        send(fd, msg, MSG_SIZE, 0);
        recv_all(fd, &reply, sizeof(reply));

        lat.push_back(reply.last_ns - decision_time);
    }

    close(fd);
    return lat;
}

// ── Cheese mode ─────────────────────────────────────────────────────────────
// pre-stage N-1 bytes (on the wire) → decision → release last byte
static std::vector<int64_t> run_cheese(const char* host, int trials, int64_t decision_ns) {
    int fd = make_socket(host);

    char msg[MSG_SIZE]{};
    Reply reply;

    // Warm up
    for (int i = 0; i < WARMUP; ++i) {
        send(fd, msg, MSG_SIZE - 1, 0);     // pre-stage — goes on the wire NOW
        spin_ns(decision_ns);
        send(fd, msg + MSG_SIZE - 1, 1, 0); // release action byte
        recv_all(fd, &reply, sizeof(reply));
    }

    std::vector<int64_t> lat;
    lat.reserve(trials);

    for (int i = 0; i < trials; ++i) {
        msg[MSG_SIZE - 1] = (char)(i & 0xFF);   // action byte decided later

        // Pre-stage: traverse the wire BEFORE the decision is made.
        // TCP_NODELAY ensures the kernel flushes immediately (no coalescing).
        send(fd, msg, MSG_SIZE - 1, 0);

        // Simulate decision computation (algorithm evaluating market data).
        spin_ns(decision_ns);
        const int64_t decision_time = now_ns();

        // Critical path: only 1 byte needs to cross the wire.
        send(fd, msg + MSG_SIZE - 1, 1, 0);
        recv_all(fd, &reply, sizeof(reply));

        lat.push_back(reply.last_ns - decision_time);
    }

    close(fd);
    return lat;
}

// ── Stats ───────────────────────────────────────────────────────────────────
static void print_stats(const char* label, std::vector<int64_t>& v) {
    std::sort(v.begin(), v.end());
    int64_t sum = 0;
    for (auto x : v) sum += x;
    const double mean = (double)sum / (double)v.size();
    printf("  %-8s  min=%6.0f  mean=%6.0f  p50=%6.0f  p99=%6.0f  max=%6.0f  ns\n",
           label,
           (double)v.front(),
           mean,
           (double)v[v.size() / 2],
           (double)v[(size_t)(v.size() * 0.99)],
           (double)v.back());
}

// ── Main ────────────────────────────────────────────────────────────────────
static void usage(const char* prog) {
    fprintf(stderr,
        "Usage: %s [options]\n"
        "  --host HOST          server address (default: 127.0.0.1)\n"
        "  --trials N           messages per mode (default: 2000)\n"
        "  --decision-us US     simulated decision window in µs (default: 100)\n"
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

    printf("partial_message_cheesing demo\n");
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
