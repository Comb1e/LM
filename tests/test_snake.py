import ctypes
from dataclasses import replace
import json
from pathlib import Path
import random
import tempfile
import threading
import unittest
from urllib.error import HTTPError
from urllib.request import Request, urlopen

from examples.snake.native import DIRECTORY, Engine, load_settings
from examples.snake.server import GameStore, SnakeServer
from lm0.tooling import build, build_c, execute, read_module


class Fixture(ctypes.Structure):
    """Test-only access for arranging otherwise hard-to-reach board configurations."""
    _fields_ = [(name, ctypes.c_int32) for name in (
        "width", "height", "capacity", "initial", "points", "length", "direction",
        "status", "food", "score", "ticks", "reason", "wrap")]
    _fields_ += [("seed", ctypes.c_uint32), ("cells", ctypes.POINTER(ctypes.c_int32))]


class SnakeTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.settings = load_settings()
        cls.settings["game"].update(width=8, height=8, initial_length=4)
        cls.engines = [Engine(cls.settings, optimization=level) for level in ("0", "2")]

    @classmethod
    def tearDownClass(cls):
        for engine in cls.engines:
            engine.close()

    def game(self, engine, seed=42, mode="classic"):
        game = engine.create(seed, mode)
        self.addCleanup(game.close)
        return game

    def arrange(self, game, body, food, direction=1):
        fixture = Fixture.from_address(game.pointer)
        fixture.length, fixture.food, fixture.direction, fixture.status = len(body), food, direction, 1
        for index, cell in enumerate(body):
            fixture.cells[index] = cell
        return fixture

    def test_initial_state_transitions_and_reversal(self):
        for engine in self.engines:
            game = self.game(engine)
            initial = game.snapshot()
            self.assertEqual(initial["body"], [36, 35, 34, 33])
            self.assertNotIn(initial["food"], initial["body"])
            game.command("step", 0)
            self.assertEqual(game.snapshot(), initial)
            game.command("start")
            game.command("step", 3)
            self.assertEqual(game.snapshot()["body"], [37, 36, 35, 34])
            self.assertEqual(game.snapshot()["direction"], 1)
            game.command("pause")
            paused = game.snapshot()
            game.command("step", 0)
            self.assertEqual(game.snapshot(), paused)
            game.command("start")
            game.command("step", 0)
            self.assertEqual(game.snapshot()["body"][0], 29)

    def test_collisions_wrapping_and_departing_tail(self):
        for engine in self.engines:
            wall = self.game(engine)
            self.arrange(wall, [15, 14, 13, 12], 0)
            wall.command("step")
            self.assertEqual((wall.snapshot()["status"], wall.snapshot()["reason"]), ("lost", "wall"))
            lost = wall.snapshot()
            wall.command("start")
            wall.command("step")
            self.assertEqual(wall.snapshot(), lost)
            wrapped = self.game(engine, mode="wrap")
            self.arrange(wrapped, [15, 14, 13, 12], 0)
            wrapped.command("step")
            self.assertEqual(wrapped.snapshot()["body"], [8, 15, 14, 13])
            hit = self.game(engine)
            self.arrange(hit, [18, 17, 9, 10, 11, 19], 0)
            hit.command("step", 0)
            self.assertEqual((hit.snapshot()["status"], hit.snapshot()["reason"]), ("lost", "self"))
            tail = self.game(engine)
            self.arrange(tail, [18, 17, 9, 10], 0)
            tail.command("step", 0)
            self.assertEqual(tail.snapshot()["status"], "running")
            self.assertEqual(tail.snapshot()["body"], [10, 18, 17, 9])

    def test_growth_food_and_complete_board(self):
        for engine in self.engines:
            game = self.game(engine)
            self.arrange(game, [36, 35, 34, 33], 37)
            game.command("step")
            snapshot = game.snapshot()
            self.assertEqual(snapshot["body"], [37, 36, 35, 34, 33])
            self.assertEqual((snapshot["length"], snapshot["score"]), (5, 10))
            self.assertNotIn(snapshot["food"], snapshot["body"])
            almost_full = self.game(engine)
            fixture = self.arrange(almost_full, list(range(62, -1, -1)), 63)
            almost_full.command("step")
            full = almost_full.snapshot()
            self.assertEqual((full["status"], full["length"], full["food"]), ("won", 64, -1))
            self.assertEqual(set(full["body"]), set(range(64)))
            self.assertEqual(fixture.status, 4)
            almost_full.command("step")
            self.assertEqual(almost_full.snapshot(), full)

    def test_native_input_validation_and_lifecycle(self):
        for engine in self.engines:
            lib = engine.library
            for args in [(0, 8, 4, 0, 0, 10), (8, 65, 4, 0, 0, 10),
                         (8, 8, 0, 0, 0, 10), (8, 8, 5, 0, 0, 10),
                         (8, 8, 4, 0, 2, 10), (8, 8, 4, 0, 0, 0)]:
                self.assertIsNone(lib.snake_create(*args))
            self.assertEqual(lib.snake_control(None, 2, 0), -1)
            self.assertEqual(lib.snake_read(None, 0), -1)
            lib.snake_destroy(None)
            game = self.game(engine)
            for index in [-2147483648, -1, 11, 2147483647]:
                self.assertEqual(lib.snake_read(game.pointer, index), -1)
            initial = game.snapshot()
            lib.snake_control(game.pointer, 99, 0)
            self.assertEqual(game.snapshot(), initial)
            game.close()
            game.close()
            with self.assertRaises(ValueError):
                game.command("start")

    def test_seed_reproducibility_and_independent_movement_oracle(self):
        rng = random.Random(2026)
        for engine in self.engines:
            for mode in ("classic", "wrap"):
                for seed in range(20):
                    game = self.game(engine, seed, mode)
                    twin = self.game(engine, seed, mode)
                    self.assertEqual(game.snapshot(), twin.snapshot())
                    game.command("start")
                    for _ in range(200):
                        before = game.snapshot()
                        direction = rng.randrange(4)
                        chosen = before["direction"] if direction == (before["direction"] + 2) % 4 else direction
                        x, y = before["body"][0] % 8, before["body"][0] // 8
                        dx, dy = [(0, -1), (1, 0), (0, 1), (-1, 0)][chosen]
                        x, y = x + dx, y + dy
                        wall = mode == "classic" and not (0 <= x < 8 and 0 <= y < 8)
                        cell = (y % 8) * 8 + x % 8
                        eats = cell == before["food"]
                        retained = before["body"] if eats else before["body"][:-1]
                        collision = cell in retained
                        game.command("step", direction)
                        after = game.snapshot()
                        self.assertEqual(after["direction"], chosen)
                        if wall or collision:
                            self.assertEqual(after["status"], "lost")
                            self.assertEqual(after["reason"], "wall" if wall else "self")
                            self.assertEqual(after["body"], before["body"])
                            break
                        self.assertEqual(after["body"], [cell, *retained])
                        self.assertEqual(after["score"], before["score"] + (10 if eats else 0))
                        self.assertNotIn(after["food"], after["body"])

    def test_full_game_under_sanitizers(self):
        # A Hamiltonian cycle visits every cell without reversing or hitting the body.
        driver = """#include <stdint.h>
extern void *snake_create(int32_t, int32_t, int32_t, uint32_t, int32_t, int32_t);
extern void snake_destroy(void *);
extern int32_t snake_control(void *, int32_t, int32_t);
extern int32_t snake_read(void *, int32_t);
int main(void) {
    for (uint32_t seed = 0; seed < 4; ++seed) {
        void *g = snake_create(8, 8, 4, seed, 0, 10);
        snake_control(g, 0, -1);
        for (int tick = 0; tick < 5000 && snake_read(g, 0) == 1; ++tick) {
            int cell = snake_read(g, 7), x = cell % 8, y = cell / 8;
            int d;
            if (x == 0) d = y == 0 ? 1 : 0;
            else if (y % 2 == 0) d = x == 7 ? 2 : 1;
            else d = x == 1 ? (y == 7 ? 3 : 2) : 3;
            snake_control(g, 2, d);
        }
        int ok = snake_read(g, 0) == 4 && snake_read(g, 3) == 64 && snake_read(g, 4) == 600;
        snake_destroy(g);
        if (!ok) return 1;
    }
    return 0;
}
"""
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            for optimization in ("0", "2"):
                object_ = root / "snake.o"
                build(read_module(DIRECTORY / "engine.lm0"), object_, kind="object", sanitize=True, optimization=optimization)
                executable = root / "snake-test"
                build_c(driver, executable, links=[object_], sanitize=True, optimization=optimization)
                result = execute([str(executable)], 10, 10000)
                self.assertEqual(result.exit_code, 0, result.stderr)
                self.assertEqual(result.stderr, "")

    def test_session_isolation_settings_expiry_and_capacity(self):
        store = GameStore(self.engines[0], self.settings)
        self.addCleanup(store.close)
        first, second = store.create({}), store.create({"mode": "wrap"})
        store.access(first["id"], {"action": "start"})
        store.access(first["id"], {"action": "step", "direction": 0})
        self.assertEqual(store.access(second["id"]), second)
        changed = store.access(first["id"], {"action": "speed", "speed": 12})
        self.assertEqual((changed["speed"], changed["ticks"]), (12, 1))
        reset = store.access(first["id"], {"action": "reset", "mode": "wrap"})
        self.assertEqual((reset["status"], reset["score"], reset["speed"], reset["mode"]), ("ready", 0, 12, "wrap"))
        with self.assertRaises(ValueError):
            store.access(first["id"], {"action": "reset", "speed": True})
        self.assertEqual(store.access(first["id"]), reset)
        session = store.sessions[first["id"]]
        expired = session.touched - self.settings["server"]["session_ttl_seconds"] - 1
        store.sessions[first["id"]] = replace(session, touched=expired)
        with self.assertRaises(KeyError):
            store.access(first["id"])
        constrained = {**self.settings, "server": {**self.settings["server"], "max_sessions": 1}}
        limited = GameStore(self.engines[0], constrained)
        self.addCleanup(limited.close)
        limited.create({})
        with self.assertRaises(OverflowError):
            limited.create({})


class SnakeHTTPTests(unittest.TestCase):
    def test_http_protocol_and_validation(self):
        with SnakeServer(("127.0.0.1", 0), load_settings()) as server:
            thread = threading.Thread(target=server.serve_forever, daemon=True)
            thread.start()
            base = f"http://127.0.0.1:{server.server_port}"
            def request(path, data=None, headers=None):
                body = json.dumps(data).encode() if data is not None else None
                with urlopen(Request(base + path, body, headers or {"Content-Type": "application/json"}), timeout=5) as response:
                    return response.status, response.read()
            try:
                self.assertEqual(request("/")[0], 200)
                _, body = request("/api/games", {})
                game = json.loads(body)
                _, body = request("/api/games/" + game["id"], {"action": "start"})
                self.assertEqual(json.loads(body)["status"], "running")
                for payload in ([1], {"speed": False}, {"mode": ["classic"]}):
                    with self.assertRaises(HTTPError) as error:
                        request("/api/games", payload)
                    self.assertEqual(error.exception.code, 400)
                with self.assertRaises(HTTPError) as error:
                    request("/api/games", {}, {"Content-Type": "application/json", "Origin": "https://elsewhere.example"})
                self.assertEqual(error.exception.code, 403)
                with self.assertRaises(HTTPError) as error:
                    request("/../config.toml")
                self.assertEqual(error.exception.code, 404)
            finally:
                server.shutdown()
                thread.join(timeout=5)
