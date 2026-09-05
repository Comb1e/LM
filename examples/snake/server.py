"""Local HTTP host. Movement, collisions, scoring, and transitions live in LM0."""

import argparse
from dataclasses import dataclass
from http import HTTPStatus
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import json
from pathlib import Path
import secrets
import threading
import time
from urllib.parse import urlsplit

from .native import DIRECTORY, Engine, Game, load_settings


ASSETS = {
    "/": ("index.html", "text/html; charset=utf-8"),
    "/index.html": ("index.html", "text/html; charset=utf-8"),
    "/style.css": ("style.css", "text/css; charset=utf-8"),
    "/app.js": ("app.js", "text/javascript; charset=utf-8"),
    "/icons.js": ("icons.js", "text/javascript; charset=utf-8"),
    "/favicon.svg": ("favicon.svg", "image/svg+xml"),
}


@dataclass
class Session:
    game: Game
    mode: str
    speed: int
    touched: float


class GameStore:
    def __init__(self, engine, settings):
        self.engine, self.settings = engine, settings
        self.sessions = {}
        self.lock = threading.Lock()

    def options(self, payload, session=None):
        mode = payload.get("mode", session.mode if session else "classic")
        speed = payload.get("speed", session.speed if session else self.settings["game"]["default_speed"])
        limits = self.settings["game"]
        if mode not in ("classic", "wrap"):
            raise ValueError("Unknown game mode")
        if type(speed) is not int or not limits["min_speed"] <= speed <= limits["max_speed"]:
            raise ValueError("Speed is outside the configured range")
        return mode, speed

    def expire(self):
        cutoff = time.monotonic() - self.settings["server"]["session_ttl_seconds"]
        for identifier, session in list(self.sessions.items()):
            if session.touched < cutoff:
                session.game.close()
                del self.sessions[identifier]

    def snapshot(self, identifier, session):
        return {"id": identifier, "mode": session.mode, "speed": session.speed,
                **session.game.snapshot()}

    def create(self, payload):
        with self.lock:
            self.expire()
            mode, speed = self.options(payload)
            if len(self.sessions) >= self.settings["server"]["max_sessions"]:
                raise OverflowError("All game slots are occupied; try again later")
            identifier = secrets.token_urlsafe(18)
            session = Session(self.engine.create(secrets.randbits(32), mode), mode, speed, time.monotonic())
            self.sessions[identifier] = session
            return self.snapshot(identifier, session)

    def access(self, identifier, payload=None):
        with self.lock:
            self.expire()
            session = self.sessions.get(identifier)
            if session is None:
                raise KeyError("This game has expired")
            if payload is not None:
                action = payload.get("action")
                if action == "reset":
                    mode, speed = self.options(payload, session)
                    replacement = self.engine.create(secrets.randbits(32), mode)
                    session.game.close()
                    session.game = replacement
                    session.mode, session.speed = mode, speed
                elif action == "speed":
                    _, session.speed = self.options(payload, session)
                else:
                    session.game.command(action, payload.get("direction", -1))
            session.touched = time.monotonic()
            return self.snapshot(identifier, session)

    def close(self):
        with self.lock:
            for session in self.sessions.values():
                session.game.close()
            self.sessions.clear()


class SnakeServer(ThreadingHTTPServer):
    daemon_threads = True

    def __init__(self, address, settings):
        self.settings = settings
        self.engine = Engine(settings)
        self.store = GameStore(self.engine, settings)
        try:
            super().__init__(address, Handler)
        except BaseException:
            self.engine.close()
            raise

    def get_request(self):
        connection, address = super().get_request()
        connection.settimeout(self.settings["server"]["request_timeout_seconds"])
        return connection, address

    def server_close(self):
        super().server_close()
        self.store.close()
        self.engine.close()


class Handler(BaseHTTPRequestHandler):
    def reply(self, code, body, content_type="application/json; charset=utf-8"):
        if not isinstance(body, bytes):
            body = json.dumps(body).encode()
        self.send_response(code)
        self.send_header("Content-Type", content_type)
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Cache-Control", "no-store")
        self.send_header("X-Content-Type-Options", "nosniff")
        self.send_header("Content-Security-Policy", "default-src 'self'; script-src 'self'; style-src 'self'; img-src 'self'; connect-src 'self'; frame-ancestors 'none'")
        self.end_headers()
        try:
            self.wfile.write(body)
        except (BrokenPipeError, ConnectionResetError):
            pass

    def do_GET(self):
        path = urlsplit(self.path).path
        if path == "/api/config":
            self.reply(200, {"game": self.server.settings["game"], "visual": self.server.settings["visual"]})
        elif path.startswith("/api/games/"):
            try:
                self.reply(200, self.server.store.access(path.removeprefix("/api/games/")))
            except KeyError:
                self.reply(404, {"error": "This game has expired"})
        elif path in ASSETS:
            filename, content_type = ASSETS[path]
            self.reply(200, (DIRECTORY / "static" / filename).read_bytes(), content_type)
        else:
            self.reply(404, {"error": "Not found"})

    def do_POST(self):
        origin = self.headers.get("Origin")
        if origin and urlsplit(origin).netloc != self.headers.get("Host"):
            self.reply(403, {"error": "Cross-origin requests are not accepted"})
            return
        if self.headers.get_content_type() != "application/json":
            self.reply(415, {"error": "Expected application/json"})
            return
        try:
            length = int(self.headers.get("Content-Length", "0"))
            if not 0 < length <= self.server.settings["server"]["max_body_bytes"]:
                self.reply(413, {"error": "Request body is too large or empty"})
                return
            payload = json.loads(self.rfile.read(length))
            if not isinstance(payload, dict):
                raise ValueError("Expected a JSON object")
            path = urlsplit(self.path).path
            if path == "/api/games":
                self.reply(201, self.server.store.create(payload))
            elif path.startswith("/api/games/"):
                self.reply(200, self.server.store.access(path.removeprefix("/api/games/"), payload))
            else:
                self.reply(404, {"error": "Not found"})
        except (ValueError, TypeError, UnicodeError) as error:
            self.reply(400, {"error": str(error)})
        except KeyError:
            self.reply(404, {"error": "This game has expired"})
        except OverflowError as error:
            self.reply(HTTPStatus.SERVICE_UNAVAILABLE, {"error": str(error)})

    def log_message(self, format, *args):
        if args and isinstance(args[0], str) and "/api/games" in args[0]:
            return
        super().log_message(format, *args)


def main():
    parser = argparse.ArgumentParser(description="Play Snake, powered by LM0")
    parser.add_argument("--config", type=Path)
    parser.add_argument("--host")
    parser.add_argument("--port", type=int)
    args = parser.parse_args()
    settings = load_settings(args.config)
    address = (args.host or settings["server"]["host"], args.port if args.port is not None else settings["server"]["port"])
    with SnakeServer(address, settings) as server:
        print(f"Snake ready at http://{address[0]}:{server.server_port}", flush=True)
        try:
            server.serve_forever()
        except KeyboardInterrupt:
            pass


if __name__ == "__main__":
    main()
