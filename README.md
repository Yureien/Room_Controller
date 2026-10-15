# Room Controller

An ESP32 controller for room temperature, air conditioning, and lighting, with
serial setup and a local web page at **http://esp32.local/**.

- **Air conditioning:** Blue Star IE318YKU control over infrared.
- **Temperature:** DS18B20 measurements, separate from the AC setpoint.
- **Lighting:** Lotus Lamp RGB strip control over Bluetooth Low Energy.
- **Saved settings:** Wi-Fi credentials, hostname, AC state, selected strip,
  last requested light settings, and Auto-Cool enabled/target settings persist
  across power loss. Temperature history and recovery timers stay in RAM.

## Hardware and project setup

| Component | Connection |
| --- | --- |
| ESP32 | NodeMCU-32S board |
| IR transmitter | GPIO 23 |
| DS18B20 data | GPIO 22, with an external pull-up resistor |
| Lotus Lamp strip | BLE; no GPIO connection |
| Blue onboard activity LED | GPIO 2, active-high on the reference NodeMCU-32S |
| Serial | `/dev/ttyUSB0`, 115200 baud, 8-N-1 |

The local project is `~/PlatformIO/Projects/Room_Controller`. Open
`Room_Controller.code-workspace` in VS Code to work on the controller. Its
optional IR receiver folder points to an adjacent `../IRremoteESP8266` checkout;
that folder is only for receiver experiments and is not needed to build.

For a fresh checkout:

```sh
cd ~/PlatformIO/Projects
git clone git@github.com:Yureien/Room_Controller.git
cd Room_Controller
```

`platformio.ini` fetches the IR library directly from the fork's experimental
Blue Star branch. An adjacent IR library checkout is not required to build:

```ini
https://github.com/Yureien/IRremoteESP8266.git#bluestar-ie318yku
```

PlatformIO installs the pinned ESP32 platform, ESP-IDF 4.4.7, Arduino 2.0.17,
OneWire, DallasTemperature, and NimBLE-Arduino dependencies. Arduino is compiled
as an ESP-IDF component so `sdkconfig.defaults` can enable power management and
tickless idle in the SDK itself. The first build downloads additional tools and
compiles the SDK; later builds are incremental. The pre-build script disables
IDF's separate component manager for the application and bootloader; PlatformIO
installs the libraries. Build, upload, and open serial:

```sh
pio run
pio run -t upload
pio device monitor
```

If `pio` is not on your PATH, use `~/.platformio/penv/bin/pio` instead.
The monitor keeps DTR/RTS inactive; press EN/reset to see startup output after
opening it. With the default AC-powered build, send serial commands directly.
If optional power savings are enabled, after idle press **Enter**, wait for
**Serial awake**, then send a command; UART wake-up can lose characters, so the
wake input is discarded. Serial input then keeps the controller awake for
60 seconds. A `[boot] reset=...` line identifies resets. Repeated boot lines
indicate board resets; Wi-Fi retries do not restart the firmware.

If serial reports `Brownout detector was triggered` followed by
`[boot] reset=brownout`, the supply fell below the ESP32's safe voltage. A reset
drops every outstanding HTTP request. Check the USB cable, supply, board
regulator, and IR transmitter wiring under load; compare with a known-good
power source. Concurrent radio/IR activity can expose a marginal supply.
Keep brownout protection enabled. See
[Espressif's brownout explanation](https://docs.espressif.com/projects/esp-idf/en/v4.4.5/esp32/api-guides/fatal-errors.html#brownout).

## Wi-Fi and address setup

On first boot, serial prompts for SSID and password. Enter each on its own
line, preserving case and spaces. Use a 2.4 GHz network. An empty password
selects an open network; blank lines at the SSID prompt are ignored.
Passwords are not echoed. `/cancel` exits setup and restores ordinary serial
commands.

The credentials and hostname are saved in ESP32 NVS. Later boots reconnect
automatically. If Wi-Fi fails, serial remains usable and connection retries
every 30 seconds.
The HTTP listener remains bound through temporary link loss; unsuccessful
listener startup retries every five seconds instead of leaving the site offline.
Repeating the current hostname does not reconnect or rewrite flash.

Open **http://esp32.local/** from a device on the same network. Serial also
prints an IP-address URL for clients that cannot resolve `.local`. The page
is served entirely by the ESP32, without internet access or external assets.
The Room Controller project rename retains the default and any saved hostname.

Network commands are serial-only:

| Command | Action |
| --- | --- |
| `wifi setup` | Replace saved SSID/password using serial prompts |
| `wifi status` | Print network name, hostname, and connection details |
| `wifi reconnect` | Reconnect using saved credentials |
| `wifi forget` | Remove Wi-Fi credentials and start setup again |
| `hostname` | Print the current hostname and connection |
| `hostname room-controller` | Save a new name and reconnect |

The hostname is 1–63 letters, digits, or hyphens, without `.local`, and cannot
start or end with a hyphen. The example becomes **http://room-controller.local/**.
`wifi forget` retains the hostname, AC state, and lamp selection.

## Room Controller website

The page contains **Blue Star AC**, **Room temperature history**, and **LED strip** sections.
Measured temperature appears separately from the AC set temperature, including
while the AC is off or in Fan-only mode.

AC controls include Power, temperature minus/plus, Mode, Fan, Convertible
capacity, Horizontal and Vertical Swing, Turbo, Energy Save, Sleep, AC Display
Light, and Send Again. Cycle controls follow the physical remote:

- Mode: Auto → Cool → Dry → Fan-only → Auto.
- Fan: Low → Medium → High → Auto → Low.
- Capacity: 100% → 110% Turbo → 80% → 60% → 40% → 100%.

AC buttons transmit immediately; Fan-only disables temperature adjustment.
**Auto-Cool** uses the measured room temperature to control Turbo and attempt
one recovery if cooling stops. It requires the AC to already be on in Cool
mode, with Energy Save and Sleep off and a valid sensor reading. It never turns
on an otherwise-off AC. It runs on the ESP32 even with the browser closed or
Wi-Fi disconnected.

- Turbo turns on above the target +1.5°C and off below the target −0.5°C.
  Equality at either boundary retains the current Turbo setting. The saved
  target stays independent of Turbo's 16°C operating temperature; the page
  shows the target and notes when the AC is running at 16°C. Temperature
  buttons adjust the target and restart trend collection.
- Recovery requires five continuous minutes above target +1.5°C with Turbo
  active. The last 30-second average must exceed the first by at least 0.125°C,
  and must also exceed the preceding 30-second average. Flat, falling, or
  one-step sensor noise does not trigger recovery. A missing reading, a gap
  longer than seven seconds, a target change, or a reading at/below target
  +1.5°C restarts the observation window.
- Recovery sends Power Off, waits two minutes without blocking other controller
  work, then sends Power On and restores the target, including targets below
  24°C that the AC library's Power command otherwise clamps. The one-recovery
  lockout clears only on a valid reading strictly below target; button toggles
  and manual power changes preserve it until then.
- During the wait, **Power** cancels the restart and keeps the AC off. Disabling
  Auto-Cool does the same. Other AC changes, including early Power On, are
  rejected until the wait finishes or is explicitly cancelled. Lamp control
  remains available. Sensor loss during the wait cancels the restart and
  requires a manual restart; outside recovery it turns Turbo off and pauses
  automation until valid measurements return.
- Manual power, mode, fan, capacity, Turbo, Energy Save, Sleep, or raw-state
  changes disable Auto-Cool. Swing and display-light edits keep it enabled.
  Enabling/disabling and target edits save a small eight-byte NVS settings
  record; unchanged values and automatic Turbo transitions do not write flash.
  Recovery updates the existing saved AC power state at Off and On, so a reboot
  during recovery leaves the AC off and does not resume a queued restart.
- On reboot, Auto-Cool resumes temperature control only if the saved AC state
  is On in Cool mode. Recovery trends, countdowns, and the one-recovery lockout
  are not persisted; a new boot starts a new observation window. No IR is sent
  during startup restoration; temperature control waits for a valid reading.

The controller has no AC state feedback: changes using the physical remote
are invisible, and an IR packet can be missed. These protections use the
controller's last requested power state. A rising trend also cannot distinguish
a cooling fault from increased room heat or a poorly placed sensor. Verify IR
reception and sensor placement on the actual AC before relying on recovery.

The LED section provides On/Off, ten preset colors, a browser color picker,
brightness, 213 effects, animation speed, Connect/Reconnect, and Disconnect. Preset color and power
buttons send immediately. Other selections transmit when you click **Apply**.

The page refreshes settings and history every 5 seconds, and polls
more frequently during queued lamp operations. Hidden tabs stop polling and
refresh when reopened. It preserves unfinished color, slider, and effect edits.
Connection and command errors are displayed; failed commands are not
automatically resent. BLE work runs on a separate worker task, so discovery
and connection attempts do not block the AC command loop. HTTP transfers also
run asynchronously using the pinned ESP32Async server and AsyncTCP libraries.
State polling leaves command buttons available, and session/revision tags prevent
out-of-order responses from rolling back newer settings.
Starting a command cancels background state/history reads; polling resumes
afterward. Hidden tabs also cancel background reads. A cancelled read does not
mark the controller offline, and an old failed request cannot erase newer
evidence that the controller is reachable.

Displays reflect this controller's last requested settings. AC remote changes
and changes from another lighting app/remote are not synchronized. The lamp's
effect names come from the upstream reference model and may vary by strip.

## Temperature sensor

DS18B20 conversions run asynchronously every 5 seconds. Serial prints each
reading automatically; `sensor` prints the latest reading, and `state` includes
it alongside the AC state. Periodic output pauses during Wi-Fi setup prompts.

A missing sensor or failed reading is reported as unavailable rather than
showing an old or invalid value. Discovery retries automatically, so
reconnecting the sensor does not require a restart.

Temperature history records one reading every 5 seconds in a fixed ring buffer of
17,280 samples (roughly 24 hours). Each sample uses a signed 16-bit temperature
in hundredths of a degree; the entire buffer and bookkeeping use 34,576 bytes
(about 33.8 KiB). This is half the sample storage required by 32-bit floats.
Missing readings and missed intervals appear as gaps. The oldest sample is
overwritten when full. History stays in RAM and resets on restart; periodic
measurements never write to flash.

The browser draws the graph with a small built-in canvas renderer, without
external libraries, CDN assets, or internet access. It labels times using the
browser's clock and the sample ages, so the ESP32 does not need NTP. History is
fetched separately from settings in pages of at most 128 samples (under 1.2 KiB
per response). The browser loads the retained day progressively, leaving 100 ms
between pages and pausing backfill while controls are sending. Once caught up,
it requests only new samples every five seconds, instead of downloading the
whole day repeatedly. Session IDs and monotonic cursors handle restarts and ring
wraps; missing readings retain their gaps. The server streams each bounded page
with Content-Length, keeping a full day's JSON out of the heap. Slow clients
transfer in the background while the main loop keeps servicing controls.

## Async HTTP ownership

Network callbacks validate request parameters and submit copied inputs through
`lib/ControllerWeb/src/web_dispatch.h`. Only the main loop executes AC/lamp commands, takes state
and history snapshots, writes settings, and updates power/activity state. No
controller objects, mutable history references, or HTTP request pointers cross
the task boundary. The existing BLE worker still communicates through its own
queues and atomics. AsyncTCP is pinned to core 0 so its callbacks do not preempt
the main loop's software IR carrier on core 1.

The handoff has six fixed slots with replies bounded to 3 KiB each (18 KiB of
reply storage): two slots reserved for commands and four for state/history.
Commands are FIFO and take priority over reads; one job is serviced per main-loop
iteration. Full queues return HTTP 503. Work still queued after two seconds is
rejected rather than executed late. Wi-Fi loss rejects pending work. Disconnects
cancel unclaimed work; a command already claimed may have taken effect, so check
the actual device before retrying. Neither firmware nor browser resends commands
automatically.

`lib/ControllerWeb/src/web_response.h` waits for the reply using the network task's existing TCP
poll callbacks, then gives the library an independent copy for the transfer. All
request/response operations and disconnect cleanup stay on the network task;
slot generations prevent a late completion from affecting a reused slot. Reply
startup can wait for the next TCP poll (normally up to roughly 500 ms), and full
history loads progressively across many pages. This bounds history work and
isolates slow network transfers; it does not make IR transmissions instantaneous.
Deferred API responses restore a ten-second receive inactivity timeout. Peers
that stop sending TCP traffic release their requests and dispatch slots after
that interval. This is not a total request deadline: received traffic can extend
the connection lifetime.

## Power saving

CPU/light-sleep/Wi-Fi power savings are **disabled by default** for AC-powered
operation. The CPU stays at 240 MHz, automatic light sleep is disabled, Wi-Fi
uses `WIFI_PS_NONE`, and serial commands do not require a wake-up Enter.
To opt in, append the power-saving define to the existing `build_flags` in the
`nodemcu-32s` environment in `platformio.ini`:

```ini
build_flags = -DCONFIG_ASYNC_TCP_RUNNING_CORE=0 -DROOM_CONTROLLER_POWER_SAVING=1
```

`lib/ControllerCore/src/power_config.h` defaults the flag to `0` and accepts only `0` or `1`.
SDK power-management support remains compiled so either mode can be built.

Bluetooth stays off at boot, including when a strip is saved. A light command
automatically starts Bluetooth and connects; **Connect/Reconnect** also starts a
session without replaying settings. After 30 seconds without a lamp operation,
the worker disconnects and shuts down the NimBLE host and controller, releasing
their allocations. **Disconnect** shuts down immediately and retains the strip
selection. Scans and failed connections also shut Bluetooth down. The LED strip
continues displaying its last settings independently of the ESP32 connection.
This disconnect behavior remains active in both builds so the phone can take
over the strip after the ESP32 releases its connection.

When the flag is `1`, `esp_pm_configure()` enables automatic light sleep and
dynamic frequency scaling with an 80 MHz minimum and the full 240 MHz maximum. ESP-IDF controls frequency
changes and sleep using its scheduler and driver locks. Application locks keep
240 MHz and prevent sleep during commands, web requests, Wi-Fi connection
attempts, setup prompts, and the entire Bluetooth session. The request locks
release after five seconds idle, unless serial activity requires more awake
time. IR transmission runs at 240 MHz to preserve the original software carrier
timing. APB stays at 80 MHz across the CPU range for UART and sensor timing.
The main loop yields for 20 ms rather than waking every millisecond, and the
BLE worker blocks indefinitely while Bluetooth is off. Temperature sensing and
RAM history continue across light sleep without a restart.

The blue onboard LED works independently of the power-saving flag. It stays on
through a BLE session or queued lamp work, and during an AC IR transmission plus
500 ms afterward. While Auto-Cool has Turbo active and the AC is on, it
alternates one second on and one second off. Monitoring without Turbo and the
recovery off period do not blink. BLE/AC activity takes priority over blinking.
Otherwise it is off after boot.
Routine web polling and temperature sampling do not hold the LED on. The
board's separate red power LED is wired directly to power and cannot be
switched by firmware.
The [Ai-Thinker schematic](https://robu.in/wp-content/uploads/2017/10/SKU-44682-datasheet.pdf)
connects the blue LED from GPIO2 to ground; `kActivityLedOnLevel` in `main.cpp`
can be changed to `LOW` for a board clone with inverted LED wiring.

Wi-Fi remains associated so the web page stays reachable.
With power savings enabled, `esp_wifi_set_ps(WIFI_PS_MIN_MODEM)` enables modem
sleep between the access point's DTIM beacons, during both idle and active use. This preserves DTIM
multicast reception for `.local` discovery. Request latency depends on the
access point's DTIM interval. Automatic light sleep complements modem sleep;
the Wi-Fi driver manages its wake-ups and power locks.

`CONFIG_PM_ENABLE` and `CONFIG_FREERTOS_USE_TICKLESS_IDLE` are enabled in
`sdkconfig.defaults`. Adding C++ flags to the old precompiled Arduino SDK would
not enable them. Startup prints the power configuration or a setup error.
If power management setup fails, the controller keeps the SDK's 240 MHz startup
frequency and minimum Wi-Fi modem sleep. The partition table preserves the
original NVS offsets and app partition sizes, so saved settings remain intact.
See [Espressif's power management documentation](https://docs.espressif.com/projects/esp-idf/en/v4.4.7/esp32/api-reference/system/power_management.html).

Actual board power savings need measurement on the NodeMCU-32S; its USB serial
adapter, regulator, and power LED also consume power. Check web response after
idle, repeated BLE reconnects, serial wake-up, temperature continuity, and IR
operation on hardware before relying on measured savings.

## AC serial commands

| Command | Values or behavior |
| --- | --- |
| `on`, `off`, `power toggle` | Set or toggle AC power |
| `temp` | `16..30`, `up`, `down`; unavailable in Fan-only |
| `mode` | `auto`, `cool`, `dry`, `fan`, `next`; aliases `dehumid`, `fan-only` |
| `fan` | `auto`, `low`, `medium`, `high`, `next` |
| `capacity` | `100`, `110`, `80`, `60`, `40`, `next` |
| `horizontal`, `vertical` | `on`, `off`, `toggle` |
| `light`, `turbo`, `econo`, `sleep` | `on`, `off`, `toggle` |
| `state` | Print AC state and measured temperature without sending IR |
| `send` | Transmit the current AC packet again |
| `load <24 hexadecimal digits>` | Import ten main bytes plus the two-byte secondary command |
| `sensor` | Print measured temperature without sending IR |
| `autocool on`, `autocool off`, `autocool toggle` | Enable/disable Auto-Cool; enabling requires AC On/Cool and a valid sensor |
| `help` | List AC, network, sensor, and lighting commands |

Unprefixed `on`, `off`, and `light` address the AC; `lamp on` and `lamp off`
address the strip. `load` validates and saves the state without transmitting;
use `send` afterward. An exact captured Power On message is:

```text
load 3388821812922AA4053305CD
send
```

Power commands clamp setpoints below 24°C to 24°C. Issue `temp 16` after `on`
to select a lower temperature. Energy Save On selects 24°C/Low; disabling it
keeps those settings. Standalone Turbo displays 16°C/High while preserving
stored temperature/fan. Convertible Turbo is a separate capacity control.

Blue Star support uses an experimental 38 kHz carrier. Clock/timer commands
are not implemented; their opaque packet fields are preserved.

## Lotus Lamp discovery and serial commands

The firmware ports commands from
[wporter82/lotus-lamp-python](https://github.com/wporter82/lotus-lamp-python)
to Arduino C++ using NimBLE. Runtime control does not need Python or another
computer. Source attribution and the upstream MIT license are in
[THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).

Power the strip and close/disconnect the phone's lamp app; the strip may allow
only one Bluetooth connection. Run:

```text
lamp scan
```

After an eight-second scan, serial prints a numbered list of connectable
devices, with probable lamps first. Select your strip by its name:

```text
lamp pair 1
lamp status
```

Replace `1` with its number from the scan. The firmware discovers a writable
characteristic, preferring service `FFF0` and characteristic `FFF3`, then saves
the device address, BLE address type, name, and discovered UUIDs. Supported
Lotus Lamp devices do not require a PIN or BLE bond; `pair` selects and remembers
the device. If discovery finds multiple possible writers, serial lists their
UUIDs. You can specify full service and write UUIDs explicitly:

```text
lamp pair 1 0000fff0-0000-1000-8000-00805f9b34fb 0000fff3-0000-1000-8000-00805f9b34fb
```

| Command | Values or behavior |
| --- | --- |
| `lamp on`, `lamp off` | Set strip power |
| `lamp color red` | Named preset: red, green, blue, white, yellow, cyan, magenta, orange, purple, pink, black |
| `lamp color #ff8000` | Any RGB color, optionally omitting `#` |
| `lamp brightness 60` | Brightness `0..100` |
| `lamp effect 143` | Effect `0..212` |
| `lamp speed 50` | Animation speed `0..100` |
| `lamp reconnect` | Connect to the saved strip |
| `lamp connect` | Alias for reconnect |
| `lamp disconnect` | Shut Bluetooth down and retain saved selection/settings |
| `lamp status` | Print selection, connection, last requested settings, and UUIDs |
| `lamp forget` | Disconnect and erase the saved lamp selection/settings |

Setup, selection, UUID overrides, and forgetting are serial-only. If the
strip's address changes, scan and select it again. Selecting a color leaves
animation mode. Effect 0 sends the documented Auto Play packet; the upstream
Python method currently clamps it to 1 despite listing Auto Play as 0.

The saved strip stays disconnected after boot. A light command attempts to
connect if needed; sessions end after 30 seconds idle. Bluetooth writes are asynchronous, with results
reported on serial and the page. A write without response indicates that the
BLE stack accepted the packet; it does not acknowledge the physical setting.

Changing the selected device or connection UUIDs stops the previous BLE host
session before reconnecting. Scan startup failures and timeouts are reported;
failed host shutdown retains its objects for a safe retry. A partial controller
teardown failure requires a board restart; stale BLE objects are never reused.
Connect/Reconnect reuses an already matching live connection. NimBLE's internal
host initialization, host stop, and synchronous GATT waits can still stall the lamp worker
if the underlying stack fails; the worker is not forcibly deleted while its
callbacks may still be active.

## State after restart or power loss

Wi-Fi, AC, and lamp records are independent. Each accepted AC setting change,
including `load`, saves the complete packet and remembered fan setting before
a capacity override. Successful lamp writes save the last requested light
settings. Unchanged records and page refreshes do not cause flash writes.
Save failures are reported, and previous flash records remain intact.

Startup restores the settings on serial and the website without sending IR
or replaying lamp colors, effects, or power commands. Bluetooth starts only when
requested. Use a control button or `send` to transmit AC
settings. Missing or incompatible saved data falls back to defaults/setup.
Measured temperature is read afresh after startup; temperature history starts empty.
Serial executes at most one complete command per loop to prevent input bursts
from starving sensing and web requests. State snapshots mark readings older
than seven seconds unavailable, matching the automation's freshness rule.

The project/repository rename preserves existing flash records. `wifi forget`
does not reset AC or lamp settings; `lamp forget` does not reset Wi-Fi or AC.

## Web API and protocol checks

| Endpoint | Behavior |
| --- | --- |
| `GET /` | Room Controller page |
| `GET /api/state` | AC, sensor, network address, Auto-Cool target/recovery status, and nested lamp status |
| `GET /api/temperature/history` | Bounded oldest-first page; optional `limit=1..128`, and `cursor` with `session` for continuation (see below) |
| `POST /api/command` | URL-encoded `command`, such as `temp 25`, through the AC handler |
| `POST /api/lamp/command` | URL-encoded `command`, such as `color red`, queued for BLE |

History responses contain `session` (boot ID), `interval` (5 seconds), `age`
(seconds since the last sample in this page), `first`/`end` (retained cursor range,
end exclusive), `start`/`next` (page range), `total` (retained slots), `reset`,
`more`, and `samples` (°C or `null` for gaps). Start with
`GET /api/temperature/history?limit=128`; continue with
`?limit=128&session=<session>&cursor=<next>` until `more` is false, then poll that
same cursor for new samples. An expired/future cursor or different session returns
`reset:true` and starts at the oldest retained sample; discard the old browser
cache. Malformed limits/cursors or a cursor without a session return HTTP 400.
Requests without pagination parameters now return the first bounded page.

State and command snapshots include `session` (boot ID) and increasing `revision`
for ordering concurrent replies. Execution replies retain AC HTTP 200/400 and
lamp HTTP 202/400 semantics. Validation, full-queue, timeout, and disconnect errors
may contain only `ok:false` and `message`; HTTP 503 never implies a command
succeeded and must not trigger an automatic resend.

A lamp response of 202 means accepted for asynchronous processing. The nested
`lamp` object in `/api/state` includes `busy`, `completed`, `ok`, and the eventual
message. Serial and web share validated command handlers. Wi-Fi credentials
are not included in web responses.

Protocol checks run on a host with Python 3 and g++:

```sh
python3 test/test_lotus_protocol.py
python3 test/test_lotus_protocol.py --reference /path/to/lotus-lamp-python
g++ -std=c++11 -Wall -Wextra -Werror -Isrc test/temperature_history_test.cpp -o /tmp/room-temperature-test
/tmp/room-temperature-test
g++ -std=c++11 -Wall -Wextra -Werror -Isrc test/temperature_history_api_test.cpp -o /tmp/room-history-api-test
/tmp/room-history-api-test
g++ -std=c++11 -Wall -Wextra -Werror -Isrc test/auto_cool_test.cpp -o /tmp/room-auto-cool-test
/tmp/room-auto-cool-test
python3 test/test_auto_cool_integration.py
g++ -std=c++11 -Wall -Wextra -Werror -pthread -Isrc test/web_dispatch_test.cpp -o /tmp/room-web-dispatch-test
/tmp/room-web-dispatch-test
python3 test/test_web_async.py
node test/test_web_ui.js
```

The async host checks use a fake HTTP transport and verify routing, task
ownership, command priority, overload/expiry, disconnected requests, immutable
history snapshots, and concurrent slot reuse. They do not measure on-device
latency, heap/stack headroom, Wi-Fi/BLE coexistence, or actual IR reception.
Hardware validation should include a full history buffer, simultaneous controls
and slow history transfers, multiple tabs, and Wi-Fi loss/reconnection.

The optional reference check compares C++ packets against the upstream Python
methods without installing or using Python Bluetooth dependencies.

## Firmware components

Reusable code uses conventional PlatformIO layouts under `lib/`: `ControllerCore`
contains portable automation/history/LED logic and the existing power flag;
`ControllerSupport` supplies JSON escaping; `LotusLamp` owns settings, command
queues, and a separate worker/session implementation. Libraries do not depend
on application globals. BLE objects and callbacks live on the worker; NVS and
result application remain on the main loop.

Tests are deferred for this refactor. Existing source-extraction harnesses may
need path/interface migration before reuse; their earlier results do not verify
the refactored firmware.

`TemperatureSensor` searches one OneWire address per service call (up to 32 per
pass), retries missing hardware every five seconds, and retains parasite power
through the 750-ms conversion. Freshness uses monotonic acquisition time and
applies to serial, HTTP, history, and automation. `AcController` owns IR,
Auto-Cool, fan memory, and the unchanged legacy AC/settings records. Separate
record save outcomes feed the existing aggregate Auto-Cool warning; a successful
settings save cannot erase a failed recovery power-state save.

`ControllerNetwork` owns only the legacy network record and Wi-Fi/mDNS lifecycle.
It retries failed mDNS startup every 30 seconds while retaining IP access.
`ControllerWeb` owns the listener, routes, immutable boot session, dispatch, and
network-task activity flag. `src/controller_app.cpp` owns composition, history,
state revisions, and loop scheduling; `src/serial_console.cpp` handles prompts
and the bounded one-line-per-loop parser. `src/board_config.h` holds GPIOs.
The existing power policy is mechanically relocated into `src/power_runtime.*`.

Browser source lives in `web/index.html`, `web/style.css`, and
`web/controller.js`. The PlatformIO pre-build hook invokes the stdlib-only
`scripts/embed_web.py`, generating `$BUILD_DIR/generated/web_ui.h` before
compilation. Every build notices asset edits; unchanged generated content keeps
its timestamp. Only `controller_web.cpp` includes the page. No Node dependencies,
external assets, filesystem upload, or separate website deployment is involved.
For manual inspection: `python3 scripts/embed_web.py --output /tmp/web_ui.h`.
Browser boot-session changes retire obsolete replies and pending lamp operation
tracking while preserving unsent field edits. Dense history lines retain isolated
sample markers without drawing a circle for every connected point.

The sensor conversion deadline uses monotonic microseconds, avoiding an early
read at a millisecond boundary. Delayed reads retain the conversion completion
time rather than acquiring false freshness after a stalled loop. Legacy NVS
record sizes and key field offsets are checked at compile time.
