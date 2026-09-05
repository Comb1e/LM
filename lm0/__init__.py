"""LM0 compiler public interface."""

from .parser import parse
from .verify import verify
from .emit import emit_c

__all__ = ["parse", "verify", "emit_c"]
__version__ = "0.1.0"
