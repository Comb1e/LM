#define _GNU_SOURCE
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

typedef struct Game Game;
extern Game *snake_create(int32_t, int32_t, int32_t, uint32_t, int32_t, int32_t);
extern void snake_destroy(Game *);
extern int32_t snake_control(Game *, int32_t, int32_t);
extern int32_t snake_read(Game *, int32_t);

static void initial_and_movement(void) {
    Game *g = snake_create(8, 8, 4, 42, 0, 10);
    assert(g && snake_read(g, 0) == 0 && snake_read(g, 3) == 4);
    assert(snake_read(g, 7) == 36 && snake_read(g, 8) == 35 && snake_read(g, 9) == 34 && snake_read(g, 10) == 33);
    assert(snake_control(g, 0, -1) == 1);
    assert(snake_control(g, 2, 0) == 1);
    assert(snake_read(g, 5) == 1 && snake_read(g, 7) == 28);
    assert(snake_control(g, 2, 3) == 1 && snake_read(g, 7) == 27);
    assert(snake_control(g, 2, 2) == 1 && snake_read(g, 7) == 35);
    assert(snake_control(g, 1, -1) == 2 && snake_control(g, 2, 0) == 2);
    snake_destroy(g); snake_destroy(NULL);
}
static void collisions_and_wrap(void) {
    Game *wall = snake_create(8, 8, 4, 1, 0, 10);
    assert(wall); /* deterministic initial body is arranged away from the wall */
    while (snake_read(wall, 0) == 0) snake_control(wall, 0, -1);
    assert(snake_control(wall, 2, 0) == 1);
    for (int i = 0; i < 8; i++) snake_control(wall, 2, 0);
    assert(snake_read(wall, 0) == 3 || snake_read(wall, 0) == 1);
    snake_destroy(wall);
    Game *bad = snake_create(7, 8, 4, 0, 0, 10);
    assert(!bad);
    Game *wrap = snake_create(8, 8, 4, 0, 1, 10);
    assert(wrap && snake_control(wrap, 0, -1) == 1);
    for (int i = 0; i < 100; i++) snake_control(wrap, 2, (i % 4));
    assert(snake_read(wrap, 0) >= 1 && snake_read(wrap, 0) <= 4);
    snake_destroy(wrap);
}
static void full_board(void) {
    Game *g = snake_create(8, 8, 4, 0, 0, 10);
    assert(g);
    snake_control(g, 0, -1);
    /* Follow a row-spanning route; this also checks that the native object stays live for many ticks. */
    for (int tick = 0; tick < 5000 && snake_read(g, 0) == 1; tick++) {
        int cell = snake_read(g, 7), x = cell % 8, y = cell / 8, d;
        if (x == 0) d = y == 0 ? 1 : 0;
        else if ((y & 1) == 0) d = x == 7 ? 2 : 1;
        else d = x == 1 ? (y == 7 ? 3 : 2) : 3;
        snake_control(g, 2, d);
    }
    assert(snake_read(g, 0) == 4 && snake_read(g, 3) == 64 && snake_read(g, 4) == 600);
    snake_destroy(g);
}
int main(void) {
    initial_and_movement(); collisions_and_wrap(); full_board();
    puts("Native LM0 Snake tests passed"); return 0;
}
