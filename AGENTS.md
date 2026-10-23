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
- Keep the [HTTP API reference](docs/http-api.md) aligned with endpoint, command,
  field, and response semantics.
- Upload firmware only when requested. Report hardware checks separately from
  builds and host tests.

## Architecture

ESP32 NodeMCU-32S firmware controls a Blue Star IE318YKU AC over IR, measures
DS18B20 temperature, and controls a Lotus Lamp BLE strip. The local website is
served by the ESP32, not deployed separately. IR/sensor/activity LED use GPIO
23/22/2. See [README.md](README.md) for setup, commands, API, and behavior details.

- `src/main.cpp` delegates to `controller_app.*`: composition, scheduling,
  history, state revision, and activity hooks. `serial_console.*` owns prompts
  and bounded input; `board_config.h` holds pins. `power_runtime.*` retains the
  existing policy without tuning.
- Reusable libraries use `lib/<name>/src`: `ControllerCore` (portable Auto-Cool,
  history/API, reading snapshot, LED, power flag), `ControllerSupport` (JSON/input),
  `AcController`, `TemperatureSensor`, `ControllerNetwork`, `ControllerWeb`, and
  `LotusLamp`. See [source map](README.md#firmware-components).
- Controllers/settings/history/NVS are main-loop owned. HTTP request/response
  operations stay on AsyncTCP (core 0); copied bounded requests/replies cross
  `ControllerWeb`'s dispatch bridge. BLE objects stay on LotusLamp's worker;
  bounded queues/atomics connect it to the loop. No reverse library dependencies
  on app globals; no copying large owners or destroying live task/callback owners.
- `web/` contains the responsive page/styles, `theme.js` (head bootstrap),
  `history_chart.js` (cached history rendering/ranges), and `controller.js`
  (local transport/commands). `scripts/embed_web.py` emits only
  `$BUILD_DIR/generated/web_ui.h`; only `controller_web.cpp` includes it. The
  pre-build hook generates before scanning and preserves `IDF_COMPONENT_MANAGER=0`
  for app/bootloader. Optional SRI-pinned uPlot loads from a CDN asynchronously;
  controls and the local canvas fallback must work without it. Chart code must
  not fetch history or create polling loops. No separate site deployment.
- Keep dependency/toolchain pins and the Git IR fork's `bluestar-ie318yku` branch.
  PlatformIO compiles library sources; do not register them again in src CMake.

## Constraints to preserve

- CPU scaling/automatic light sleep are opt-in: `ROOM_CONTROLLER_POWER_SAVING`
  defaults to `0` in `lib/ControllerCore/src/power_config.h`. Default CPU is fixed
  at 240 MHz; automatic light sleep and UART wake handling are disabled. Wi-Fi
  always uses
  `WIFI_PS_MIN_MODEM` in both Arduino and driver configuration paths: the pinned
  SDK requires it before BLE startup for coexistence. With flag `1`, preserve
  SDK-managed sleep/scaling and acquire the existing CPU/no-sleep locks before
  IR; do not change CPU clocks manually.
- `lib/ControllerCore/src/activity_led.h` is independent of power management:
  BLE/AC activity wins
  over the one-second on/off blink when Auto-Cool Turbo is active and AC is on.
  AC sends hold the LED for 500 ms afterward; polling does not light it.
  Keep BLE's 30-second idle and manual disconnects in both builds for phone access.
- There is no AC feedback; the physical remote and missed IR packets are invisible.
  Startup restoration sends no IR. Persisted Auto-Cool can act after a valid reading.
- Auto-Cool retains its target separately from Turbo's 16°C operation. Recovery
  cannot override manual Off/cancellation; no queued restart resumes after reboot.
  The library's Power command clamps targets below 24°C: restore the target in a
  separate packet after Power On. Preserve hysteresis, the 20-minute cooling
  trial, capped repeat-recovery backoff, and Turbo's 20-minute/5-minute timers.
  Automatic IR uses three transmissions two seconds apart; queued packet pairs
  preserve Power On/target ordering and cannot outlive cancellation/new commands.
- Preserve `partitions.csv` offsets and versioned NVS compatibility. AC/network
  use legacy namespace `ac-controller`; lamp settings use `lotus-lamp`.
  Deduplicate writes. Auto-Cool enabled/target settings persist, while history,
  observation, and recovery attempts/countdown stay in RAM. Automatic Turbo never writes
  flash; recovery updates the existing saved AC power state at Off/On.
- Sensor freshness is resolved in `TemperatureSensor::snapshot()` from monotonic
  acquisition time; discovery examines one address per turn and preserves parasite
  power through conversion. Keep waits/conversions asynchronous, buffers bounded,
  history JSON streamed,
  and elapsed-time arithmetic safe across rollover.

## Verification

Build code/dependency changes with `pio run` (or `~/.platformio/penv/bin/pio`).
For power-related changes, also build with both
`-DCONFIG_ASYNC_TCP_RUNNING_CORE=0 -DROOM_CONTROLLER_POWER_SAVING=1` in
`PLATFORMIO_BUILD_FLAGS`, then rebuild normally so the final artifact uses the
default disabled setting.

Host-check inventory and deferred path/interface migration are in
[test/README](test/README) and [README](README.md#host-checks-and-deferred-migration).
The October 2026 refactor explicitly deferred tests and hardware checks; do not
claim the historical extraction harnesses verify the new classes. Finish with
`git diff --check`. Builds do not establish runtime heap/stack safety, electrical
stability, or IR reception. If dependency metadata is stale, remove only the
generated `.pio/build/nodemcu-32s` directory and rebuild.
