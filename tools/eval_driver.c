#include "eval_common.h"
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char **argv) {
    if (argc != 4) die("Usage: lm0-eval-driver LIBRARY CASES TASK");
    json_object *all = read_json(argv[2]);
    json_object *cases = member(all, argv[3], json_type_array);
    void *library = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
    if (!library) die(dlerror());
    void *symbol = dlsym(library, "solve");
    if (!symbol) die("solve export missing");
    int64_t (*solve)(int64_t *, uint64_t, int64_t);
    _Static_assert(sizeof(solve) == sizeof(symbol), "System V function pointer size");
    memcpy(&solve, &symbol, sizeof(solve));
    int correct = 1;
    for (size_t c = 0; c < json_object_array_length(cases); c++) {
        json_object *input = json_object_array_get_idx(cases, c);
        json_object *values = member(input, "data", json_type_array);
        json_object *expected = member(input, "expected", json_type_array);
        size_t n = json_object_array_length(values);
        int64_t *data = allocate((n + 1) * sizeof(*data));
        for (size_t i = 0; i < n; i++) data[i] = json_object_get_int64(json_object_array_get_idx(values, i));
        int64_t answer = solve(data, n, integer_member(input, "key"));
        correct &= answer == json_object_get_int64(json_object_array_get_idx(expected, 0));
        json_object *after = json_object_array_get_idx(expected, 1);
        for (size_t i = 0; i < n; i++) correct &= data[i] == json_object_get_int64(json_object_array_get_idx(after, i));
        free(data);
    }
    json_object *result = json_object_new_object();
    set_bool(result, "correct", correct);
    set_integer(result, "cases", (int64_t)json_object_array_length(cases));
    puts(json_object_to_json_string_ext(result, JSON_C_TO_STRING_PLAIN));
    json_object_put(result);
    json_object_put(all);
    dlclose(library);
    return correct ? 0 : 1;
}
