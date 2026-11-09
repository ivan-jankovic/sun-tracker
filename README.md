# sun-tracker

A single-axis sun tracker, built as a small model of spacecraft sun acquisition. Two photoresistors behind a divider wall act as a coarse sun sensor, a servo turns the platform, and the firmware runs a mode manager that finds the light, points at it, keeps pointing, and handles faults in a predictable way. A Python ground station shows live telemetry and sends telecommands over USB.

The scope is deliberately small (one axis, two sensors, one actuator), so the effort goes into clear behaviour, fault handling and tests. The full design, with every rule and the reasons for it, is in [docs/design.md](docs/design.md).

## How it works

- **Sensor:** with the platform facing the light, both photoresistors are lit equally. When it turns away, the wall shades one of them. The balance `e = (L − R) / (L + R)` says which way to turn, and `L + R` says whether there is a light at all.
- **Modes:** SEARCH sweeps between the end stops until the light is found, TRACKING keeps the balance near zero with a proportional controller, SAFE parks the platform, and FAULT holds it still. Every mode change is reported with its reason.
- **Faults:** a self-test at boot, and an open sensor detected during operation after 1 s. Faults latch until the `CLEAR` telecommand, and only clear if the checks pass. Faults can be injected from the ground station to show this.
- **Calibration on site:** the room light at the demo is unknown, so the `CAL` telecommand measures it and sets the light-present thresholds above it.
- **Simulation:** the core logic is plain C++ without Arduino code. It runs in host tests against a model of the light and the platform (software-in-the-loop), and in a firmware build where a simulated light replaces the photoresistors.

## Hardware

- ESP32 DevKit V1 (ESP32-WROOM-32, CH340C), powered from USB
- 2 × LDR5516 photoresistors and 2 × 4.7 kΩ resistors
- a cardboard sensor head on the servo horn: a face plate with the photoresistors, and a wall 4 cm tall and 3 cm deep between them, blackened with a marker and held with tape (details in [docs/design.md](docs/design.md#hardware))
- 4 female-to-male jumpers from the photoresistors to the breadboard
- SG90 micro servo
- 1000 µF / 25 V electrolytic capacitor
- half-size breadboard and jumper wires; no soldering

## Wiring

| Part | ESP32 pin | Notes |
|---|---|---|
| Breadboard + rail | VIN | 5 V from USB, **for the servo only** |
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

Each sensor channel is a divider: 3.3 V → photoresistor → midpoint (to the ADC pin) → 4.7 kΩ → GND. More light gives a higher voltage. Left and right are seen from the platform: facing it from the front, the left photoresistor is on your right.

**Safety:**
- The ESP32 is 3.3 V only. Never connect the 5 V rail, or anything above 3.3 V, to a GPIO or to the sensor circuit. The photoresistors are fed from D25, not from the rail.
- The capacitor is polarized: stripe (−) to GND, the longer leg to the 5 V rail.
- Analog inputs use ADC1 pins (GPIO 32-39); D34 and D35 are input-only. Avoid GPIO 0, 2, 12 and 15, which affect booting.
- Place the ESP32 shifted on the breadboard so the VIN pin row is reachable.

## Getting started

Requirements: [PlatformIO](https://platformio.org) (CLI or the VS Code extension) and [uv](https://docs.astral.sh/uv/).

```sh
# Firmware: build, flash and watch the serial output
pio run -d firmware -t upload -t monitor

# Or the simulation build: a simulated light replaces the photoresistors
pio run -d firmware -e esp32dev-sim -t upload

# Ground station (close the serial monitor first)
cd ground
uv run ground --list                  # find the port, e.g. /dev/cu.usbserial-1430
uv run ground --port <port>           # plot window, buttons, session log in ground/logs/
uv run ground --replay logs/<file>.csv  # play a recorded session back, no board needed
```

**Demo procedure:**
1. Power up. Until calibrated, the light-present thresholds are deliberately high, so in most rooms the tracker searches, finds nothing it trusts and ends in SAFE.
2. Click **SAFE** (if not already there), then with the torch off click **CAL**: the room light is measured and the thresholds set above it. Dim the room, but not fully: with the torch off, both L and R should read at least about 250 mV, or a shaded sensor can look disconnected. A lamp behind you works well.
3. Click **SEARCH** and switch the torch on: the platform sweeps, finds the light, and tracks it.
4. Click **INJECT L**: the platform holds still, and after 1 s the board reports `INJECTED_L` and goes to FAULT. **CLEAR** runs the checks and resumes the search.

In the simulation build, the **SIM SUN** slider moves the light. −90° puts it behind the platform, which counts as "torch off" for `CAL`.

## Tests

```sh
pio test -d firmware -e native   # core unit tests and SIL scenarios
cd ground && uv run pytest       # ground station
```

- **Firmware (212 tests):** one rule per test for the sensing, controller, faults, telecommand parser, mode manager and protocol, plus software-in-the-loop scenarios in the measured dark and bright rooms: acquisition, tracking a moving light, losing and finding it again, a dark room without `CAL`, open sensors, injected faults and `CLEAR`. Every scenario also checks that the platform never left its end stops.
- **Ground station (94 tests):** the strict protocol parser with corrupted lines taken from real logs, the serial link against a loopback port, the session log, the ground state, and the plot window drawn off-screen.
- **CI** ([.github/workflows/ci.yml](.github/workflows/ci.yml)) runs both test suites and builds both firmware variants on every push.

Results from the SIL scenarios:

| What | Result |
|---|---|
| open sensor or injected fault to FAULT | 1.00 s |
| no light, from SEARCH to SAFE | 12.65 s |
| worst pointing error, light moving at 10°/s | 0.64° |
| settling precision, dark / bright room | ±0.7° / ±2.4° |

On the hardware, the platform follows a torch across its whole range, finds it again after it is switched off and on, and goes to FAULT 1.0 s after a photoresistor is pulled or a fault is injected, holding still until `CLEAR`. The full list is in [docs/design.md](docs/design.md#hardware-tests).

## Repository layout

```
docs/design.md          modes, control law, faults, protocol, wiring, limitations
firmware/
  include/config.h      pins, rates, thresholds, calibration
  lib/core/             modes, controller, faults, commands, light model (no Arduino)
  src/                  Arduino glue: sensors, servo, serial
  test/                 unit tests and SIL scenarios
ground/
  src/ground/           parser, serial link, log, state, plot window
  tests/
```

## Limitations

One axis, no position feedback from the servo, and photoresistors that give direction rather than accurate angles. Room light reduces precision and biases the pointing towards its side, the counterpart of Earth albedo for a real coarse sun sensor. The details and their reasons are in [docs/design.md](docs/design.md#known-limitations).
