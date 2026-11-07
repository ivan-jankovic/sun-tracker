"""Firmware values the ground station draws into its plots or uses for its controls.

Copied from firmware/include/config.h. tests/test_limits.py reads that file and
fails if a value here no longer matches, so the plots cannot drift from the board.
"""

DEADBAND = 0.04  # |e| below this: no motion
SUN_ENTER_MV = 4100  # boot default until CAL, applies to L + R
SUN_EXIT_MV = 3800  # boot default until CAL, applies to L + R
ANGLE_MIN_DEG = 10.0  # soft end stop
ANGLE_MAX_DEG = 170.0  # soft end stop
SIM_SUN_MIN_DEG = -180.0  # range of SIM SUN
SIM_SUN_MAX_DEG = 180.0
