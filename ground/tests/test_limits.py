"""The ground station's copy of firmware values must match firmware/include/config.h."""

import re
from pathlib import Path

import pytest

from ground import limits

CONFIG_H = Path(__file__).resolve().parents[2] / "firmware" / "include" / "config.h"


def config_value(name: str) -> float:
    # Matches lines such as: inline constexpr float DEADBAND = 0.04f;  // comment
    pattern = rf"^inline constexpr \w+ {name} = (-?\d+(?:\.\d+)?)f?;"
    match = re.search(pattern, CONFIG_H.read_text(), re.MULTILINE)
    assert match, f"{name} is not a plain number in {CONFIG_H}"
    return float(match.group(1))


@pytest.mark.parametrize(
    "name",
    [
        "DEADBAND",
        "SUN_ENTER_MV",
        "SUN_EXIT_MV",
        "ANGLE_MIN_DEG",
        "ANGLE_MAX_DEG",
        "SIM_SUN_MIN_DEG",
        "SIM_SUN_MAX_DEG",
    ],
)
def test_limit_matches_the_firmware(name):
    assert getattr(limits, name) == config_value(name)
