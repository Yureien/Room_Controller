# Room Controller contributor guide

## Workflow

- Break feature requests, bug fixes, refactors, and similar work into small,
  coherent chunks that can be reviewed and verified independently.
- Finish the implementation, relevant checks, and documentation for each chunk,
  then commit and push it before starting the next chunk. Use descriptive commit
  messages and the current branch's upstream. Follow explicit user instructions
  if they request a different commit or push workflow.
- Inspect `git status` first. Preserve unrelated user changes and avoid committing
  generated files, credentials, or edits outside the requested scope.
- Explain assumptions and meaningful behavior changes. Keep the README, serial
  help, web UI, and API behavior consistent when controls or intervals change.

## Project and hardware

This is ESP32 firmware, not a separately hosted website. A NodeMCU-32S serves
the local control page at `http://esp32.local/` (or its saved hostname/IP).
It controls a Blue Star IE318YKU AC over IR and a Lotus Lamp strip over BLE,
and measures room temperature with a DS18B20.

- IR transmitter: GPIO 23. DS18B20: GPIO 22 with an external pull-up.
- Activity LED: GPIO 2, active-high on the reference board.
- Serial/upload: `/dev/ttyUSB0`, 115200 baud, 8-N-1.
- PlatformIO environment: `nodemcu-32s`, ESP32 platform 7.1.3, Arduino 2.0.17
  built with ESP-IDF 4.4.7. The board has 4 MiB flash.
- IR dependency: `https://github.com/Yureien/IRremoteESP8266.git#bluestar-ie318yku`.
  Keep the experimental Blue Star fork; the upstream registry library is not
  an interchangeable replacement. An adjacent checkout is optional and is
  referenced only by the VS Code workspace's IR receiver example.

## Code map

- `src/main.cpp`: setup/loop, asynchronous sensing, serial and HTTP command
  validation, Wi-Fi/mDNS, NVS, AC state/IR, Auto-Cool integration, power locks.
  Serial and web AC commands share `executeAcCommand()`.
- `src/auto_cool.h`: allocation-free, hardware-independent decision controller.
  It returns actions; `serviceAutoCool()` applies them and sends IR.
- `src/temperature_history.h`: fixed RAM ring of signed 16-bit hundredths of
  a degree. Five-second cadence, 17,280 slots, about 34,568 bytes including
  bookkeeping, covering 24 hours. Missing readings occupy gap slots. History
  is not saved to flash and starts empty after reboot.
- `src/web_ui.h`: complete HTML/CSS/JS in a `PROGMEM` raw string, including the
  canvas temperature chart. No browser build step, CDN, or external assets.
  State and history refresh every five seconds; hidden tabs stop polling.
  Preserve unfinished lamp edits and avoid automatically retrying commands.
- `src/lotus_lamp.cpp` / `.h`: main-loop settings/status plus a separate BLE
  worker and bounded queues. Bluetooth starts on demand and shuts down after
  30 seconds idle. Keep slow discovery/connection work off the main loop and
  keep NVS writes in the main loop. The worker has a 12,288-byte task stack.
- `src/lotus_protocol.h`: portable Lotus packet parsing/encoding. Keep upstream
  attribution and `THIRD_PARTY_NOTICES.md` when changing the port.
- `CMakeLists.txt`, `src/CMakeLists.txt`, `sdkconfig.defaults`, `platformio.ini`,
  `scripts/platformio_build.py`: hybrid SDK build configuration. The pre-build
  script disables IDF's component manager for both app and bootloader;
  PlatformIO installs the dependencies.
- `partitions.csv`: preserves existing NVS/OTA offsets. Each app slot is
  `0x140000` bytes. Do not casually change partition offsets or saved layouts.

## Behavioral constraints

- Control state describes the last requested settings. There is no AC feedback:
  the physical remote is invisible and IR reception cannot be confirmed.
- Startup restoration itself sends no IR or lamp commands. Persisted Auto-Cool
  may subsequently act when a valid sensor reading arrives and saved AC state
  is On in Cool mode. Never enable it by turning an off AC on.
- Auto-Cool remembers its target separately from Turbo's 16°C display/operation.
  Turbo starts strictly above target +1.5°C and stops strictly below target
  -0.5°C. Enabling requires AC On/Cool, a fresh sensor, Energy Save/Sleep off.
- Recovery needs five continuous minutes above target +1.5°C with Turbo active,
  a rise of at least 0.125°C between the first/last 30-second averages, and a
  rising recent average. Missing/stale samples and target edits restart the
  trend window. A hot but flat or cooling room must not cause a power cycle.
- Recovery sends Off, waits two minutes without blocking, then sends On and
  restores the target. The IR library clamps temperatures below 24°C on power
  changes; send target restoration separately after the Power On packet so
  its command code is preserved.
- Allow one recovery until a valid reading is strictly below target. Manual
  power changes and Auto-Cool toggles do not clear this RAM latch. Manual Off,
  disabling Auto-Cool, or sensor loss during the wait cancels automatic restart.
  Reject early Power On/other AC changes during the wait; Power on the page
  sends Off to cancel. Lamp controls remain usable.
- Sensor loss outside recovery turns Turbo off and pauses automation. Manual
  power/mode/fan/capacity/Turbo/Energy Save/Sleep/raw-state changes disable
  Auto-Cool; target, swing, and display-light edits can preserve it.
- NVS namespace `ac-controller` retains `network`, `ac-state`, and the eight-byte
  `auto-cool` enabled/target record. Lamp settings use `lotus-lamp` / `settings`.
  Validate saved versions/lengths/ranges and deduplicate unchanged writes.
  Automatic Turbo changes never write flash. Recovery updates the existing
  saved AC power state at Off/On so a reboot during the wait leaves the AC off;
  countdown, trend, and recovery lockout are not persisted.
- Preserve SDK-managed light sleep and CPU scaling. IR sends must acquire the
  existing 240 MHz/no-sleep locks through `boostCpu()` / `sendAcPacket()`.
  Do not manually change CPU frequency. Keep conversions and timer waits
  asynchronous, and use unsigned elapsed-time arithmetic for clock rollover.
- Keep buffers bounded and stream history JSON in small chunks. Static build
  RAM/flash usage does not measure runtime Wi-Fi/BLE heap or stack usage.

## Verification

Run checks relevant to the change, plus the firmware build for code/dependency
changes. `pio` may instead be available at `~/.platformio/penv/bin/pio`.

```sh
pio run
g++ -std=c++11 -Wall -Wextra -Werror -Isrc test/temperature_history_test.cpp -o /tmp/room-temperature-test
/tmp/room-temperature-test
g++ -std=c++11 -Wall -Wextra -Werror -Isrc test/auto_cool_test.cpp -o /tmp/room-auto-cool-test
/tmp/room-auto-cool-test
python3 test/test_auto_cool_integration.py
node test/test_web_ui.js
python3 test/test_lotus_protocol.py
git diff --check
```

`test_auto_cool_integration.py` runs actual firmware helper definitions with
fake IR/NVS to check persistence, write counts, reboot, and target restoration.
The Lotus test's optional Python-reference comparison skips without
`--reference /path/to/lotus-lamp-python`. Host tests do not verify real IR
reception, power draw, flash hardware, or sensor placement; distinguish those
limits when reporting results. Upload firmware only when requested.

After dependency-path changes, stale CMake metadata can require removing only
the generated `.pio/build/nodemcu-32s` directory before rebuilding. Generated
`.pio/`, `sdkconfig.nodemcu-32s*`, and IDE caches are ignored; keep them out of
commits. Prefer portable logic and focused tests over hardware-dependent test
setup for decision logic.
