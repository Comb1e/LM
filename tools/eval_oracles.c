#include "eval_common.h"
#include <stdlib.h>
#include <string.h>

static int64_t signed_bits(uint64_t value) {
    int64_t result;
    memcpy(&result, &value, sizeof(result));
    return result;
}

static int compare(const void *left, const void *right) {
    int64_t a = *(const int64_t *)left, b = *(const int64_t *)right;
    return (a > b) - (a < b);
}

static int64_t oracle(const char *task, int64_t *data, size_t n, int64_t key) {
    uint64_t result = 0;
    if (!strcmp(task, "product")) result = 1;
    if (!strcmp(task, "minimum") || !strcmp(task, "maximum")) {
        int64_t bound = n ? data[0] : 0;
        for (size_t i = 1; i < n; i++)
            if ((!strcmp(task, "minimum") && data[i] < bound) || (!strcmp(task, "maximum") && data[i] > bound)) bound = data[i];
        return bound;
    }
    if (!strcmp(task, "index_of") || !strcmp(task, "binary_search")) {
        for (size_t i = 0; i < n; i++) if (data[i] == key) return (int64_t)i;
        return -1;
    }
    if (!strcmp(task, "is_sorted") || !strcmp(task, "is_palindrome")) {
        for (size_t i = 0; i < n; i++) {
            if (!strcmp(task, "is_sorted") && i && data[i-1] > data[i]) return 0;
            if (!strcmp(task, "is_palindrome") && data[i] != data[n-1-i]) return 0;
        }
        return 1;
    }
    if (!strcmp(task, "gcd")) {
        if (n < 2) return 0;
        uint64_t a = data[0] < 0 ? 0 - (uint64_t)data[0] : (uint64_t)data[0];
        uint64_t b = data[1] < 0 ? 0 - (uint64_t)data[1] : (uint64_t)data[1];
        while (b) { uint64_t remainder = a % b; a = b; b = remainder; }
        return signed_bits(a);
    }
    if (!strcmp(task, "factorial")) {
        result = 1;
        for (int64_t i = 1; i <= key; i++) result *= (uint64_t)i;
        return signed_bits(result);
    }
    if (!strcmp(task, "fibonacci")) {
        uint64_t next = 1;
        for (int64_t i = 0; i < key; i++) { uint64_t old = result; result = next; next += old; }
        return signed_bits(result);
    }
    if (!strcmp(task, "popcount")) {
        uint64_t bits = (uint64_t)key;
        while (bits) { result += bits & 1; bits >>= 1; }
        return (int64_t)result;
    }
    if (!strcmp(task, "sort")) { qsort(data, n, sizeof(*data), compare); return (int64_t)n; }
    if (!strcmp(task, "reverse")) {
        for (size_t i = 0; i < n/2; i++) { int64_t tmp = data[i]; data[i] = data[n-1-i]; data[n-1-i] = tmp; }
        return (int64_t)n;
    }
    for (size_t i = 0; i < n; i++) {
        uint64_t value = (uint64_t)data[i];
        if (!strcmp(task, "sum") || !strcmp(task, "prefix_sum")) result += value;
        else if (!strcmp(task, "product")) result *= value;
        else if (!strcmp(task, "count_equal")) result += data[i] == key;
        else if (!strcmp(task, "count_positive")) result += data[i] > 0;
        else if (!strcmp(task, "sum_squares")) result += value * value;
        else if (!strcmp(task, "xor_reduce")) result ^= value;
        else if (!strcmp(task, "clamp_sum")) result += (uint64_t)(data[i] < -key ? -key : data[i] > key ? key : data[i]);
        else die("Unknown oracle task");
        if (!strcmp(task, "prefix_sum")) data[i] = signed_bits(result);
    }
    return signed_bits(result);
}

json_object *evaluation_cases(json_object *tasks) {
    json_object *inputs = read_json("evaluation/inputs.json");
    json_object *result = json_object_new_object();
    json_object_object_foreach(tasks, task, description) {
        (void)description;
        json_object *cases = NULL;
        if (!json_object_object_get_ex(inputs, task, &cases)) cases = member(inputs, "default", json_type_array);
        json_object *output = json_object_new_array();
        for (size_t c = 0; c < json_object_array_length(cases); c++) {
            json_object *input = json_object_array_get_idx(cases, c);
            json_object *values = member(input, "data", json_type_array);
            size_t n = json_object_array_length(values);
            int64_t *data = allocate((n + 1) * sizeof(*data));
            for (size_t i = 0; i < n; i++) data[i] = json_object_get_int64(json_object_array_get_idx(values, i));
            int64_t key = integer_member(input, "key");
            if (!strcmp(task, "clamp_sum") && key < 0) key = -key;
            if (!strcmp(task, "binary_search")) qsort(data, n, sizeof(*data), compare);
            json_object *record = json_object_new_object(), *initial = json_object_new_array(), *after = json_object_new_array();
            for (size_t i = 0; i < n; i++) json_object_array_add(initial, json_object_new_int64(data[i]));
            int64_t answer = oracle(task, data, n, key);
            for (size_t i = 0; i < n; i++) json_object_array_add(after, json_object_new_int64(data[i]));
            json_object_object_add(record, "data", initial);
            set_integer(record, "key", key);
            json_object *expected = json_object_new_array();
            json_object_array_add(expected, json_object_new_int64(answer));
            json_object_array_add(expected, after);
            json_object_object_add(record, "expected", expected);
            json_object_array_add(output, record);
            free(data);
        }
        json_object_object_add(result, task, output);
    }
    json_object_put(inputs);
    return result;
}
