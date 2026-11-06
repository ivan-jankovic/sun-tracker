# sun-tracker design

A single-axis model of spacecraft sun acquisition: find the light source, point at it, keep pointing, and handle faults in a predictable way. Every number marked *(cal)* is a starting value, to be calibrated on the hardware and kept in `firmware/include/config.h`.

## Hardware

- ESP32 DevKit V1 (ESP32-WROOM-32, CH340C), USB-powered.
- Coarse sun sensor: two LDR5516 photoresistors on the platform, one on each side of a matt-black divider wall 3-4 cm tall. When the platform points at the light, both are lit equally. When it turns away, the wall shadows one of them.
- Actuator: SG90 micro servo (180°), no position feedback.
- 1000 µF / 25 V low-ESR capacitor across the servo supply, against the current spikes of the servo.
- Half-size breadboard; only the photoresistors and the wall sit on the moving platform.

### Wiring

| Part | ESP32 pin | Notes |
|---|---|---|
| Breadboard + rail | VIN | 5 V from USB, for the servo only |
| Breadboard − rail | GND | common ground |
| Sensor supply (row S) | D25 | driven HIGH = 3.3 V; LOW during the self-test |
| Left photoresistor | row S → row L | |
| Right photoresistor | row S → row R | |
| Left divider (row L) | D34 | ADC1; 4.7 kΩ from row L to GND |
| Right divider (row R) | D35 | ADC1; 4.7 kΩ from row R to GND |
| Servo brown | − rail | |
| Servo red | + rail | never to a GPIO |
| Servo orange | D26 | 50 Hz PWM, 3.3 V logic |
| Capacitor | + rail / − rail | stripe (−) to GND, next to the servo wires |

Each divider: 3.3 V → photoresistor → midpoint (ADC) → 4.7 kΩ → GND. More light means lower resistance and a higher voltage. 4.7 kΩ keeps a torch close to the sensor below ADC saturation; 10 kΩ saturates near 3.1 V. A disconnected photoresistor leaves the midpoint pulled to GND, so it reads the ADC floor (below).

## Sensing

- Each control cycle reads both channels with `analogReadMilliVolts()`, averaging 8 samples per channel.
- ADC floor: 0 V reads as `ADC_FLOOR_MV` = 142 mV (raw 0), measured with D34 and D35 tied to GND. Below about 150 mV the ADC cannot tell voltages apart, so every threshold near zero is set relative to the floor. Another board needs this measured again.
- The right channel is multiplied by `R_GAIN` *(cal, 1.0)* to cancel the mismatch between the two photoresistors: with both equally lit, `L ≈ R`. Measured with a torch from the front: L and R within 1 %.
- Pointing error: `e = (L − R) / (L + R)`, in −1…+1, and 0 when the sum is below `NO_LIGHT_SUM_MV` (twice the floor + 100 mV: no light, no direction).
- Sign convention: `e > 0` means the left side is brighter, so the light is to the left, and the controller increases the angle. If the servo is mounted the other way round, `DIRECTION` in `config.h` flips it.
- Sun present: the sum `L + R` with hysteresis, so the mode does not flicker at the threshold: enter at the enter threshold, leave below the exit threshold.
- The demo room is not known in advance, and room light alone ranged from 573 mV (dark room) to 3454 mV (bright room). So the thresholds are set on site with `CAL`, in SAFE with the torch off: enter = room + `SUN_ENTER_MARGIN_MV` *(cal, 600)*, exit = room + `SUN_EXIT_MARGIN_MV` *(cal, 300)*. The margins are set by the bright room, where the torch from the front added +1160 mV (+3400 mV in the dark room). `CAL` is refused (`cal_out_of_range`) if the enter threshold would be above `SUN_ENTER_MAX_MV` *(5600)*, and the old thresholds stay.
- Until `CAL`, the boot defaults `SUN_ENTER_MV` *(4100)* and `SUN_EXIT_MV` *(3800)* apply. They are high on purpose: without `CAL`, a dark room ends in SAFE (`no_sun`) instead of the room light being tracked as the sun. Demo procedure: boot → SAFE → `CAL` with the torch off → `SEARCH`.

## Actuator

- LEDC PWM at 50 Hz, 16-bit, driven directly without a servo library.
- Angle to pulse width is linear: 0° = `SERVO_MIN_US` *(cal, 500)*, 180° = `SERVO_MAX_US` *(cal, 2400)*.
- Soft end stops at `ANGLE_MIN_DEG` *(cal, 10)* and `ANGLE_MAX_DEG` *(cal, 170)*. The controller can never command outside them.
- The SG90 reports no position, so the commanded angle is treated as the actual one. Telemetry reports it as `angle_deg` (commanded).
- At boot the first pulse commands 90° (centre). The platform is mounted on the horn at that position, with the wall facing forward.

## Modes

```
            self-test ok
   boot ──────────────────► SEARCH ──── sun_found ───► TRACKING
     │                       │  ▲  ◄──── sun_lost ────────┘
     │ self-test failed      │  │
     ▼                no_sun │  │ SEARCH command
   FAULT                     ▼  │
     │                       SAFE
     └── CLEAR, checks ok ──► SEARCH

   any mode ───────────── fault ─────────► FAULT
   any mode but FAULT ─── SAFE command ──► SAFE
```

| Mode | What it does | Leaves when |
|---|---|---|
| **SEARCH** | sweeps between the end stops at `SEARCH_RATE_DEG_S` *(45)* | sun present for 3 cycles → TRACKING (`sun_found`); `SEARCH_MAX_SWEEPS` *(3)* full sweeps without sun → SAFE (`no_sun`) |
| **TRACKING** | runs the controller | sun absent for 10 cycles (0.5 s) → SEARCH (`sun_lost`) |
| **SAFE** | parks at 90° and stays passive | `SEARCH` command → SEARCH |
| **FAULT** | holds the last angle, no motion | `CLEAR` command, and the self-test and sensor checks pass → SEARCH |

- Any fault, from any mode, goes to FAULT. In the same cycle, a fault wins over every other rule.
- `SAFE` from any mode except FAULT goes to SAFE (`cmd_safe`). In FAULT it is refused, because a fault must be cleared first.
- Every transition sends an `EV` event with the old mode, the new mode and the reason.
- `BOOT` is a pseudo-mode that exists only until the boot self-test has run, so the first event reads `EV,…,MODE,BOOT,SEARCH,boot` (or `BOOT,FAULT,fault`). It never appears in `TM`.
- A command that would not change the mode, `SAFE` in SAFE or `SEARCH` in SEARCH, is acknowledged and sends no event, because no transition happened.
- `INJECT` is refused in FAULT (`fault_latched`). `CAL` is accepted only in SAFE (`not_in_safe`), where the platform is parked and still.
- A failed `CLEAR` stays in FAULT and is refused with `selftest_failed`.
- A full sweep is one run from end stop to end stop. The partial run from the angle where SEARCH started does not count. The sweep starts upwards, or downwards if it is already at the top end stop.
- Entering a mode restarts its counters: after `sun_lost` or `CLEAR`, SEARCH again needs the sun for 3 cycles in a row.
- A new mode acts in the same cycle: after `sun_found` the first controller step happens at once, and SAFE commands 90° at once, without rate limiting.
- Order of lines: a command's `ACK` or `NACK` comes first, then the events it caused. A detected fault sends `EV FAULT` first, then `EV MODE` (cause before effect).

## Controller (TRACKING)

Per control cycle (20 Hz, 50 ms):

1. If `|e| < DEADBAND` *(cal, 0.04)*, do nothing. This stops the servo from twitching around the target.
2. Otherwise `step = DIRECTION · KP · e` degrees, with `KP` *(cal, 10)*.
3. Clamp the step to `MAX_RATE_DEG_S` *(90)* × 0.05 s = ±4.5° per cycle.
4. Clamp the new angle to the soft end stops.

A proportional controller is enough: the servo itself closes the position loop, so the firmware only decides where to point. Reaching an end stop while the light is still further out is not a fault. The platform waits at the stop, still in TRACKING.

## Fault detection

| Code | Condition | Why |
|---|---|---|
| `SELFTEST_SHORT` | at boot or on `CLEAR`: with D25 LOW, either channel reads above `SELFTEST_SHORT_MV` (floor + 100 mV) | the divider midpoint touches a supply other than D25, e.g. the 3V3 pin |
| `SENSOR_OPEN_L` / `SENSOR_OPEN_R` | one channel below `OPEN_MV` *(cal, floor + 60 mV)* while the other is above 500 mV, for 20 cycles (1 s) | a disconnected photoresistor; uniform darkness cannot cause it, because both channels would be low |
| `INJECTED_L` / `INJECTED_R` | the `INJECT` telecommand | tests the fault path on demand, without touching the wiring |

An injected fault makes the firmware behave as if that channel read the ADC floor, which is what a really open sensor reads. So it goes through the same detection, with the same 20-cycle debounce. When the detection fires on the injected channel, the code is `INJECTED_L` or `INJECTED_R`, and otherwise `SENSOR_OPEN_L` or `SENSOR_OPEN_R`. Only one channel can be injected at a time, and a second `INJECT` replaces the first: with both channels at the floor, the open-sensor rule could never fire. In a dark room the injection stays pending until the other channel is above 500 mV.

While the open-sensor condition holds, even before the debounce is complete, the readings are not trusted and the platform holds still in TRACKING and SEARCH. Without this, the SIL found that an open sensor in the dark room was never detected while the light was in view:
1. The controller saw `e ≈ −0.9` and turned away from the light.
2. The other channel fell into the wall's shadow, below 500 mV, which restarted the debounce.
3. `sun_lost`, then SEARCH found the light again and the loop repeated, with the condition never holding for 20 cycles.

With the hold, the fault latches after exactly 1 s in both the dark and the bright room, with the platform still pointed where it was. Holding cannot get stuck: either the condition lasts for the debounce (FAULT), or it ends and the motion continues. In the bright room, the open channel also takes `L + R` below the exit threshold, so `sun_lost` comes 0.5 s before the fault.

`CLEAR` in FAULT:
1. Removes the injection, even if the checks then fail.
2. Runs the self-test (D25 LOW).
3. If it passes, takes one reading with D25 HIGH and applies the open-sensor rule once, without the 20-cycle debounce.
4. If both pass: `ACK`, the fault is cleared, the debounce restarts, and the mode goes to SEARCH (`cmd_clear`).
5. If a check fails: `NACK selftest_failed`, the mode stays FAULT, and the active fault code becomes what the check found, with an `EV FAULT` if the code changed.

`OPEN_MV` must stay below the darkest reading of a shadowed but connected sensor at the demo, for example a torch from the side in a dimmed room. The calibration measures that reading, and the threshold goes below it with margin. Measured with the torch from the side: the shaded sensor read 374 mV or more in a dark room and 1215 mV or more in bright room light, and a removed photoresistor read exactly the floor. With D25 LOW, both channels read the floor.

Known gaps: a short between row S and a midpoint is not detected. With D25 HIGH that channel reads as a fully lit sensor. With D25 LOW, row S is at 0 V too, so the channel reads the floor like a healthy one, and the self-test passes. Servo faults are not detectable without position feedback.

## Protocol

USB serial at 115200 baud, one message per line, fields separated by commas. Lines starting with `#` are human-readable logs and the ground station ignores them.

### Telemetry, 10 Hz

```
TM,<seq>,<t_ms>,<mode>,<l_mv>,<r_mv>,<err>,<angle_deg>,<fault>,<sun_deg>
TM,1042,52100,TRACKING,2215,2180,0.008,97.5,-,-
```

- Lines from the firmware end with `\n`.
- `seq`: one counter for every `TM`, `EV`, `ACK` and `NACK` line, so the ground station can count lost lines. It starts at 0 at boot, so a jump back to 0 means the board has restarted.
- `err`: 3 decimals; `angle_deg`: 1 decimal, commanded. Values are rounded before printing, so a tiny negative value prints as `0.000`, never `-0.000`.
- `l_mv`, `r_mv`: what the firmware works with, so `r_mv` is after `R_GAIN`, and an injected channel shows the ADC floor.
- `fault`: the active fault code or `-`.
- `sun_deg`: the simulated light angle in the `esp32dev-sim` build, `-` on the real sensors.

### Events

```
EV,<seq>,<t_ms>,MODE,<from>,<to>,<reason>
EV,<seq>,<t_ms>,FAULT,<code>
EV,<seq>,<t_ms>,CAL,<room_mv>,<enter_mv>,<exit_mv>
```

- `CAL`: the result of a successful `CAL` telecommand. `room_mv` is the measured room-light sum `L + R`, and `enter_mv` and `exit_mv` are the new sun-present thresholds.

Reasons: `boot`, `sun_found`, `sun_lost`, `no_sun`, `cmd_safe`, `cmd_search`, `cmd_clear`, `fault`.

### Telecommands

| Command | Effect |
|---|---|
| `PING` | only the acknowledgement |
| `SAFE` | to SAFE; refused in FAULT |
| `SEARCH` | from SAFE or TRACKING to SEARCH; refused in FAULT |
| `CLEAR` | in FAULT: removes injections, reruns the checks, and goes to SEARCH if they pass |
| `INJECT L` / `INJECT R` | injects an open-sensor fault on that channel |
| `CAL` | in SAFE: measures the room light and sets the sun-present thresholds from it (the torch must be off) |
| `SIM SUN <deg>` | moves the simulated light, `esp32dev-sim` only, in any mode (it moves the environment, not the platform); −180…180, so −90 puts the light behind the platform |

Every command gets exactly one reply: `ACK,<seq>,<command>` or `NACK,<seq>,<command>,<reason>` (`unknown`, `bad_arg`, `fault_latched`, `not_in_fault`, `not_in_safe`, `cal_out_of_range`, `not_sim`, `selftest_failed`).

- Syntax: upper-case words separated by spaces, at most 32 characters. Whitespace at either end, including the CR of a CRLF line end, is ignored.
- `unknown`: not a command, including lower case and over-long lines. `bad_arg`: a known command with a missing, extra or invalid argument.
- `<deg>` is a plain decimal number such as `45`, `-90` or `12.5`. Exponents, hex, `nan` and `inf` are refused.
- `<command>` in the reply echoes the received line with commas and unprintable characters replaced by `_`, so the reply always has the same number of fields.

## Simulation

The same core logic runs in three places:

1. **Unit tests** (`pio test -e native`): the controller, mode transitions, fault rules and command parsing, one rule per test.
2. **SIL scenarios** (also `native`): the core runs in a loop against a model of the light and the platform, in the measured dark and bright rooms. Scenarios: acquisition near either end stop, a light beyond an end stop, tracking a moving light, light lost and found again, three sweeps without light, a dark room without `CAL`, an open sensor in both rooms, an injected fault and `CLEAR`, a short at boot, and repeatability for a given seed. Every scenario also checks that the commanded angle never left the end stops.
3. **`esp32dev-sim` on the board**: the real firmware and serial protocol, with the sensor readings computed from the model and the commanded angle. The ground station can be built and demonstrated before the sensors exist, and `SIM SUN` moves the light from a slider.

Light model, with `δ = sun_deg − angle_deg`:

```
visible = max(0, cos δ)
L = ambient_l + intensity · visible · clamp(0.5 + δ / W, 0, 1) + noise
R = ambient_r + intensity · visible · clamp(0.5 − δ / W, 0, 1) + noise
```

- `W` *(30°)* stands for the width of the wall's shadow.
- Values are clamped to the ADC floor (142 mV) and 3300 mV, so the model reads like the real ADC. An open channel reads exactly the floor.
- The noise is uniform and comes from a seeded xorshift32 generator, so a failing scenario repeats exactly, on the host and on the ESP32.
- Room presets from the measurements:
  - dark room: ambient 283 / 290 mV, intensity 3400 mV
  - bright room: ambient 1727 / 1727 mV, intensity 1160 mV

  The real bright room read 2166 / 1288 mV, with the light from the left. The model adds light in millivolts and ignores the photoresistor's lower response in bright light, so that imbalance would cause a much larger pointing bias in the model than on the real sensor. The model spreads it evenly instead.

Measured in the SIL:

| What | Result |
|---|---|
| open sensor or injection to FAULT, dark and bright room | 1.00 s (the debounce) |
| SEARCH to SAFE without light, from 90° | 12.65 s |
| worst pointing error, light moving at 10°/s | 0.64° |
| settling precision, dark / bright room | ±0.7° / ±2.4° (the dead band in degrees) |

## Ground station

`uv run ground --port <port>` (on macOS, the CH340C appears as `/dev/cu.wchusbserial*` or `/dev/cu.usbserial-*`). One matplotlib window, updated live:

- **Pointing:** the commanded angle over the last 30 s, plus the simulated light angle in the sim build.
- **Sensors:** L and R in mV, with the sun-present thresholds drawn in.
- **Error:** `e`, with the dead band shaded.
- **Status:** the current mode as a coloured label, the active fault, lost lines, and the last events.
- **Commands:** buttons for `SAFE`, `SEARCH`, `CLEAR`, `INJECT L`, `INJECT R`, and a slider for `SIM SUN`.

Every received and every sent line goes to a CSV log in `ground/logs/` (gitignored), one file per session, named after its start time, so a session can be replayed.
- Columns: `host_time` (seconds since the epoch, 3 decimals), `dir` (`rx` from the board, `tx` to the board), `line`.
- The log keeps the raw lines, not parsed values, so an improved parser can read old logs again.

The parser is a separate module without plotting, tested with pytest. It is strict: a value the protocol does not define is an error, and a line that cannot be parsed counts as lost.

The serial link reads in a background thread, so the plot window stays responsive. It splits lines only on complete line ends, and a failing port (cable pulled) is reported rather than silently stopping the display.

## Repository layout

```
CLAUDE.md
docs/design.md
firmware/
  platformio.ini        environments: esp32dev, esp32dev-sim, native
  include/config.h      pins, rates, thresholds, calibration
  lib/core/             modes, controller, faults, commands, light model (no Arduino)
  src/                  Arduino glue: sensors, servo, serial
  test/                 unit tests and SIL scenarios
ground/
  pyproject.toml, uv.lock
  src/ground/           parser, serial link, plots
  tests/
.github/workflows/ci.yml  native tests, both firmware builds, ground tests
```

## Known limitations

- One axis only. A real coarse sun sensor covers the whole sphere with several sensor faces.
- The SG90 has no position feedback, so a stalled or slipping servo goes unnoticed.
- Photoresistors are slow (about 30 ms) and non-linear; good enough for direction, not for accurate angles.
- Indoors, room light raises both channels. `CAL` puts the sun-present thresholds above the room light on site, but the torch must still stand out: in the bright room it added only +1160 mV from the front. In bright light the photoresistors are already low in resistance, so extra light changes the voltage little. The demo works best in a dimmed room, with the torch close.
- Room light makes the error smaller for the same angle, so the dead band covers more degrees: the platform settles within ±0.7° in the dark room, but only within ±2.4° in the bright room.
- Room light from one side biases the pointing towards it: with room light alone, `e` was 0.25. This is the counterpart of Earth albedo for a real coarse sun sensor. Subtracting the room light per channel would only be correct at the angle where `CAL` measured it, because the wall shades differently at every angle, so it is not done. The bias shrinks as the torch gets brighter relative to the room.
