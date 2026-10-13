# Blue Star IE318YKU Wi-Fi remote

ESP32 NodeMCU-32S, IR transmitter on **GPIO 23**, serial **115200 8-N-1**.
DS18B20 data on **GPIO 22**, with your external pull-up resistor.
The project uses the adjacent `../IRremoteESP8266` checkout with experimental
BLUESTAR support. The fork branch alternative is in `platformio.ini`.

Build, upload, and open serial from this project directory:

```sh
pio run
pio run -t upload
pio device monitor
```

The port defaults to `/dev/ttyUSB0`. No IR commands are sent at boot.
The monitor holds DTR/RTS inactive; press EN/reset once if you need to see the
startup prompt after opening the monitor. A `[boot] reset=...` line identifies
resets. That line repeating indicates board resets rather than Wi-Fi retries.
Blank lines at the SSID prompt are ignored without repeating the prompt.

## Remembered AC settings

The controller saves its AC state in ESP32 NVS after each accepted setting
change from serial or the web page, including imported `load` states. Power,
mode, temperature, fan, swing, light, Turbo, capacity, Energy Save and Sleep
survive restarts and power loss. The fan selected before a capacity override
is also remembered, so returning to 100% behaves the same after restarting.

Startup restores the settings in the page and serial output without sending
IR. Use a control button or `send` to transmit them. Missing, invalid, or
incompatible saved data falls back to library defaults. Unchanged states and
page refreshes do not write flash. If saving fails, serial/web command output
reports the failure; the previously saved state is retained.

AC settings are stored independently from Wi-Fi credentials, so `wifi forget`
does not reset them. The controller remembers its own commands; it cannot
observe changes made with the physical remote.

## First Wi-Fi setup

On the first boot, serial prompts for your SSID and password. Enter each on
its own line, preserving case and spaces. Use a 2.4 GHz network for this board.
For an open network, submit an empty password line. The firmware does not echo
passwords; PlatformIO's local echo is disabled. `/cancel` exits the setup wizard
and leaves ordinary serial AC commands available.

Credentials and the hostname are saved together in ESP32 NVS through
[Preferences](https://docs.espressif.com/projects/arduino-esp32/en/latest/api/preferences.html).
They survive resets and power loss. Later boots reconnect automatically.
If connection fails, serial stays available and Wi-Fi retries every 30 seconds.
Use `wifi setup` to replace credentials; type the SSID and password at its prompts.

Once connected, open **http://esp32.local/** from a device on the same network.
Serial also prints an IP-address URL; use it when your client/network does not
resolve `.local` names. The remote page is served entirely by the ESP32 and
needs no internet access or external fonts/scripts.

To change the address, enter over serial:

```text
hostname bedroom-ac
```

The hostname is saved and Wi-Fi reconnects. Then use **http://bedroom-ac.local/**.
Use a name of 1-63 letters, digits, or hyphens, without `.local`; it cannot start
or end with a hyphen. `hostname` alone prints the current name and connection.

Network commands are serial-only:

```text
wifi setup
wifi status
wifi reconnect
wifi forget
hostname bedroom-ac
```

`wifi forget` removes the saved credentials, disconnects Wi-Fi, retains the
hostname, and starts setup again. `/cancel` leaves that wizard.

## Remote page

The page has Power, temperature minus/plus, Mode, Fan, Convertible capacity,
Horizontal Swing, Vertical Swing, Turbo, Energy Save, Sleep, Light, and Send
Again buttons. The cycle buttons follow the physical remote:

- Mode: Auto → Cool → Dry → Fan-only → Auto
- Fan: Low → Medium → High → Auto → Low
- Capacity: 100% → 110% Turbo → 80% → 60% → 40% → 100%

Setting buttons send IR immediately and refresh the display. Fan-only disables
temperature buttons. The page also refreshes every five seconds to show serial
changes. A failed request displays an error; it is never automatically resent.
The page reflects the controller's settings, not feedback from the AC: changes
made with the physical remote are not received by this project.

## Serial AC commands

The DS18B20 is read every five seconds. Its **Measured temperature** appears
separately from the AC **Set temperature** on the page, including while the AC
is off or in Fan-only mode. Serial prints each reading automatically and
`sensor` prints the latest reading immediately; `state` includes it too.
Periodic prints pause during the Wi-Fi setup prompts. Neither command sends IR.

Conversions run asynchronously so serial and web controls remain responsive.
If the sensor is missing or a reading fails, the page and serial report it as
unavailable instead of showing an old or invalid value. Discovery retries
automatically, so reconnecting the sensor does not require a restart.
PlatformIO installs the pinned OneWire and DallasTemperature dependencies.

Original commands remain available, with cycle/toggle shortcuts:

```text
on
mode cool
temp 25
fan auto
horizontal on
vertical off
light off
turbo on
capacity 80
econo on
sleep on
state
off
help
```

- `on`, `off`, `power toggle`
- `temp 16..30|up|down` (unavailable in Fan-only)
- `mode auto|cool|dry|fan|next` (`dehumid`/`fan-only` also accepted)
- `fan auto|low|medium|high|next`
- `capacity 100|110|80|60|40|next`
- `horizontal|vertical|light|turbo|econo|sleep on|off|toggle`
- `state`, `send`, `help`
- `load <24 hexadecimal digits>` imports ten main bytes plus the two-byte
  storage for the 11-bit secondary; follow with `send` to transmit.

`state`, `help`, and `load` do not send IR. Network setup does not send IR.
For an exact captured Power On message, use:

```text
load 3388821812922AA4053305CD
send
```

Power commands clamp setpoints below 24 C to 24; issue `temp 16` after `on`
to choose a lower temperature. Energy Save On selects 24 C/Low; disabling it
retains those settings. Standalone Turbo temporarily displays 16 C/High while
preserving stored temperature/fan; Convertible Turbo is a separate control.
The carrier remains the experimental 38 kHz value. Clock and timer commands
are not implemented; their raw fields remain preserved.

The web API uses `GET /api/state` and `POST /api/command` with a URL-encoded
`command` field. Serial and web use the same validated AC command handler.
Wi-Fi credentials are never included in web responses.

## Lotus Lamp LED strip

The firmware ports the BLE commands from
[wporter82/lotus-lamp-python](https://github.com/wporter82/lotus-lamp-python)
to Arduino C++ using NimBLE-Arduino. Python or a separate computer is not
needed when controlling the strip. Source attribution and the upstream MIT
license are in `THIRD_PARTY_NOTICES.md`.

Close/disconnect the phone's Lotus Lamp app before setup; the strip may allow
only one Bluetooth connection. Power the strip, open serial, then run:

```text
lamp scan
```

After the eight-second scan, serial prints a numbered list of connectable
Bluetooth devices, with probable lamps first. Select your strip by its name:

```text
lamp pair 1
lamp status
```

Replace `1` with its number in your scan. The firmware connects and discovers
a writable characteristic, preferring the standard `FFF0` service / `FFF3`
characteristic. It saves the address, BLE address type, name and discovered
UUIDs in a separate NVS entry. Supported Lotus Lamp devices do not require a
PIN or BLE bond; `pair` selects and remembers the device. If there are multiple
possible write characteristics, serial lists their service/characteristic
UUIDs. Select them explicitly using full UUIDs:

```text
lamp pair 1 0000fff0-0000-1000-8000-00805f9b34fb 0000fff3-0000-1000-8000-00805f9b34fb
```

The saved strip reconnects after boot, without replaying colors, effects or
power commands. Last requested settings are also saved after successful BLE
writes and displayed on the page. Bluetooth work runs on its own worker task;
scan/connect/write operations do not block the serial/web AC command loop.
If a connection is lost, the next light command attempts to reconnect. Failed
writes are reported and never automatically resent.

The existing web page includes On/Off, ten preset colors, a browser color
picker, brightness, 213 effect selections and animation speed. Adjusting a
picker, slider or effect selection does not transmit until you click Apply.
Preset color and On/Off buttons send immediately. The page shows connection
and operation status; it reports the controller's last requested values, not
readback from the strip or changes from another remote/app. Effect names are
from the upstream model and may vary with your strip.

Serial control also remains available:

```text
lamp on
lamp color red
lamp color #ff8000
lamp brightness 60
lamp effect 143
lamp speed 50
lamp off
lamp reconnect
lamp status
lamp forget
```

Brightness/speed range from 0 to 100. Documented effects range from 0 to 212;
0 uses the documented Auto Play packet (the Python method currently clamps
0 to 1 despite listing Auto Play as 0). Selecting a color leaves animation
mode. Setup, device selection, UUID overrides and `lamp forget` are serial-only.
Forgetting the strip does not erase Wi-Fi or AC settings. If the strip's BLE
address changes, scan and select it again.

`POST /api/lamp/command` accepts a URL-encoded `command` such as `color red`.
A 202 response means accepted for asynchronous processing; `GET /api/state`
contains the nested `lamp` status, completion counter and eventual result.
A successful write without response means the BLE stack accepted the packet;
there is no optical/device acknowledgement of the chosen setting.

Protocol checks can run on a host with Python 3 and g++:

```sh
python3 test/test_lotus_protocol.py
python3 test/test_lotus_protocol.py --reference /path/to/lotus-lamp-python
```

The optional reference check compares the C++ packets with the upstream
Python command methods without installing or using Bluetooth dependencies.
