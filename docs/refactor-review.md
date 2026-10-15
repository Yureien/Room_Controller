# Refactor review handoff — 2026-10-15

Implementation follows `/tmp/room-controller-refactor-plan.md`. No firmware was
uploaded. Tests, on-device checks, power tuning, alternate power builds, framework
updates, and test migration were explicitly deferred. Earlier unfinished work and
test artifacts were reviewed/preserved in the first commit.

## Completed chunks

| Commit | Change |
| --- | --- |
| `3ce9dca` | Pending reliability work: listener retry/retention, receive inactivity timeout, late allocation cleanup, serial fairness, freshness, browser read cancellation, BLE replacement/scan/teardown checks |
| `943050d` | Core/support/lamp libraries, shared JSON utility, named worker/session helpers |
| `b16f631` | AC and sensor owners, incremental discovery, explicit parasite conversion, independent save outcomes |
| `e2286b7` | Network/HTTP owners, app/console composition, deterministic HTML/CSS/JS embedding, browser boot/edit ordering |
| `a626810` | Microsecond conversion deadline, freshness after loop stalls, NVS ABI offsets, browser revision retention, owner copy guards |

See [source map](../README.md#firmware-components) and
[transport ownership](../README.md#async-http-ownership). The external endpoints,
field names, six dispatch slots/two command slots, 3-KiB replies, two-second queue
expiry, 128-sample history pages, and 24-hour RAM ring are retained. History starts
on the first Ready or Unavailable sensor event; initial discovery remains Waiting.

The normal build uses the existing dependency pins, partition offsets, and default
power-saving flag of zero. Compared with the handoff's last normal build:

| Metric | Handoff | Final refactor | Difference |
| --- | ---: | ---: | ---: |
| Static RAM | 105,552 bytes | 105,620 bytes | +68 bytes |
| Flash | 1,103,329 bytes | 1,100,029 bytes | −3,300 bytes |

Normal integrated and incremental asset builds passed. The final clean normal
build passed and verified generation from absent build output. These are compilation/link
checks, not runtime test results. Build logs remain in `/tmp`, outside commits.

## Paths reviewed

Boot restoration sends no IR and starts no BLE session. The loop services one
serial line, lamp results, one sensor state step, automation/history, networking,
and one web job before power/LED updates. Fresh readings are obtained at each
consumer boundary. AC packets retain before/after activity hooks and the separate
Power On/low-target restoration packet. Automatic Turbo does not save flash;
recovery power saves cannot have their failures hidden by settings-only saves.

HTTP callbacks validate/copy data; the loop takes snapshots and executes commands.
Cancellation after claim cannot undo IR; slot generations guard late completion.
Listener/mDNS startup failures retry independently and log transitions. Browser
reads abort on commands/hidden tabs; aborted reads do not mark the device offline.
Session/revision tracking rejects obsolete state/feedback and detaches old lamp
operations while preserving local unsent/failed edits. Dense connected history
samples form lines while isolated samples retain markers.

Source review covered BLE idle shutdown, Disconnect/Forget, replacement, scan start and
completion failure, connection failure, write failure, and partial teardown.
Shutdown stops the host before deleting its objects. Failed live-host stop retains
objects for retry; a failure after the library has stopped the host latches a fault
and requires a board restart rather than treating the next no-op stop as recovery.
Connect/Reconnect deliberately reuses a matching live connection. The 12-KiB
worker stack is retained.

## Remaining limits and follow-up

- The serial brownout message confirms an electrical reset for some observed web
  interruptions. Software changes do not repair or diagnose the exact supply,
  cable, regulator, or IR-circuit cause. Brownout protection stays enabled.
- NimBLE initialization, host stop, and synchronous GATT internals can still wait
  indefinitely. The outer scan wait is bounded; it does not bound those internals.
  No live task or callback objects are forcibly destroyed.
- The HTTP ten-second receive timeout is inactivity-based. A peer that continues
  sending TCP traffic can extend its lifetime; this is not a total deadline.
- Heap/stack headroom, coexistence, runtime latency, sensor disconnect/reconnect,
  parasite wiring, physical IR reception, and phone BLE handoff need hardware
  verification. Static RAM totals do not establish runtime memory safety.
- Host tests were not run or expanded. Source-extraction Python/JavaScript
  harnesses need path/interface/asset/fake migration; see [test inventory](../test/README).
  No post-refactor test coverage is claimed.
