from copy import deepcopy
from pathlib import Path
import tomllib


DEFAULTS = tomllib.loads(Path(__file__).with_name("defaults.toml").read_text())


def load_config(path: str | None = None) -> dict:
    config = deepcopy(DEFAULTS)
    if path:
        overrides = tomllib.loads(Path(path).read_text())
        for section, values in overrides.items():
            if section not in config or not isinstance(values, dict):
                raise ValueError(f"Unknown configuration section: {section}")
            for key, value in values.items():
                if key not in config[section]:
                    raise ValueError(f"Unknown configuration key: {section}.{key}")
                original = config[section][key]
                if type(value) is not type(original):
                    raise ValueError(f"Wrong configuration type: {section}.{key}")
                if isinstance(value, int) and value <= 0:
                    raise ValueError(f"Configuration limit must be positive: {section}.{key}")
                config[section][key] = value
    if config["compiler"]["optimization"] not in {"0", "1", "2", "3", "s"}:
        raise ValueError("optimization must be 0, 1, 2, 3, or s")
    return config
