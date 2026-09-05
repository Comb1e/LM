#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <limits.h>
#include <float.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

_Static_assert(CHAR_BIT == 8 && sizeof(void *) == 8 && sizeof(size_t) == 8,
               "LM0 requires an 8-bit byte and a 64-bit target");
_Static_assert(sizeof(float) == 4 && sizeof(double) == 8 && FLT_RADIX == 2 &&
               FLT_MANT_DIG == 24 && DBL_MANT_DIG == 53,
               "LM0 requires IEEE binary32 and binary64");
_Static_assert(INT64_MIN == -INT64_MAX - 1, "LM0 requires two's complement");

static _Noreturn void lm0_trap(const char *diagnostic) {
    fputs(diagnostic, stderr);
    fputc('\n', stderr);
    exit(70);
}

#define LM0_SIGNED_BITS(N) \
    static int##N##_t lm0_i##N(uint##N##_t bits) { \
        int##N##_t result; \
        memcpy(&result, &bits, sizeof(result)); \
        return result; \
    }
LM0_SIGNED_BITS(8)
LM0_SIGNED_BITS(16)
LM0_SIGNED_BITS(32)
LM0_SIGNED_BITS(64)

static uint64_t lm0_sar(uint64_t bits, unsigned width, unsigned count) {
    uint64_t mask = width == 64 ? UINT64_MAX : (UINT64_C(1) << width) - 1;
    bits &= mask;
    uint64_t result = bits >> count;
    if (count && (bits & (UINT64_C(1) << (width - 1))))
        result |= mask ^ (mask >> count);
    return result;
}

static float lm0_f32(double value) {
    /* Round the narrow interval above FLT_MAX without an out-of-range C cast. */
    if (isfinite(value) && fabs(value) > FLT_MAX) {
        float magnitude = fabs(value) >= 0x1.ffffffp127 ? INFINITY : FLT_MAX;
        return copysignf(magnitude, signbit(value) ? -1.0f : 1.0f);
    }
    return (float)value;
}

static void *lm0_alloc(uint64_t count, size_t size, const char *error) {
    if (count > SIZE_MAX / size) lm0_trap(error);
    size_t bytes = (size_t)count * size;
    void *result = malloc(bytes ? bytes : 1);
    if (!result) lm0_trap(error);
    return result;
}

static void *lm0_offset(void *base, int64_t index, size_t size, const char *error) {
    uint64_t magnitude = index < 0 ? UINT64_C(0) - (uint64_t)index : (uint64_t)index;
    if (magnitude > (uint64_t)PTRDIFF_MAX / size) lm0_trap(error);
    if (!index) return base;
    return index < 0 ? (unsigned char *)base - magnitude * size
                     : (unsigned char *)base + magnitude * size;
}
