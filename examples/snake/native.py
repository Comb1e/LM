"""Opaque ownership and ABI bindings for the LM0 game engine."""

import ctypes
from pathlib import Path
import tempfile
import tomllib

from lm0.tooling import build, read_module


DIRECTORY = Path(__file__).resolve().parent
STATES = ("ready", "running", "paused", "lost", "won")
REASONS = (None, "wall", "self")
ACTIONS = {"start": 0, "pause": 1, "step": 2}


def load_settings(path=None):
    settings = tomllib.loads(Path(path or DIRECTORY / "config.toml").read_text())
    game = settings["game"]
    for name in ("width", "height"):
        if type(game[name]) is not int or not 8 <= game[name] <= 64:
            raise ValueError(f"{name} must be an integer between 8 and 64")
    ranges = {
        "initial_length": (2, game["width"] // 2),
        "points_per_food": (1, 10000),
        "min_speed": (1, 60), "max_speed": (1, 60), "default_speed": (1, 60),
    }
    for name, (low, high) in ranges.items():
        if type(game[name]) is not int or not low <= game[name] <= high:
            raise ValueError(f"{name} must be an integer between {low} and {high}")
    if not game["min_speed"] <= game["default_speed"] <= game["max_speed"]:
        raise ValueError("Default speed must be within the configured speed range")
    for name in ("port", "max_sessions", "session_ttl_seconds", "max_body_bytes", "request_timeout_seconds"):
        value = settings["server"][name]
        if type(value) is not int or value <= 0:
            raise ValueError(f"{name} must be a positive integer")
    return settings


class Engine:
    def __init__(self, settings, *, optimization="2"):
        self.settings = settings["game"]
        self.temporary = tempfile.TemporaryDirectory(prefix="lm0-snake-")
        try:
            library = Path(self.temporary.name) / "snake.so"
            build(read_module(DIRECTORY / "engine.lm0"), library,
                  kind="shared", optimization=optimization)
            self.library = ctypes.CDLL(str(library))
        except BaseException:
            self.temporary.cleanup()
            raise
        self.library.snake_create.argtypes = [ctypes.c_int32, ctypes.c_int32,
                                              ctypes.c_int32, ctypes.c_uint32,
                                              ctypes.c_int32, ctypes.c_int32]
        self.library.snake_create.restype = ctypes.c_void_p
        self.library.snake_destroy.argtypes = [ctypes.c_void_p]
        self.library.snake_destroy.restype = None
        self.library.snake_control.argtypes = [ctypes.c_void_p, ctypes.c_int32, ctypes.c_int32]
        self.library.snake_control.restype = ctypes.c_int32
        self.library.snake_read.argtypes = [ctypes.c_void_p, ctypes.c_int32]
        self.library.snake_read.restype = ctypes.c_int32

    def create(self, seed, mode="classic"):
        if mode not in {"classic", "wrap"}:
            raise ValueError("Unknown game mode")
        config = self.settings
        pointer = self.library.snake_create(config["width"], config["height"],
                                            config["initial_length"], seed,
                                            int(mode == "wrap"), config["points_per_food"])
        if not pointer:
            raise ValueError("The LM0 engine rejected the game configuration")
        return Game(self, pointer)

    def close(self):
        self.temporary.cleanup()


class Game:
    def __init__(self, engine, pointer):
        self.engine, self.pointer = engine, pointer

    def close(self):
        if self.pointer:
            self.engine.library.snake_destroy(self.pointer)
            self.pointer = None

    def command(self, action, direction=-1):
        if not self.pointer:
            raise ValueError("Game has been closed")
        if action not in ACTIONS:
            raise ValueError("Unknown action")
        if type(direction) is not int or not -1 <= direction <= 3:
            raise ValueError("Direction must be an integer between -1 and 3")
        self.engine.library.snake_control(self.pointer, ACTIONS[action], direction)

    def snapshot(self):
        if not self.pointer:
            raise ValueError("Game has been closed")
        read = self.engine.library.snake_read
        state, direction, food, length, score, ticks, reason = (
            read(self.pointer, index) for index in range(7))
        return {"status": STATES[state], "direction": direction, "food": food,
                "length": length, "score": score, "ticks": ticks, "reason": REASONS[reason],
                "body": [read(self.pointer, index + 7) for index in range(length)]}
