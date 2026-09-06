# Snake

A playable Snake game whose state machine, movement, collisions, growth,
food placement, scoring, and victory condition are implemented in
[`engine_v2.lm0`](engine_v2.lm0). The canvas UI includes keyboard, swipe, and touch
controls, classic/wrap modes, adjustable pace, colors, sound, fullscreen,
and personal records saved in the browser.

## Run

From the repository root, with GNU make, GCC, binutils, and x86-64 Linux:

```sh
make snake
build/snake/snake
```

Open <http://127.0.0.1:4173>. The server and gameplay engine are native LM0
executables; no Python runtime or third-party server package is needed at
startup. Use `--port 0` to select an ephemeral port, or pass `--config`,
`--assets`, and `--host` to override bundled files and binding.
Stop the foreground server with Ctrl+C.

Use `--port 4175` when the default port is occupied. For a phone on the same LAN,
bind explicitly with `--host 0.0.0.0` and open the computer's LAN address and port.
This is a local game host, not a production internet service. The browser UI
requires the native server; it cannot run by opening `index.html` directly.

## Controls

| Action | Input |
| --- | --- |
| Start/resume | Play button, Space, or P |
| Move | Arrow keys, WASD, swipe on the board, or touch direction buttons |
| Pause/resume | Space, P, or the pause button |
| New game | R or the restart button; active runs require confirmation |
| Fullscreen | Fullscreen button; Escape exits |

Classic ends the run at a wall; Wrap connects opposite edges. Self-collisions
end both modes. An immediate reversal is ignored, and up to two upcoming turns
are buffered. Moving into the departing tail's cell is legal. Filling the board
wins the game. Changing mode starts a new run; pace can change during play.
Focus loss or a hidden tab pauses play. After an interrupted connection,
Reconnect reads the authoritative state and pauses it before resuming. Commands
with an uncertain result are not replayed. An expired session starts a fresh game.

## Configuration

Edit [`config.json`](config.json), or pass `--config PATH`, then restart the
server. It controls board dimensions, starting length, food points, pace limits,
server limits, and board colors. Board dimensions are 8 through 64 cells per
axis; starting length must be between 2 and half the width. Sessions are isolated,
bounded in number, and released when expired or when the server shuts down.
Scores and completed run history are local to each browser, not a shared leaderboard.

## Architecture

```text
Keyboard / touch -> serialized HTTP commands -> native LM0
Canvas rendering <- JSON snapshot             <- opaque game-state accessors
```

`server.lm0` is compiled ahead of time into `build/snake/snake`. The LM0
standard libraries provide nonblocking sockets, incremental HTTP parsing, JSON
configuration, secure session entropy, monotonic deadlines, and signal cleanup.
The browser sets tick cadence and interpolates received cell positions. It does
not decide collisions, food, or scores.

| Export | Contract |
| --- | --- |
| `snake_create(i32 width, i32 height, i32 initial, u32 seed, i32 wrap, i32 points) -> ptr<Game>` | Creates an owned handle; invalid configuration returns null |
| `snake_destroy(ptr<Game>) -> void` | Releases the handle and its body allocation; null is accepted |
| `snake_control(ptr<Game>, i32 action, i32 direction) -> i32` | Action 0 starts/resumes, 1 pauses, 2 ticks; returns state or -1 for null |
| `snake_read(ptr<Game>, i32 selector) -> i32` | Selectors 0..6 read status, direction, food, length, score, ticks, reason; 7+i reads body cell i; invalid selectors return -1 |

States are `0=ready`, `1=running`, `2=paused`, `3=lost`, `4=won`. Only a running
game advances. Directions are `0=up`, `1=right`, `2=down`, `3=left`; -1 retains
the current direction. Cells encode `y * width + x`. Reasons are `0=none`,
`1=wall`, `2=self`. Food is -1 only when the board is complete. Native handles
are opaque; do not access them after destruction or use them concurrently.

The engine uses the new `move` instruction to shift overlapping body storage.
Food selection chooses a free-cell rank with a seeded LCG and scans bounded
storage, so nearly full boards never depend on an unbounded retry loop. The
native allocation and trap semantics are those of the LM0 specification.

## Validation

From the repository root:

```sh
make test-snake
```

Native tests exercise movement against an independent oracle, both collision
types, tail-cell entry, deterministic seeds, growth, complete boards, invalid
inputs, HTTP framing, nonblocking sockets, entropy, deadlines, and shutdown.

For browser development, from `examples/snake`:

```sh
npm ci
npx playwright install chromium
npm test
```

Tests start a separate server on port 4174 and cover desktop, tablet, and phone
viewports, visible assets, canvas pixels and movement, input buffering, pause,
restart, settings, wrapping, saved records, and connection recovery. Screenshots
are written to `build/snake/`. Set `PLAYWRIGHT_CHROMIUM_EXECUTABLE` to use an
already installed compatible Chromium binary.

The selected Lucide icons are checked in as `static/icons.js`. To regenerate
them after changing `icons-entry.js`, run `npm run assets`. See
[`THIRD_PARTY.md`](THIRD_PARTY.md) for license notices.

The language limitations found during this implementation, reproducers, and
fixes are recorded in [the LM0 experience report](../../docs/experience/snake.md).
