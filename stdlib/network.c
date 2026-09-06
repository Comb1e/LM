#define _GNU_SOURCE
#include "std.h"
#include <arpa/inet.h>
#include <errno.h>
#include <limits.h>
#include <poll.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <sys/random.h>
#include <sys/socket.h>
#include <unistd.h>

static int address(uint8_t *host, uint64_t len, int32_t port, struct sockaddr_storage *out, socklen_t *size) {
    if (!host || !len || len >= INET6_ADDRSTRLEN || port < 0 || port > 65535 || memchr(host, 0, len)) return STD_INVALID;
    char name[INET6_ADDRSTRLEN];
    memcpy(name, host, len); name[len] = 0;
    if (!strcmp(name, "localhost")) strcpy(name, "127.0.0.1");
    memset(out, 0, sizeof(*out));
    struct sockaddr_in *v4 = (void *)out;
    if (inet_pton(AF_INET, name, &v4->sin_addr) == 1) {
        v4->sin_family = AF_INET; v4->sin_port = htons(port); *size = sizeof(*v4); return STD_OK;
    }
    struct sockaddr_in6 *v6 = (void *)out;
    if (inet_pton(AF_INET6, name, &v6->sin6_addr) == 1) {
        v6->sin6_family = AF_INET6; v6->sin6_port = htons(port); *size = sizeof(*v6); return STD_OK;
    }
    return STD_INVALID;
}
static int owned_socket(int fd, StdSocket **out) {
    StdSocket *s;
    int status = std_core_allocate(1, sizeof(*s), (void **)&s);
    if (status) { close(fd); return status; }
    s->fd = fd; *out = s; return STD_OK;
}
int32_t std_net_listen(uint8_t *host, uint64_t len, int32_t port, int32_t backlog, StdSocket **out) {
    if (!out || backlog <= 0) return STD_INVALID;
    struct sockaddr_storage addr; socklen_t size;
    int status = address(host, len, port, &addr, &size);
    if (status) return status;
    int fd = socket(addr.ss_family, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0), one = 1;
    if (fd < 0) return STD_IO;
    if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one)) ||
        (addr.ss_family == AF_INET6 && setsockopt(fd, IPPROTO_IPV6, IPV6_V6ONLY, &one, sizeof(one))) ||
        bind(fd, (void *)&addr, size) || listen(fd, backlog)) { close(fd); return STD_IO; }
    return owned_socket(fd, out);
}
int32_t std_net_accept(StdSocket *listener, StdSocket **out) {
    if (!listener || !out) return STD_INVALID;
    int fd;
    do { fd = accept4(listener->fd, NULL, NULL, SOCK_NONBLOCK | SOCK_CLOEXEC); } while (fd < 0 && errno == EINTR);
    if (fd < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) { *out = NULL; return STD_OK; }
        return STD_IO;
    }
    return owned_socket(fd, out);
}
int32_t std_net_connect(uint8_t *host, uint64_t len, int32_t port, StdSocket **out) {
    if (!out) return STD_INVALID;
    struct sockaddr_storage addr; socklen_t size;
    int status = address(host, len, port, &addr, &size);
    if (status) return status;
    int fd = socket(addr.ss_family, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
    if (fd < 0) return STD_IO;
    if (connect(fd, (void *)&addr, size) && errno != EINPROGRESS && errno != EINTR) { close(fd); return STD_IO; }
    return owned_socket(fd, out);
}
int32_t std_net_connected(StdSocket *s) {
    if (!s) return STD_INVALID;
    int error = 0; socklen_t size = sizeof(error);
    return getsockopt(s->fd, SOL_SOCKET, SO_ERROR, &error, &size) || error ? STD_IO : STD_OK;
}
int32_t std_net_port(StdSocket *s, int32_t *out) {
    if (!s || !out) return STD_INVALID;
    struct sockaddr_storage a; socklen_t size = sizeof(a);
    if (getsockname(s->fd, (void *)&a, &size)) return STD_IO;
    *out = ntohs(a.ss_family == AF_INET ? ((struct sockaddr_in *)&a)->sin_port : ((struct sockaddr_in6 *)&a)->sin6_port);
    return STD_OK;
}
int32_t std_net_receive(StdSocket *s, uint8_t *data, uint64_t capacity, uint64_t *count) {
    if (!s || !count || (capacity && !data)) return STD_INVALID;
    if (capacity > INT64_MAX) return STD_RANGE;
    if (!capacity) { *count = 0; return STD_OK; }
    ssize_t n;
    do { n = recv(s->fd, data, capacity, 0); } while (n < 0 && errno == EINTR);
    if (n < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) { *count = 0; return STD_OK; }
        return STD_IO;
    }
    *count = n; return n ? STD_OK : STD_EOF;
}
int32_t std_net_send(StdSocket *s, uint8_t *data, uint64_t len, uint64_t *count) {
    if (!s || !count || (len && !data)) return STD_INVALID;
    if (len > INT64_MAX) return STD_RANGE;
    ssize_t n;
    do { n = send(s->fd, data, len, MSG_NOSIGNAL); } while (n < 0 && errno == EINTR);
    if (n < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) { *count = 0; return STD_OK; }
        return STD_IO;
    }
    *count = n; return STD_OK;
}
int32_t std_net_poll(StdPoll *items, uint64_t count, int32_t timeout) {
    if ((count && !items) || timeout < 0) return STD_INVALID;
    if (count > INT_MAX || count > INT64_MAX / sizeof(struct pollfd)) return STD_RANGE;
    struct pollfd *fds;
    int status = std_core_allocate(count, sizeof(*fds), (void **)&fds);
    if (status) return status;
    for (uint64_t i = 0; i < count; i++) {
        if (items[i].events & ~3) { free(fds); return STD_INVALID; }
        fds[i].fd = items[i].socket ? items[i].socket->fd : -1;
        fds[i].events = ((items[i].events & 1) ? POLLIN : 0) | ((items[i].events & 2) ? POLLOUT : 0);
    }
    int n = poll(fds, count, timeout);
    if (n < 0 && errno != EINTR) { free(fds); return STD_IO; }
    for (uint64_t i = 0; i < count; i++) {
        int r = n < 0 ? 0 : fds[i].revents;
        items[i].ready = ((r & POLLIN) ? 1 : 0) | ((r & POLLOUT) ? 2 : 0) | ((r & (POLLERR | POLLHUP | POLLNVAL)) ? 4 : 0);
    }
    free(fds); return STD_OK;
}
int32_t std_net_close(StdSocket *s) {
    if (!s) return STD_OK;
    int status = close(s->fd) ? STD_IO : STD_OK;
    free(s); return status;
}
int32_t std_random_secure(uint8_t *data, uint64_t len) {
    if (len && !data) return STD_INVALID;
    if (len > INT64_MAX) return STD_RANGE;
    uint64_t done = 0;
    while (done < len) {
        ssize_t n = getrandom(data + done, len - done, 0);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) return STD_IO;
        done += n;
    }
    return STD_OK;
}
int32_t std_process_argument(int32_t argc, uint8_t **argv, int32_t index, uint8_t **data, uint64_t *len) {
    if (!argv || !data || !len || argc < 0) return STD_INVALID;
    if (index < 0 || index >= argc) return STD_RANGE;
    *data = argv[index]; *len = strlen((char *)argv[index]); return STD_OK;
}
int32_t std_process_executable(StdBuf **out) {
    if (!out) return STD_INVALID;
    StdBuf *b;
    int status = std_bytes_new(&b);
    if (status) return status;
    uint64_t size = 256;
    for (;;) {
        status = std_bytes_reserve(b, size);
        if (status) break;
        ssize_t n = readlink("/proc/self/exe", (char *)b->data, size);
        if (n < 0) { status = STD_IO; break; }
        if ((uint64_t)n < size) { b->len = n; *out = b; return STD_OK; }
        if (size > (uint64_t)INT64_MAX / 2) { status = STD_RANGE; break; }
        size *= 2;
    }
    std_bytes_destroy(b); return status;
}
typedef struct { struct sigaction interrupt, terminate; } SignalState;
static volatile sig_atomic_t stopping;
static StdShutdown *active_guard;
static void stop_signal(int signal) { (void)signal; stopping = 1; }
int32_t std_process_shutdown_begin(StdShutdown **out) {
    if (!out || active_guard) return STD_INVALID;
    StdShutdown *g = NULL; SignalState *state = NULL;
    int status = std_core_allocate(1, sizeof(*g), (void **)&g);
    if (status) return status;
    status = std_core_allocate(1, sizeof(*state), (void **)&state);
    if (status) { free(g); return status; }
    struct sigaction action = {0}; action.sa_handler = stop_signal; sigemptyset(&action.sa_mask);
    stopping = 0;
    if (sigaction(SIGINT, &action, &state->interrupt)) { free(g); free(state); return STD_IO; }
    if (sigaction(SIGTERM, &action, &state->terminate)) {
        sigaction(SIGINT, &state->interrupt, NULL); free(g); free(state); return STD_IO;
    }
    g->storage = state; active_guard = g; *out = g; return STD_OK;
}
_Bool std_process_shutdown_requested(StdShutdown *g) { return g && stopping; }
void std_process_shutdown_end(StdShutdown *g) {
    if (!g) return;
    SignalState *state = g->storage;
    sigaction(SIGINT, &state->interrupt, NULL); sigaction(SIGTERM, &state->terminate, NULL);
    active_guard = NULL; free(state); free(g);
}
