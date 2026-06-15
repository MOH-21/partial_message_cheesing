// Mock exchange server: receives fixed-size order messages, timestamps first
// and last byte arrival, and ACKs so the client can measure decision-to-fill latency.

#include <arpa/inet.h>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

static constexpr int PORT     = 9001;
static constexpr int MSG_SIZE = 64;

static int64_t now_ns() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1'000'000'000LL + ts.tv_nsec;
}

// Sent back to the client after every complete message.
struct Reply {
    int64_t first_ns;  // when first byte of this message arrived
    int64_t last_ns;   // when last  byte of this message arrived
};

static void handle_client(int fd) {
    int nd = 1;
    setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &nd, sizeof(nd));

    char buf[MSG_SIZE];
    long msg_count = 0;

    while (true) {
        int     received = 0;
        int64_t first_ns = 0, last_ns = 0;

        while (received < MSG_SIZE) {
            int n = recv(fd, buf + received, MSG_SIZE - received, 0);
            if (n <= 0) {
                printf("Client disconnected after %ld messages\n", msg_count);
                return;
            }
            if (received == 0) first_ns = now_ns();
            received += n;
        }
        last_ns = now_ns();
        ++msg_count;

        Reply reply{first_ns, last_ns};
        if (send(fd, &reply, sizeof(reply), 0) != (int)sizeof(reply)) return;
    }
}

int main() {
    int srv = socket(AF_INET, SOCK_STREAM, 0);
    int opt = 1;
    setsockopt(srv, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    sockaddr_in addr{};
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port        = htons(PORT);

    if (bind(srv, (sockaddr*)&addr, sizeof(addr)) < 0) { perror("bind"); return 1; }
    listen(srv, 10);

    printf("Exchange mock listening on port %d  (msg_size=%d bytes)\n", PORT, MSG_SIZE);
    printf("Reply carries first/last byte timestamps so client can compute latency.\n\n");

    while (true) {
        int fd = accept(srv, nullptr, nullptr);
        if (fd < 0) continue;
        printf("Client connected\n");
        handle_client(fd);
        close(fd);
    }
}
