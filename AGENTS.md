# Room Controller contributor guide

## Workflow

- Split feature requests, bug fixes, and similar work into coherent chunks.
  Implement, verify, document, commit, and push each completed chunk before
  starting the next, unless the user requests a different workflow.
- After work is complete, add relevant context to this file only when necessary.
  Keep it concise; review and trim stale, redundant, or unnecessary sections.
  Link to existing documentation rather than duplicating it here.
- Inspect `git status` first and preserve unrelated user changes. Keep generated
  files and credentials out of commits. Update README/help/UI when behavior changes.
- Upload firmware only when requested. Report hardware checks separately from
  builds and host tests.

## Architecture

ESP32 NodeMCU-32S firmware controls a Blue Star IE318YKU AC over IR, measures
DS18B20 temperature, and controls a Lotus Lamp BLE strip. The local website is
served by the ESP32, not deployed separately. IR/sensor/activity LED use GPIO
23/22/2. See [README.md](README.md) for setup, commands, API, and behavior details.

- `src/main.cpp`: setup/loop, sensor, network, NVS, IR, shared serial/web AC handler.
- `src/auto_cool.h`: portable decision logic; main.cpp applies its actions.
- `src/temperature_history.h`: fixed 24-hour RAM ring, 17,280 int16 hundredths
  at five-second intervals (33.8 KiB). Missing readings preserve gaps; no flash history.
- `src/web_ui.h`: HTML/CSS/JS in PROGMEM; no browser build or external assets.
  Five-second polling stops in hidden tabs and preserves unfinished lamp edits.
- `src/lotus_lamp.*`: bounded queues and BLE worker; keep slow BLE work off the
  main loop and settings/NVS writes in the main loop. `lotus_protocol.h` is
  the portable encoder; preserve third-party attribution.
- `platformio.ini`, CMake files, `sdkconfig.defaults`, `scripts/platformio_build.py`:
  Arduino + ESP-IDF build. PlatformIO manages dependencies; the pre-build script
  disables IDF's separate component manager for both app and bootloader.
  Keep the Git IR fork's `bluestar-ie318yku` branch, not the registry replacement.

## Constraints to preserve

- Power savings are opt-in: `ROOM_CONTROLLER_POWER_SAVING` defaults to `0` in
  `src/power_config.h`. Default CPU is fixed at 240 MHz, Wi-Fi sleep is off, and
  UART wake handling is disabled. With flag `1`, preserve SDK-managed sleep/scaling
  and acquire the existing CPU/no-sleep locks before IR; do not change CPU clocks manually.
- `src/activity_led.h` is independent of power management: BLE/AC activity wins
  over the one-second on/off blink when Auto-Cool Turbo is active and AC is on.
  AC sends hold the LED for 500 ms afterward; polling does not light it.
  Keep BLE's 30-second idle and manual disconnects in both builds for phone access.
- There is no AC feedback; the physical remote and missed IR packets are invisible.
  Startup restoration sends no IR. Persisted Auto-Cool can act after a valid reading.
- Auto-Cool retains its target separately from Turbo's 16°C operation. Recovery
  cannot override manual Off/cancellation; no queued restart resumes after reboot.
  The library's Power command clamps targets below 24°C: restore the target in a
  separate packet after Power On. Preserve the tested hysteresis/trend/one-cycle contract.
- Preserve `partitions.csv` offsets and versioned NVS compatibility. AC/network
  use legacy namespace `ac-controller`; lamp settings use `lotus-lamp`.
  Deduplicate writes. Auto-Cool enabled/target settings persist, while history,
  trend, and recovery latch/countdown stay in RAM. Automatic Turbo never writes
  flash; recovery updates the existing saved AC power state at Off/On.
- Keep waits/conversions asynchronous, buffers bounded, history JSON streamed,
  and elapsed-time arithmetic safe across rollover.

## Verification

Build code/dependency changes with `pio run` (or `~/.platformio/penv/bin/pio`).
For power-related changes, also build with
`PLATFORMIO_BUILD_FLAGS=-DROOM_CONTROLLER_POWER_SAVING=1 pio run`, then rebuild
normally so the final artifact uses the default disabled setting.

Run relevant checks listed in README: native history/Auto-Cool tests,
`python3 test/test_auto_cool_integration.py`, `node test/test_web_ui.js`, and
`python3 test/test_lotus_protocol.py`. Finish with `git diff --check`.
Integration tests use fake IR/NVS; build RAM totals do not establish runtime
heap/stack safety or real IR reception. Stale CMake metadata after dependency
changes may require removing only generated `.pio/build/nodemcu-32s` and rebuilding.
