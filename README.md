# Room Controller

An ESP32 controller for room temperature, air conditioning, and lighting, with
serial setup and a local web page at **http://esp32.local/**.

- **Air conditioning:** Blue Star IE318YKU control over infrared.
- **Temperature:** DS18B20 measurements, separate from the AC setpoint.
- **Lighting:** Lotus Lamp RGB strip control over Bluetooth Low Energy.
- **Saved settings:** Wi-Fi credentials, hostname, AC state, selected strip,
  and last requested light settings persist across power loss.

## Hardware and project setup

| Component | Connection |
| --- | --- |
| ESP32 | NodeMCU-32S board |
| IR transmitter | GPIO 23 |
| DS18B20 data | GPIO 22, with an external pull-up resistor |
| Lotus Lamp strip | BLE; no GPIO connection |
| Serial | `/dev/ttyUSB0`, 115200 baud, 8-N-1 |

The local project is `~/PlatformIO/Projects/Room_Controller`. Open
`Room_Controller.code-workspace` in VS Code to work on the controller and the
adjacent IR receiver example.

For a fresh checkout, create both projects side by side:

```sh
cd ~/PlatformIO/Projects
git clone git@github.com:Yureien/Room_Controller.git
git clone --branch bluestar-ie318yku git@github.com:Yureien/IRremoteESP8266.git
cd Room_Controller
```

`platformio.ini` uses the adjacent `../IRremoteESP8266` checkout, which contains
experimental Blue Star support. Changes to that checkout are available through
the local dependency link. To build without an adjacent checkout, replace only
`symlink://../IRremoteESP8266` with:

```ini
https://github.com/Yureien/IRremoteESP8266.git#bluestar-ie318yku
```

PlatformIO installs the pinned ESP32 platform, OneWire, DallasTemperature,
and NimBLE-Arduino dependencies. Build, upload, and open serial:

```sh
pio run
pio run -t upload
pio device monitor
```

If `pio` is not on your PATH, use `~/.platformio/penv/bin/pio` instead.
The monitor keeps DTR/RTS inactive; press EN/reset to see startup output after
opening it. A `[boot] reset=...` line identifies resets. Repeated boot lines
indicate board resets; Wi-Fi retries do not restart the firmware.

## Wi-Fi and address setup

On first boot, serial prompts for SSID and password. Enter each on its own
line, preserving case and spaces. Use a 2.4 GHz network. An empty password
selects an open network; blank lines at the SSID prompt are ignored.
Passwords are not echoed. `/cancel` exits setup and restores ordinary serial
commands.

The credentials and hostname are saved in ESP32 NVS. Later boots reconnect
automatically. If Wi-Fi fails, serial remains usable and connection retries
every 30 seconds.

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

The page contains a **Blue Star AC** section and an **LED strip** section.
Measured temperature appears separately from the AC set temperature, including
while the AC is off or in Fan-only mode.

AC controls include Power, temperature minus/plus, Mode, Fan, Convertible
capacity, Horizontal and Vertical Swing, Turbo, Energy Save, Sleep, AC Display
Light, and Send Again. Cycle controls follow the physical remote:

- Mode: Auto → Cool → Dry → Fan-only → Auto.
- Fan: Low → Medium → High → Auto → Low.
- Capacity: 100% → 110% Turbo → 80% → 60% → 40% → 100%.

AC buttons transmit immediately; Fan-only disables temperature adjustment.
The LED section provides On/Off, ten preset colors, a browser color picker,
brightness, 213 effects, animation speed, and Reconnect. Preset color and power
buttons send immediately. Other selections transmit when you click **Apply**.

The page refreshes every five seconds and polls more frequently during queued
lamp operations. It preserves unfinished color, slider, and effect edits.
Connection and command errors are displayed; failed commands are not
automatically resent. BLE work runs on a separate worker task, so discovery
and connection attempts do not block the AC command loop.

Displays reflect this controller's last requested settings. AC remote changes
and changes from another lighting app/remote are not synchronized. The lamp's
effect names come from the upstream reference model and may vary by strip.

## Temperature sensor

DS18B20 conversions run asynchronously every five seconds. Serial prints each
reading automatically; `sensor` prints the latest reading, and `state` includes
it alongside the AC state. Periodic output pauses during Wi-Fi setup prompts.

A missing sensor or failed reading is reported as unavailable rather than
showing an old or invalid value. Discovery retries automatically, so
reconnecting the sensor does not require a restart.

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
| `lamp status` | Print selection, connection, last requested settings, and UUIDs |
| `lamp forget` | Disconnect and erase the saved lamp selection/settings |

Setup, selection, UUID overrides, and forgetting are serial-only. If the
strip's address changes, scan and select it again. Selecting a color leaves
animation mode. Effect 0 sends the documented Auto Play packet; the upstream
Python method currently clamps it to 1 despite listing Auto Play as 0.

The saved strip reconnects after boot. A light command also attempts to
reconnect if disconnected. Bluetooth writes are asynchronous, with results
reported on serial and the page. A write without response indicates that the
BLE stack accepted the packet; it does not acknowledge the physical setting.

## State after restart or power loss

Wi-Fi, AC, and lamp records are independent. Each accepted AC setting change,
including `load`, saves the complete packet and remembered fan setting before
a capacity override. Successful lamp writes save the last requested light
settings. Unchanged records and page refreshes do not cause flash writes.
Save failures are reported, and previous flash records remain intact.

Startup restores the settings on serial and the website without sending IR
or replaying lamp colors, effects, or power commands. The lamp connection can
be established automatically. Use a control button or `send` to transmit AC
settings. Missing or incompatible saved data falls back to defaults/setup.
Measured temperature is read afresh after startup.

The project/repository rename preserves existing flash records. `wifi forget`
does not reset AC or lamp settings; `lamp forget` does not reset Wi-Fi or AC.

## Web API and protocol checks

| Endpoint | Behavior |
| --- | --- |
| `GET /` | Room Controller page |
| `GET /api/state` | AC, sensor, network address, and nested lamp status |
| `POST /api/command` | URL-encoded `command`, such as `temp 25`, through the AC handler |
| `POST /api/lamp/command` | URL-encoded `command`, such as `color red`, queued for BLE |

A lamp response of 202 means accepted for asynchronous processing. The nested
`lamp` object in `/api/state` includes `busy`, `completed`, `ok`, and the eventual
message. Serial and web share validated command handlers. Wi-Fi credentials
are not included in web responses.

Protocol checks run on a host with Python 3 and g++:

```sh
python3 test/test_lotus_protocol.py
python3 test/test_lotus_protocol.py --reference /path/to/lotus-lamp-python
```

The optional reference check compares C++ packets against the upstream Python
methods without installing or using Python Bluetooth dependencies.
