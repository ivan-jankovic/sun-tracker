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
- Sun present: the sum `L + R` with hysteresis, so the mode does not flicker at the threshold: enter at `SUN_ENTER_MV` *(cal, 3600)*, leave below `SUN_EXIT_MV` *(cal, 3000)*.

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

- Any fault, from any mode, goes to FAULT.
- `SAFE` from any mode except FAULT goes to SAFE (`cmd_safe`). In FAULT it is refused, because a fault must be cleared first.
- Every transition sends an `EV` event with the old mode, the new mode and the reason.

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
| `SELFTEST_SHORT` | at boot or on `CLEAR`: with D25 LOW, either channel reads above `SELFTEST_SHORT_MV` (floor + 100 mV) | the divider midpoint touches a supply, e.g. row S shorted to row L |
| `SENSOR_OPEN_L` / `SENSOR_OPEN_R` | one channel below `OPEN_MV` *(cal, floor + 60 mV)* while the other is above 500 mV, for 20 cycles (1 s) | a disconnected photoresistor; uniform darkness cannot cause it, because both channels would be low |
| `INJECTED_L` / `INJECTED_R` | the `INJECT` telecommand | tests the fault path on demand, without touching the wiring |

An injected fault makes the firmware behave as if that channel read the ADC floor, which is what a really open sensor reads. So it goes through the same detection, with the same 20-cycle debounce. When the detection fires on the injected channel, the code is `INJECTED_L` or `INJECTED_R`, and otherwise `SENSOR_OPEN_L` or `SENSOR_OPEN_R`. `CLEAR` removes the injection.

`OPEN_MV` must stay below the darkest reading of a shadowed but connected sensor at the demo, for example a torch from the side in a dimmed room. The calibration measures that reading, and the threshold goes below it with margin. Measured with the torch from the side: the shaded sensor read 374 mV or more in a dark room and 1215 mV or more in bright room light, and a removed photoresistor read exactly the floor. With D25 LOW, both channels read the floor.

Known gaps: a short between row S and a midpoint while D25 is HIGH reads as a very bright sensor, and only the next self-test (boot or `CLEAR`) catches it. Servo faults are not detectable without position feedback.

## Protocol

USB serial at 115200 baud, one message per line, fields separated by commas. Lines starting with `#` are human-readable logs and the ground station ignores them.

### Telemetry, 10 Hz

```
TM,<seq>,<t_ms>,<mode>,<l_mv>,<r_mv>,<err>,<angle_deg>,<fault>,<sun_deg>
TM,1042,52100,TRACKING,2215,2180,0.008,97.5,-,-
```

- `seq`: one counter for every `TM`, `EV`, `ACK` and `NACK` line, so the ground station can count lost lines.
- `err`: 3 decimals; `angle_deg`: 1 decimal, commanded.
- `fault`: the active fault code or `-`.
- `sun_deg`: the simulated light angle in the `esp32dev-sim` build, `-` on the real sensors.

### Events

```
EV,<seq>,<t_ms>,MODE,<from>,<to>,<reason>
EV,<seq>,<t_ms>,FAULT,<code>
```

Reasons: `boot`, `sun_found`, `sun_lost`, `no_sun`, `cmd_safe`, `cmd_search`, `cmd_clear`, `fault`.

### Telecommands

| Command | Effect |
|---|---|
| `PING` | only the acknowledgement |
| `SAFE` | to SAFE; refused in FAULT |
| `SEARCH` | from SAFE or TRACKING to SEARCH; refused in FAULT |
| `CLEAR` | in FAULT: removes injections, reruns the checks, and goes to SEARCH if they pass |
| `INJECT L` / `INJECT R` | injects an open-sensor fault on that channel |
| `SIM SUN <deg>` | moves the simulated light, `esp32dev-sim` only |

Every command gets exactly one reply: `ACK,<seq>,<command>` or `NACK,<seq>,<command>,<reason>` (`unknown`, `bad_arg`, `fault_latched`, `not_in_fault`, `not_sim`, `selftest_failed`).

## Simulation

The same core logic runs in three places:

1. **Unit tests** (`pio test -e native`): the controller, mode transitions, fault rules and command parsing, one rule per test.
2. **SIL scenarios** (also `native`): the core runs in a loop against a model of the light and the platform. Scenarios: acquisition from either end stop, tracking a moving light, light lost and found again, three sweeps without light, an open sensor, an injected fault and `CLEAR`, and that the commanded angle never leaves the end stops.
3. **`esp32dev-sim` on the board**: the real firmware and serial protocol, with the sensor readings computed from the model and the commanded angle. The ground station can be built and demonstrated before the sensors exist, and `SIM SUN` moves the light from a slider.

Light model, with `δ = sun_deg − angle_deg`:

```
visible = max(0, cos δ)
L = ambient + intensity · visible · clamp(0.5 + δ / W, 0, 1) + noise
R = ambient + intensity · visible · clamp(0.5 − δ / W, 0, 1) + noise
```

`W` *(30°)* stands for the width of the wall's shadow. Values are clamped to 0-3300 mV, and the noise is seeded, so a failing scenario repeats exactly.

## Ground station

`uv run ground --port <port>` (on macOS, the CH340C appears as `/dev/cu.wchusbserial*` or `/dev/cu.usbserial-*`). One matplotlib window, updated live:

- **Pointing:** the commanded angle over the last 30 s, plus the simulated light angle in the sim build.
- **Sensors:** L and R in mV, with the sun-present thresholds drawn in.
- **Error:** `e`, with the dead band shaded.
- **Status:** the current mode as a coloured label, the active fault, lost lines, and the last events.
- **Commands:** buttons for `SAFE`, `SEARCH`, `CLEAR`, `INJECT L`, `INJECT R`, and a slider for `SIM SUN`.

Every received line goes to a CSV log in `ground/logs/` (gitignored), so a session can be replayed. The parser is a separate module without plotting, tested with pytest.

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
- Indoors, room light raises both channels. The sun-present thresholds must sit between room light and a torch at demo distance; the demo works best in a dimmed room.
