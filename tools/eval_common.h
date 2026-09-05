#ifndef LM0_EVAL_COMMON_H
#define LM0_EVAL_COMMON_H
#include <json-c/json.h>
#include <stddef.h>
#include <stdint.h>

typedef struct { char *out; char *err; int code; int timed_out; int limited; } Process;
extern size_t file_limit, output_limit;
extern unsigned process_timeout, run_timeout, max_repairs;
char *lm0_sha256(const void *, size_t, char[65]);
void die(const char *message);
void *allocate(size_t bytes);
char *format(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
char *read_text(const char *path);
void write_text(const char *path, const char *text);
json_object *parse_json(const char *text);
json_object *read_json(const char *path);
void write_json(const char *path, json_object *value);
json_object *member(json_object *object, const char *key, enum json_type type);
const char *string_member(json_object *object, const char *key);
int64_t integer_member(json_object *object, const char *key);
void set_string(json_object *object, const char *key, const char *value);
void set_integer(json_object *object, const char *key, int64_t value);
void set_bool(json_object *object, const char *key, int value);
Process process(char *const argv[], unsigned timeout);
void process_free(Process *result);
json_object *evaluation_cases(json_object *tasks);
#endif
