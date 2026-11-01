# CLAUDE.md

Persistent rules for this repository. Modes, control law, fault rules, protocol and wiring: see `docs/design.md`.

## What this project is

`sun-tracker`: a single-axis sun tracker, built as a small model of spacecraft sun acquisition. Two photoresistors with a divider wall between them act as a coarse sun sensor, an SG90 servo turns the platform, and the firmware runs a mode manager (SAFE, SEARCH, TRACKING, FAULT). A Python ground station shows live telemetry as plots and sends telecommands over USB serial. The scope is deliberately small: one axis, two sensors, one actuator. Clear behaviour, fault handling and tests matter more than feature count.

## Stack

- Firmware: C++ (Arduino framework) on a classic ESP32 DevKit V1 (ESP32-WROOM-32, CH340C), built with PlatformIO in `firmware/`, platform `espressif32@8.0.0`.
- Core logic (modes, controller, fault detection, command parsing) is plain C++ with no Arduino includes, in `firmware/lib/core/`, so it builds and is tested on the host.
- Tests: PlatformIO `native` environment with Unity (`pio test -e native`), including SIL scenarios against a simulated sun.
- Ground station: Python 3.14, `pyserial` and `matplotlib`, pytest. Managed with **uv** (`pyproject.toml` + `uv.lock`) in `ground/`.
- No WiFi, no server, no secrets: everything goes over the USB cable.

## Commands

- Firmware build: `pio run -d firmware` (the VS Code extension's CLI is `~/.platformio/penv/bin/pio`)
- Firmware with the simulated sun: `pio run -d firmware -e esp32dev-sim`
- Upload and monitor: `pio run -d firmware -t upload -t monitor` (close any open serial monitor or ground station first)
- Core and SIL tests: `pio test -d firmware -e native`
- Ground station: `cd ground && uv run ground --port <serial port>`
- Ground tests: `cd ground && uv run pytest`
- Add a Python dependency: `cd ground && uv add <pkg>` (dev: `uv add --dev <pkg>`)

## Conventions

- The core logic never touches hardware. Sensors and the servo sit behind small interfaces, so the simulator and the tests use the same code paths as the board, and the photoresistors can be swapped for another sensor later.
- Every mode change has a reason, and the firmware reports it as an event. No silent transitions.
- Faults latch: the firmware leaves FAULT only on the `CLEAR` telecommand, after its checks pass.
- Tuning values and calibration constants live in `firmware/include/config.h`, never scattered as magic numbers.
- Angles are in degrees, voltages in millivolts, times in milliseconds since boot. Units go in names where they are not obvious (`angle_deg`, `l_mv`).
- Telemetry and commands follow the protocol in `docs/design.md`. Change the document and the ground station parser together with the firmware.
- Write or update tests with every change to the core logic or the protocol.
- Small, meaningful commits with imperative messages. The Git history should read as a clear story of how the project was built. Always ask before committing or pushing.

## Hardware safety rules (important)

- The ESP32 is 3.3 V only. Never connect 5 V or anything above 3.3 V to a GPIO or to the sensor circuit.
- Power comes from USB only. The breadboard's + rail carries 5 V from VIN **for the servo only**: only the servo's red wire and the capacitor's + leg go there. The photoresistors are fed from D25 (3.3 V when HIGH), never from the rail.
- The 1000 µF / 25 V electrolytic capacitor is polarized: stripe (−) to GND, longer leg to the 5 V rail.
- Use ADC1 pins (GPIO 32-39) for analog input. The sensors are on D34 and D35.
- Avoid GPIO 0, 2, 12, 15 (boot-related). VN/VP (36, 39) and 34/35 are input-only.
- The ESP32 sits shifted on the breadboard so the pin row with VIN is reachable (USB end first: VIN, GND, D13, D12, D14, D27, D26, D25, D33, D32, D35, D34, VN, VP, EN).
- No soldering anywhere in this project.

## Working agreements

- Wiring and pin assignments are documented in `docs/design.md` (and later in the README wiring table). Any hardware suggestion names exact pins and voltage levels, with a one-line reason.
- Breadboard wiring and flashing are done by hand on the physical board, so don't assume access to the device. Give exact pin numbers and wiring tables.
- Ask before large structural changes. Prefer simple solutions and avoid unneeded dependencies.
