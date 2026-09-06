#ifndef LM0_V3_H
#define LM0_V3_H
#include <stddef.h>
#include <stdint.h>
/* The existing assembly token ABI. Keep in step with native/base.inc. */
typedef struct CoreToken {
    struct CoreToken *next;
    char *text;
    uint64_t start, end, line, column, end_line, end_column, kind;
} CoreToken;
void v3_prepare(void);
void v3_finish_lex(void);
void v3_verify(void);
void v3_replace(void);
int v3_signature(void *function);
extern long v3_active;
#endif
