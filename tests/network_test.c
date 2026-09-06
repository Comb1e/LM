#define _GNU_SOURCE
#include "std.h"
#include <assert.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

static void parse_tests(void) {
    StdHttp *p = NULL;
    assert(!std_http_new(1024, 64, 8, &p));
    const char *request = "POST /api/games?q=1 HTTP/1.1\r\nHost: localhost\r\nContent-Type: application/json\r\nContent-Length: 2\r\n\r\n{}";
    for (size_t split = 0; split <= strlen(request); split++) {
        std_http_reset(p); _Bool ready = 0;
        assert(!std_http_feed(p, (uint8_t *)request, split, &ready));
        if (!ready) assert(!std_http_feed(p, (uint8_t *)request + split, strlen(request) - split, &ready));
        assert(ready && std_http_method(p) == 2);
        uint8_t *data; uint64_t len;
        std_http_target(p, &data, &len); assert(len == 14 && !memcmp(data, "/api/games?q=1", len));
        std_http_body(p, &data, &len); assert(len == 2 && !memcmp(data, "{}", 2));
        assert(!std_http_header(p, (uint8_t *)"CONTENT-TYPE", 12, &data, &len));
        assert(len == 16 && !memcmp(data, "application/json", len));
    }
    std_http_reset(p); _Bool ready = 0;
    for (size_t i = 0; i < strlen(request); i++) {
        assert(!std_http_feed(p, (uint8_t *)request + i, 1, &ready));
        assert(ready == (i + 1 == strlen(request)));
    }
    const struct { const char *text; int code; } bad[] = {
        {"GET / HTTP/1.1\r\n\r\n", 400},
        {"GET / HTTP/1.1\nHost: x\r\n\r\n", 400},
        {"GET / HTTP/1.1\r\nHost : x\r\n\r\n", 400},
        {"GET / HTTP/1.1\r\nHost: x\r\nHost: x\r\n\r\n", 400},
        {"GET / HTTP/1.0\r\nContent-Length: 0\r\ncontent-length: 0\r\n\r\n", 400},
        {"GET / HTTP/1.0\r\nContent-Length: -1\r\n\r\n", 400},
        {"GET / HTTP/1.0\r\nContent-Length: 65\r\n\r\n", 413},
        {"GET / HTTP/1.0\r\nTransfer-Encoding: chunked\r\n\r\n", 501},
        {"GET / HTTP/1.0\r\nExpect: 100-continue\r\n\r\n", 417},
        {"GET / HTTP/1.0\r\nOrigin: x\r\nORIGIN: x\r\n\r\n", 400},
        {"GET / HTTP/1.0\r\nContent-Type: x\r\nCONTENT-TYPE: x\r\n\r\n", 400},
        {"GET /#x HTTP/1.0\r\n\r\n", 400},
        {"GET / HTTP/2.0\r\n\r\n", 400},
        {"GET / HTTP/1.0\r\nX: a\rbad\r\n\r\n", 400},
    };
    for (size_t i = 0; i < sizeof(bad) / sizeof(*bad); i++) {
        std_http_reset(p); ready = 1;
        assert(std_http_feed(p, (uint8_t *)bad[i].text, strlen(bad[i].text), &ready));
        assert(std_http_error(p) == bad[i].code && ready);
    }
    std_http_destroy(p);
    assert(!std_http_new(16, 0, 1, &p));
    assert(std_http_feed(p, (uint8_t *)request, 17, &ready) == STD_LIMIT);
    std_http_destroy(p);
    StdBuf *response = NULL;
    const char *headers = "Content-Type: application/json\r\n";
    assert(!std_http_response(201, (uint8_t *)headers, strlen(headers), (uint8_t *)"{}", 2, 1024, &response));
    const char *expected = "HTTP/1.1 201 LM0\r\nContent-Length: 2\r\nConnection: close\r\nContent-Type: application/json\r\n\r\n{}";
    assert(response->len == strlen(expected) && !memcmp(response->data, expected, response->len));
    std_bytes_destroy(response);
    response = NULL;
    assert(std_http_response(200, NULL, 0, (uint8_t *)"a", 1, 1, &response) == STD_LIMIT && !response);
}
static void socket_tests(void) {
    StdSocket *listener = NULL, *client = NULL, *peer = NULL;
    assert(!std_net_listen((uint8_t *)"127.0.0.1", 9, 0, 4, &listener));
    int32_t port; assert(!std_net_port(listener, &port) && port > 0);
    assert(!std_net_accept(listener, &peer) && !peer);
    assert(!std_net_connect((uint8_t *)"localhost", 9, port, &client));
    StdPoll events[] = {{listener, 1, 0}, {client, 2, 0}};
    assert(!std_net_poll(events, 2, 1000) && events[1].ready);
    assert(!std_net_connected(client));
    assert(!std_net_poll(events, 1, 1000) && events[0].ready);
    assert(!std_net_accept(listener, &peer) && peer);
    uint8_t data[32]; uint64_t n = 99;
    assert(!std_net_receive(peer, data, sizeof(data), &n) && !n);
    assert(!std_net_send(client, (uint8_t *)"hello", 5, &n) && n == 5);
    events[0] = (StdPoll){peer, 1, 0};
    assert(!std_net_poll(events, 1, 1000) && events[0].ready == 1);
    assert(!std_net_receive(peer, data, sizeof(data), &n) && n == 5 && !memcmp(data, "hello", 5));
    assert(!std_net_close(client));
    assert(!std_net_poll(events, 1, 1000));
    assert(std_net_receive(peer, data, sizeof(data), &n) == STD_EOF && !n);
    assert(!std_net_close(peer)); assert(!std_net_close(listener)); assert(!std_net_close(NULL));
    assert(std_net_listen((uint8_t *)"bad host", 8, 0, 1, &listener) == STD_INVALID);
}
static void process_tests(int argc, char **argv) {
    uint8_t *data; uint64_t n;
    assert(!std_process_argument(argc, (uint8_t **)argv, 0, &data, &n));
    assert(n == strlen(argv[0]) && data == (uint8_t *)argv[0]);
    assert(std_process_argument(argc, (uint8_t **)argv, argc, &data, &n) == STD_RANGE);
    StdBuf *path; assert(!std_process_executable(&path) && path->len && path->data[0] == '/'); std_bytes_destroy(path);
    uint8_t random[32] = {0}; assert(!std_random_secure(random, sizeof(random)));
    unsigned sum = 0; for (size_t i = 0; i < sizeof(random); i++) sum |= random[i]; assert(sum);
    StdShutdown *guard; assert(!std_process_shutdown_begin(&guard));
    assert(!std_process_shutdown_requested(guard)); raise(SIGTERM); assert(std_process_shutdown_requested(guard));
    StdShutdown *other = NULL; assert(std_process_shutdown_begin(&other) == STD_INVALID && !other);
    std_process_shutdown_end(guard); std_process_shutdown_end(NULL);
}
int main(int argc, char **argv) {
    parse_tests(); socket_tests(); process_tests(argc, argv);
    puts("HTTP, socket, entropy and process tests passed"); return 0;
}
