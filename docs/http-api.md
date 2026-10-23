# Room Controller HTTP API

The ESP32 serves this API over HTTP on port **80**, alongside the controller
page. The default base URL is `http://esp32.local`; the configured hostname or
the IP address printed on serial also works. Paths are not version-prefixed.
This reference describes the current firmware, not the serial console protocol.

## Requests and access

- State/history reads use `GET`. Commands use `POST` with
  `Content-Type: application/x-www-form-urlencoded` and a `command` field.
  JSON request bodies are not supported.
- Command names are case-insensitive; leading/trailing whitespace is trimmed.
  Use the exact syntax below, with single spaces between lamp command arguments.
  HTTP lamp commands omit the serial console's `lamp ` prefix.
- The decoded command must be nonempty, contain no NUL bytes, and be at most
  **127 bytes for AC** or **100 bytes for lamp** commands.
- Command requests with an `Origin` header are accepted only if its value
  exactly equals `http://` followed by the request's `Host` header. An absent
  `Origin` is accepted, as with the curl examples below. The firmware does not
  provide cross-origin CORS/preflight support.
- There is no API authentication, token, or TLS endpoint. The Origin check is
  a browser-origin restriction, not authentication. Wi-Fi credentials are not
  returned in API responses.
- API JSON responses, including validation errors, use
  `Content-Type: application/json` and `Cache-Control: no-store`. Unknown routes
  return plain text instead. Human-readable `message` text is not an error code.

## Endpoints

| Method and path | Success | Body |
| --- | --- | --- |
| `GET /` | `200` | Embedded HTML controller page; `text/html; charset=utf-8` |
| `GET /api/state` | `200` | Full [state snapshot](#state-snapshot) |
| `GET /api/temperature/history` | `200` | [History page](#temperature-history); separate schema without `ok` or `revision` |
| `POST /api/command` | `200` | Full state snapshot with AC command outcome |
| `POST /api/lamp/command` | `202` | Full state snapshot with lamp request acceptance |
| `GET /favicon.ico` | `204` | Empty body |

Unmatched requests return `404` with the text `Not found`. Use the methods
listed above; there are no legacy `/state`, `/command`, or `/history` aliases.

## Quick start

These examples use the default hostname. The POST examples operate the devices.

```sh
controller_base='http://esp32.local'

curl --silent --show-error "$controller_base/api/state"

curl --silent --show-error \
  "$controller_base/api/temperature/history?limit=128"

# Send an AC setpoint command.
curl --include --data-urlencode 'command=temp 25' \
  "$controller_base/api/command"

# Queue a color command for the lamp selected previously over serial.
curl --include --data-urlencode 'command=color #ff8000' \
  "$controller_base/api/lamp/command"
```

`--data-urlencode` selects POST and encodes spaces and `#` correctly. Inspect
both the HTTP status and JSON body; a lamp `202` is not its completion result.

## State snapshot

`GET /api/state` and command-handler responses share this shape. The example
is illustrative; it is not a statement of startup defaults.

```json
{
  "ok": true,
  "revision": 42,
  "session": "cafebabe",
  "message": "",
  "hostname": "esp32",
  "ip": "192.168.1.55",
  "power": true,
  "mode": 8,
  "temperature": 24,
  "autoCool": {
    "enabled": true,
    "target": 24,
    "waiting": false,
    "waitSeconds": 0,
    "recoveryUsed": false,
    "recoveryAttempts": 0,
    "recoveryBackoffSeconds": 300,
    "irRetriesRemaining": 0,
    "cancelled": false,
    "saved": true
  },
  "fan": 4,
  "capacity": 5,
  "horizontal": false,
  "vertical": true,
  "light": true,
  "turbo": false,
  "turboCooldownSeconds": 0,
  "econo": false,
  "sleep": false,
  "sent": 12,
  "measuredTemperature": 24.25,
  "sensorStatus": "ready",
  "sensorIntervalSeconds": 5,
  "lamp": {
    "configured": true,
    "connected": false,
    "busy": false,
    "ok": true,
    "message": "Strip saved. Connect or send a setting when needed; Bluetooth sleeps after 30s idle.",
    "name": "LED strip",
    "address": "12:34:56:78:90:AB",
    "power": null,
    "color": "#ffffff",
    "brightness": 100,
    "speed": 50,
    "effect": 0,
    "solid": true,
    "completed": 0
  }
}
```

### Response metadata and ordering

| Field | Type | Meaning |
| --- | --- | --- |
| `ok` | boolean | This HTTP operation's success/acceptance. A state read can have `ok:true` while `lamp.ok:false` or the sensor is unavailable. |
| `message` | string | Command outcome, rejection, or persistence warning; normally empty for `GET /api/state`. |
| `session` | string | Eight lowercase hexadecimal digits identifying this boot; also used by history pagination. |
| `revision` | integer | Increasing snapshot sequence within a boot. It increments for every full snapshot, including reads and command rejections, even when settings did not change. |
| `hostname` | string | Hostname without `.local`. |
| `ip` | string | Controller's current station IPv4 address. |

Apply snapshots only in revision order within a session. A new session permits
revisions to restart and invalidates previous history/operation tracking; ignore
late replies from retired sessions. `revision` is not a configuration version
and cannot be supplied as a conditional-write/precondition parameter. Handle a
command response's acceptance/error separately from whether its snapshot is
new enough to replace displayed state.

### AC and sensor fields

| Field | Type | Meaning |
| --- | --- | --- |
| `power` | boolean | Last requested AC power state. |
| `mode` | integer | Native mode: `1` Fan-only, `2` Auto, `4` Dry, `8` Cool. |
| `temperature` | integer | Stored AC temperature in °C, normally `16..30`; Fan-only has a hidden stored value of `24`. Standalone Turbo operates at 16°C without replacing the stored target. |
| `fan` | integer | Stored native fan code: `0` Convertible Turbo/High, `1` High, `2` Medium, `4` Low, `7` Auto. Standalone Turbo operates at High regardless of the stored code. |
| `capacity` | integer | Native capacity code: `1` → 110%, `2` → 80%, `3` → 60%, `4` → 40%, `5` → 100%. These are not percentage values. |
| `horizontal`, `vertical` | boolean | Requested horizontal/vertical swing. |
| `light` | boolean | Requested AC display light; unrelated to the LED strip. |
| `turbo`, `econo`, `sleep` | boolean | Requested standalone Turbo, Energy Save, and Sleep modes. |
| `turboCooldownSeconds` | integer | Remaining automatic Turbo cooldown, rounded up; `0` when eligible. Turbo expires after 20 minutes and Auto-Cool waits five minutes before another session. Normal cooling continues during this wait; manual Turbo On can start a fresh session. |
| `sent` | integer | IR send-call count since boot, including serial and automatic actions. One command can produce multiple sends. It is not an AC acknowledgement counter. |
| `measuredTemperature` | number or null | Latest valid, fresh DS18B20 reading in °C, reported to two decimal places; `null` when waiting/unavailable. |
| `sensorStatus` | string | `waiting`, `ready`, or `unavailable`. Readings older than seven seconds are treated as unavailable. |
| `sensorIntervalSeconds` | integer | `5`. |

AC values describe the controller's stored/requested state. There is no physical
AC feedback: the remote and missed IR packets are invisible. Raw state imports
can preserve opaque/native values; clients should tolerate unknown numeric codes.
Standalone Turbo expiry is serviced by the ESP32 without a browser or fresh
sensor reading. Unrelated commands and Send Again do not extend its timer.
Imported Turbo starts timing when transmitted. Startup clears saved Turbo
locally without sending IR; it does not resume a session with an unknown age.

### `autoCool`

| Field | Type | Meaning |
| --- | --- | --- |
| `enabled` | boolean | Automatic cooling control enabled. |
| `target` | integer | Separate desired target, `16..30` °C; use this for Auto-Cool's setpoint instead of Turbo's operating temperature. |
| `waiting` | boolean | AC is in the automatic recovery cooldown before a possible restart. |
| `waitSeconds` | integer | Remaining cooldown seconds, rounded up; `0` when not waiting. |
| `recoveryUsed` | boolean | Compatibility field: `recoveryAttempts > 0`. It does not lock out another recovery. |
| `recoveryAttempts` | integer | Attempts since sustained successful cooling, saturating at `4294967295`. Clears after a successful 15-minute measurement or five continuous minutes strictly below target while running. Manual commands preserve it; reboot clears it. |
| `recoveryBackoffSeconds` | integer | Full current Off interval when waiting, otherwise the next attempt's Off interval: `300`, `600`, `1200`, or `1800`. Indefinite retries remain capped at 30 minutes Off. |
| `irRetriesRemaining` | integer | `0..2` automatic transmission attempts still queued. Each automatic Turbo/recovery action sends once immediately and repeats twice at two-second intervals. A restart attempt may contain both Power On and target restoration packets. This counts delivery attempts, not AC acknowledgements. |
| `cancelled` | boolean | Sensor loss cancelled a pending recovery; manual restart is required. |
| `saved` | boolean | Combined latest tracked save status for AC and Auto-Cool settings. A command may succeed while this is false and `message` contains a warning. |

See [Auto-Cool behavior](../README.md#room-controller-website) for hysteresis,
the five-minute settling plus 15-minute cooling measurement, persistence, and
repeat-recovery details. Recovery needs a temperature above target +1.5°C
throughout the trial and less than 0.25°C cooling between its first/last
30-second measurement averages. Each restart receives a new full trial; Turbo
expiry/cooldown does not interrupt observation. Missing readings, gaps longer
than seven seconds, target edits, or returning to the hysteresis band restart
the trial.

Automatic repeats use the exact original packet sequence and count toward
`sent`. They do not extend Turbo timers or write flash. Automatic Turbo waits
until the Power On/target repeat sequence finishes. Accepted AC commands other
than `state`, sensor loss, superseding automatic actions, and reboot cancel
pending repeats. Loop delays space remaining repeats from their actual send
time, without catch-up bursts. Manual commands are sent once.

### `lamp`

| Field | Type | Meaning |
| --- | --- | --- |
| `configured` | boolean | A strip address has been selected. |
| `connected` | boolean | Worker-reported BLE connection status; connection can change independently of queued completion. |
| `busy` | boolean | A queued lamp operation is awaiting its result. |
| `ok` | boolean | Operation result when idle. Reset to `true` on queue acceptance, so it does not prove completion while `busy:true`. |
| `message` | string | Setup, progress, completion, failure, or save-warning message. |
| `name`, `address` | string | Saved device name and BLE address; may be empty when unconfigured. |
| `power` | boolean or null | Last explicitly requested strip power; `null` until known. A color/brightness/effect command does not establish power feedback. |
| `color` | string | Last solid RGB color, encoded as lowercase `#rrggbb`. |
| `brightness`, `speed` | integer | Last requested brightness/animation speed, `0..100`. |
| `effect` | integer | Last animation code, `0..212`; meaningful as active mode only when `solid:false`. |
| `solid` | boolean | Whether the last selected mode is solid color. |
| `completed` | integer | 32-bit count of processed worker results, successes and failures, since boot. Includes serial-initiated jobs; not a per-client operation ID. |

The strip keeps its settings after BLE disconnect. It normally disconnects
after 30 seconds idle and starts disconnected after boot. A BLE write can report
success without physical confirmation of the light setting.
The reported light settings come from defaults, restored records, or successful
worker writes; pending and failed control operations do not replace them.

## AC commands

`POST /api/command` returns `200` with a full snapshot and `ok:true` after the
handler finishes, or `400` with `ok:false` for a rejected command. Route-level
validation failures instead return the minimal error body below.

| `command` value | Behavior |
| --- | --- |
| `on`, `off`, `power toggle` | Set/toggle power. |
| `temp 16` … `temp 30`, `temp up`, `temp down` | Change temperature; numeric input is exactly two decimal digits. Up/down clamp at the endpoints. Rejected in Fan-only mode. Updates Auto-Cool's target when enabled. |
| `mode auto`, `mode cool`, `mode dry`, `mode fan`, `mode next` | Select mode; aliases `mode dehumid` and `mode fan-only`. Next cycles Auto → Cool → Dry → Fan-only. |
| `fan auto`, `fan low`, `fan medium`, `fan high`, `fan next` | Select fan; next cycles Low → Medium → High → Auto. |
| `capacity 100`, `capacity 110`, `capacity 80`, `capacity 60`, `capacity 40`, `capacity next` | Select capacity using percentages; next follows this listed order. |
| `horizontal on/off/toggle`, `vertical on/off/toggle` | Swing; choose one of `on`, `off`, or `toggle`. |
| `light on/off/toggle`, `turbo on/off/toggle`, `econo on/off/toggle`, `sleep on/off/toggle` | AC display and mode flags, with the same three alternatives. |
| `autocool on`, `autocool off`, `autocool toggle` | Enable/disable automatic cooling. Enabling from disabled requires AC On/Cool, a valid sensor, and Energy Save/Sleep off. |
| `state` | Return settings without sending IR; prefer `GET /api/state` for polling. |
| `send` | Transmit the current AC packet again. |
| `load <24 hexadecimal digits>` | Validate/import/save the complete 12-byte AC packet without transmitting. Spaces between hex bytes are accepted; use `send` separately to transmit. |

The slash notation in the table lists alternatives, not a literal command:
send `horizontal on`, for example. `help`, `sensor`, network configuration, and
commands prefixed with `lamp` are not AC HTTP commands.

Power commands clamp targets below 24°C to 24°C; send `temp 16` after `on` if
that lower target is intended. Energy Save On selects 24°C/Low; disabling it
keeps those settings. Other mode/capacity interactions follow the AC protocol;
use the returned snapshot rather than assuming only one field changed.

Manual power, mode, fan, capacity, Turbo, Energy Save, Sleep, and raw-state
changes disable Auto-Cool. Swing/display-light changes preserve it. During
recovery (`autoCool.waiting:true`), only `off`, `state`, and `autocool` handling
are allowed through the AC handler; ordinary changes, `on`, `send`, and
`power toggle` are rejected. Use `off` or `autocool off` to cancel the restart
and leave the AC off. State/history reads and lamp operations remain available.

Successful commands can include persistence warnings. `ok:true` means the
handler succeeded; it does not guarantee settings were saved or IR was received.

## Lamp commands and completion

`POST /api/lamp/command` accepts these **unprefixed** values:

| `command` value | Behavior |
| --- | --- |
| `on`, `off` | Request strip power. |
| `color <name>` | Presets: red, green, blue, white, yellow, cyan, magenta, orange, purple, pink, black. |
| `color #RRGGBB` or `color RRGGBB` | Set RGB color and leave animation mode. |
| `brightness <0..100>` | Integer brightness. |
| `effect <0..212>` | Integer animation code; `0` is Auto Play. |
| `speed <0..100>` | Integer animation speed. |
| `connect`, `reconnect` | Connect to the saved strip, reusing a matching live connection; do not replay saved light settings. |
| `disconnect` | Shut Bluetooth down and retain the selected strip/settings. |
| `status` | Return lamp status in a full snapshot; does not queue work or increment `completed`. |

Scan, pairing, UUID overrides, and forgetting are serial-only and rejected over
HTTP. Controls/connect require a selected strip; disconnect/status can be used
without one. A busy lamp rejects another job with **400**, not 409 or 503.

For a queued operation, **202** with top-level `ok:true` means acceptance only:

1. Save the response's `session` and `lamp.completed` value.
2. Poll `GET /api/state` while `lamp.busy` is true; the built-in UI uses about
   750 ms during lamp work and five seconds when idle.
3. In the same boot, a changed `lamp.completed` indicates a processed result.
   Read `lamp.ok` and `lamp.message` when idle. Failures and flash-save errors
   can appear here after the original `202` response.
4. A changed session invalidates that completion tracking. There is no durable
   operation ID or command-result lookup. Other HTTP clients and the serial
   console share this counter; coordinate senders when attribution matters.

`status` is an exception: the route also returns **202** for this successful
read-only command, but no new completion is pending. Prefer `/api/state` when
polling. Some BLE stack operations can stall; acceptance does not promise a
fixed completion deadline, and disconnect is not a cancellation API for a busy job.

## Temperature history

`GET /api/temperature/history` returns oldest-first pages of retained RAM history.
One slot represents five seconds; at most **17,280 slots** (24 hours) are retained.
Null slots preserve sensor failures or missed intervals. Restart clears history.
There is no HTTP history-clear or flash-history endpoint.

| Query parameter | Rules |
| --- | --- |
| `limit` | Optional unsigned decimal integer `1..128`; defaults to `128`. |
| `cursor` | Optional unsigned decimal integer `0..9007199254740991`; requires `session`. It is a sample sequence position, not a timestamp or ring-array index. |
| `session` | Use the boot identifier returned by the previous page. A mismatch causes a reset page, not a 400. Without `cursor`, the request starts from the oldest retained slot. |

Signed/fractional/malformed numeric parameters, invalid limits, embedded NULs,
or a cursor without a session return **400**. An expired/future cursor is valid
syntax and returns a reset page with **200**.

Example first page with `limit=3` while six slots are retained:

```json
{
  "session": "cafebabe",
  "interval": 5,
  "age": 17,
  "first": 0,
  "start": 0,
  "next": 3,
  "end": 6,
  "total": 6,
  "reset": true,
  "more": true,
  "samples": [24.25, null, 24.31]
}
```

| Response field | Meaning |
| --- | --- |
| `session` | Same boot identifier as state responses. |
| `interval` | Sample spacing in seconds, always `5`. |
| `age` | Seconds since sample `next - 1`, the last returned sample during backfill. An empty delta page reports the newest retained sample's age; an empty ring reports `0`. |
| `first`, `end` | Retained cursor interval `[first, end)` at the time this page was produced. |
| `start`, `next` | Returned page interval `[start, next)`. `samples.length == next - start`. |
| `total` | Retained slots, including nulls; `end - first`, at most `17280`. Not this page's count. |
| `reset` | Discard the previous cache and begin from `first`. True on initial reads, session mismatch, or out-of-range cursors. |
| `more` | `next < end`; more slots were available when this page was created. |
| `samples` | Array of Celsius numbers to hundredths (valid range `-55..125`) or `null`. No `ok`, `message`, or `revision` fields on successful history pages. |

### Incremental reading

1. Fetch `/api/temperature/history?limit=128`.
2. If `reset:true`, discard the old cache. Otherwise require the same session
   and expected `start`, append the returned samples, and discard cached cursor
   positions below `first`. Keep at most 17,280 slots, including nulls.
3. Continue with the returned `session` and `next`, for example
   `/api/temperature/history?limit=128&session=cafebabe&cursor=3`.
4. While `more:true`, read another page, leaving room for commands between pages
   (the UI waits 100 ms). Pages are individual snapshots; the ring can advance
   while backfilling, so `end`/`first` need not remain fixed.
5. Once caught up, poll with the latest `next` about every five seconds. An empty
   `samples` array with `more:false` is normal. Advance the cursor only after a
   successfully validated page; reads can be retried without device commands.

### Sample times

The ESP32 returns ages rather than wall-clock timestamps. For a nonempty page,
let `receivedMs` be the client's response-receipt time, `n = samples.length`,
and `i` a page index:

```text
lastReturnedMs = receivedMs - age * 1000
sampleMs[i] = lastReturnedMs - (n - 1 - i) * interval * 1000
newestRetainedMs = lastReturnedMs + (end - next) * interval * 1000
```

An empty delta page with retained history gives
`newestRetainedMs = receivedMs - age * 1000`; an empty ring has no sample time.
During backfill, `age` can be many hours: do not anchor an old page at the current
time. The last expression identifies the newest retained slot when choosing a trailing chart
window. These wall-clock estimates depend on the client's clock and response
latency; they are not device-synchronized timestamps. Keep nulls at their time
positions rather than joining lines across gaps.

## Errors, retries, and client pacing

Errors raised before command handling or by the bounded HTTP handoff can have
only this minimal body; do not require a state snapshot on every error:

```json
{"ok": false, "message": "Controller busy or disconnected. Check state before retrying."}
```

| HTTP status | Meaning |
| --- | --- |
| `200` | State/history read or accepted AC handler operation. |
| `202` | Accepted lamp request; see the asynchronous completion and `status` exception above. |
| `400` | Invalid/missing/too-long command field, invalid history parameters, rejected command, unconfigured/busy lamp, or unavailable lamp queue/worker. May be minimal JSON or a full state snapshot with `ok:false`. |
| `403` | Command Origin mismatch. Minimal JSON: `Use the controller page.` |
| `404` | Unmatched request; plain text `Not found`. |
| `503` | HTTP handoff unavailable/full, request expired before execution, disconnected controller, or oversized/unavailable reply. Minimal JSON with `ok:false`. |
| No complete response | Connection loss, allocation failure, reset, or timeout; command outcome may be unknown. |

- Serialize your commands and leave capacity for state/history reads. There are
  two HTTP command slots and four read slots; commands receive execution priority.
  One lamp job is accepted at a time. No rate-limit/Retry-After contract is supplied.
- Reads queued for two seconds without execution expire, as do queued commands.
  This is a queue deadline, not a BLE operation deadline. Deferred HTTP responses
  also use a ten-second receive-inactivity timeout, not a fixed total duration.
- The built-in UI uses five-second fetch timeouts, cancels background reads when
  sending a command, and stops polling while hidden. These are client choices,
  not guarantees that a cancelled command was never executed.
- Do not automatically replay a command after a lost/503 response. An action
  may already have occurred; `toggle`, `next`, and relative temperatures are
  especially sensitive to repeats. There are no idempotency keys or durable
  command receipts. Read state and resolve the uncertain outcome before retrying.
- A successful state read reports controller state, not proof of physical IR
  reception or lamp behavior. Read-only polling can resume independently of a
  failed command; preserve newer snapshots when older requests arrive later.

## Implementation references

- [HTTP routes, form validation, and Origin check](../lib/ControllerWeb/src/controller_web.cpp)
- [Snapshot assembly and HTTP status selection](../src/controller_app.cpp)
- [AC handler and fields](../lib/AcController/src/ac_controller.cpp)
- [Lamp handler, completion, and fields](../lib/LotusLamp/src/lotus_lamp.cpp)
- [History query formatting](../lib/ControllerCore/src/temperature_history_api.h)
- [HTTP queue limits](../lib/ControllerWeb/src/web_dispatch.h) and
  [deferred response lifetime](../lib/ControllerWeb/src/web_response.h)

Update this reference when endpoint, command, field, or response semantics change.
