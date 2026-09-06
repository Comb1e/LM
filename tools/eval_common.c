#define _GNU_SOURCE
#include "eval_common.h"
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <poll.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

size_t file_limit = 16777216, output_limit = 1048576;
unsigned process_timeout = 30, run_timeout = 5, max_repairs = 3;

void die(const char *message) {
    json_object *error = json_object_new_object();
    set_bool(error, "ok", 0);
    set_string(error, "error", message);
    puts(json_object_to_json_string_ext(error, JSON_C_TO_STRING_PLAIN));
    exit(2);
}

void *allocate(size_t bytes) {
    void *p = calloc(bytes ? bytes : 1, 1);
    if (!p) die("Allocation failed");
    return p;
}

char *format(const char *fmt, ...) {
    char *result;
    va_list args;
    va_start(args, fmt);
    int size = vasprintf(&result, fmt, args);
    va_end(args);
    if (size < 0) die("Allocation failed");
    return result;
}

char *read_text(const char *path) {
    FILE *file = fopen(path, "rb");
    if (!file) die(format("Cannot read %s: %s", path, strerror(errno)));
    char *text = allocate(file_limit + 2);
    size_t size = fread(text, 1, file_limit + 1, file);
    if (ferror(file) || size > file_limit || memchr(text, 0, size)) die("Invalid or oversized text file");
    fclose(file);
    return text;
}

void write_text(const char *path, const char *text) {
    char *temporary = format("%s.XXXXXX", path);
    int fd = mkstemp(temporary);
    if (fd < 0) die(format("Cannot create %s: %s", path, strerror(errno)));
    FILE *file = fdopen(fd, "wb");
    size_t size = strlen(text);
    if (!file || fwrite(text, 1, size, file) != size || fclose(file) || rename(temporary, path)) {
        unlink(temporary);
        die("Atomic output failed");
    }
    free(temporary);
}

json_object *try_parse_json(const char *text) {
    json_tokener *parser = json_tokener_new();
    json_tokener_set_flags(parser, JSON_TOKENER_STRICT | JSON_TOKENER_VALIDATE_UTF8);
    size_t length = strlen(text);
    if (length > INT_MAX) {json_tokener_free(parser);return NULL;}
    json_object *value = json_tokener_parse_ex(parser, text, (int)length);
    if (json_tokener_get_error(parser) != json_tokener_success || !value) {json_tokener_free(parser);json_object_put(value);return NULL;}
    size_t end = json_tokener_get_parse_end(parser);
    while (end < length && (text[end] == ' ' || text[end] == '\n' || text[end] == '\r' || text[end] == '\t')) end++;
    json_tokener_free(parser);
    if (end != length) {json_object_put(value);return NULL;}
    return value;
}

json_object *parse_json(const char *text) {
    json_object *value=try_parse_json(text);
    if(!value)die("Invalid or trailing JSON input");
    return value;
}

json_object *read_json(const char *path) {
    char *text = read_text(path);
    json_object *value = parse_json(text);
    free(text);
    return value;
}

void write_json(const char *path, json_object *value) {
    write_text(path, json_object_to_json_string_ext(value, JSON_C_TO_STRING_PRETTY));
}

json_object *member(json_object *object, const char *key, enum json_type type) {
    json_object *value = NULL;
    if (!json_object_is_type(object, json_type_object) || !json_object_object_get_ex(object, key, &value) ||
        !json_object_is_type(value, type)) die(format("Missing or invalid field: %s", key));
    return value;
}

const char *string_member(json_object *object, const char *key) {
    json_object *value = member(object, key, json_type_string);
    const char *text = json_object_get_string(value);
    if (strlen(text) != (size_t)json_object_get_string_len(value)) die("Embedded NUL in JSON string");
    return text;
}
int64_t integer_member(json_object *object, const char *key) {
    json_object *value = member(object, key, json_type_int);
    if (json_object_get_uint64(value) > INT64_MAX && json_object_get_int64(value) >= 0) die("Integer exceeds i64");
    return json_object_get_int64(value);
}
void set_string(json_object *object, const char *key, const char *value) { json_object_object_add(object, key, json_object_new_string(value)); }
void set_integer(json_object *object, const char *key, int64_t value) { json_object_object_add(object, key, json_object_new_int64(value)); }
void set_bool(json_object *object, const char *key, int value) { json_object_object_add(object, key, json_object_new_boolean(value)); }

static int64_t milliseconds(void) {
    struct timespec now;
    if (clock_gettime(CLOCK_MONOTONIC, &now)) die("Clock failed");
    return (int64_t)now.tv_sec * 1000 + now.tv_nsec / 1000000;
}

/* Drain both streams while bounding the process group and combined output. */
Process process(char *const argv[], unsigned timeout) {
    return process_in(argv,timeout,NULL);
}

Process process_in(char *const argv[], unsigned timeout,const char *directory) {
    int pipes[2][2];
    if (pipe2(pipes[0], O_CLOEXEC) || pipe2(pipes[1], O_CLOEXEC)) die("Pipe failed");
    pid_t pid = fork();
    if (pid < 0) die("Fork failed");
    if (!pid) {
        setpgid(0, 0);
        dup2(pipes[0][1], STDOUT_FILENO);
        dup2(pipes[1][1], STDERR_FILENO);
        for (int i = 0; i < 2; i++) { close(pipes[i][0]); close(pipes[i][1]); }
        if(directory && chdir(directory)) _exit(126);
        execvp(argv[0], argv);
        _exit(127);
    }
    setpgid(pid, pid);
    Process result = {.out = allocate(output_limit + 1), .err = allocate(output_limit + 1)};
    char *buffers[2] = {result.out, result.err};
    size_t lengths[2] = {0, 0};
    struct pollfd fds[2];
    for (int i = 0; i < 2; i++) {
        close(pipes[i][1]);
        fcntl(pipes[i][0], F_SETFL, O_NONBLOCK);
        fds[i] = (struct pollfd){.fd = pipes[i][0], .events = POLLIN};
    }
    int64_t deadline = milliseconds() + (int64_t)timeout * 1000;
    int status = 0, reaped = 0, stopped = 0;
    while (!reaped || fds[0].fd >= 0 || fds[1].fd >= 0) {
        if (!stopped && milliseconds() >= deadline) result.timed_out = stopped = 1;
        if (stopped) kill(-pid, SIGKILL);
        if (!reaped) {
            pid_t waited = waitpid(pid, &status, WNOHANG);
            if (waited == pid) reaped = 1;
            else if (waited < 0 && errno != EINTR) die("waitpid failed");
        }
        poll(fds, 2, 10);
        for (int i = 0; i < 2; i++) {
            if (fds[i].fd < 0) continue;
            char chunk[4096];
            ssize_t n = read(fds[i].fd, chunk, sizeof(chunk));
            if (n > 0) {
                size_t room = output_limit - lengths[0] - lengths[1];
                size_t copy = (size_t)n < room ? (size_t)n : room;
                memcpy(buffers[i] + lengths[i], chunk, copy);
                lengths[i] += copy;
                if (copy != (size_t)n) result.limited = stopped = 1;
            } else if (!n || (errno != EAGAIN && errno != EINTR)) {
                close(fds[i].fd);
                fds[i].fd = -1;
            }
        }
    }
    result.code = WIFEXITED(status) ? WEXITSTATUS(status) : -WTERMSIG(status);
    return result;
}

void process_free(Process *result) { free(result->out); free(result->err); }
