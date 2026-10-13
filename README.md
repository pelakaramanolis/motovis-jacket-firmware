# MotoVis Jacket Firmware

ESP32 firmware for a motorcycle safety jacket: it watches the rider's
acceleration, heart rate, position and battery, decides when a crash has
happened, gives the rider a few seconds to cancel, and publishes everything
over Bluetooth Low Energy to a companion phone app.

## What this repository is

This is a rebuilt version of a university capstone project that was
originally built by myself and two other students. The original lives at
[Aaron-9009/MotorVis](https://github.com/Aaron-9009/MotorVis).

I rebuilt it to understand every layer of it, including the parts I did not
originally write. The BLE wire protocol (service and characteristic UUIDs,
packet layouts, descriptors) is byte-for-byte identical to the original, so
the existing Android app would work against this firmware. Where this
version differs, it is either a bug fix or an addition, and the reason for
each is given below.

The hardware is not available to me. The physical jacket stayed with my
teammate. Every sensor and the BLE radio therefore run against simulated
sources, and the firmware is exercised in QEMU and by host-compiled unit
tests. See [What is and isn't tested](#what-is-and-isnt-tested).

## What it does

| Subsystem | Hardware | Rate | What it produces |
|---|---|---|---|
| Accelerometer | ADXL345 over I2C | 10 Hz | 3-axis milli-g, crash detection |
| Heart rate | analog pulse sensor on ADC1 | 100 Hz | BPM with a validity flag |
| GNSS | serial receiver on UART2 @ 9600 | streaming | lat/long as degrees x 10^7 |
| Battery | ADC | 1 Hz | millivolts and estimated percentage |
| Buttons | 2 GPIO, active low | 5 ms poll | debounced press/release events |
| Status LED | GPIO | - | boot / advertising / connected / error |
| BLE | ESP32 radio, Bluedroid | - | 5 characteristics, read + notify |

Six FreeRTOS tasks plus the main loop, on one core, nothing starved.

## Architecture

The core idea is that **data has three shapes**, and most bugs in systems
like this come from storing one shape as another.

### State - snapshots behind a mutex

Battery percentage, current position, current heart rate. There is a
"right now" value, and missing a reading costs nothing because the next one
is just as good.

Each manager owns a struct, guards it with a mutex, and hands out copies:

```c
bool battery_manager_get_latest_sample(battery_sample_t *out);
```

Every snapshot carries a timestamp, a sequence number and a validity flag.
The sequence number lets a reader tell a fresh sample from one it has
already seen; the validity flag means "this reading exists but don't trust
it".

### Events - a queue, because they happen once

A crash. A button press. Gaining or losing a GPS fix. There is no "current
value" to re-read. If one is missed, it is gone forever.

Everything that happens goes onto a single shared `event_bus`: one FreeRTOS
queue, one event struct with a `kind` tag and a union of payloads.

One bus rather than one queue per producer, because **order between
producers is the point**. A crash followed by a cancel press means the rider
is fine. A cancel press followed by a crash means a real crash nobody
cancelled. Separate queues cannot distinguish those; one ordered stream can.

The bus assigns the timestamp and sequence number itself, under a mutex that
covers stamping and enqueueing together, so sequence order and queue order
can never disagree.

### Derived state - a state machine over the event stream

"An alert is pending and has 3.2 seconds left" is neither a sensor reading
nor an event. It is a conclusion built from a sequence of events plus the
passage of time.

`alert_state` is that machine: IDLE -> PENDING on a crash -> CANCELLED if
the rider presses cancel inside the window, or CONFIRMED if the window
expires. It takes two kinds of input, and the second one matters:

- `alert_state_event()` - something happened
- `alert_state_tick()` - nothing happened, and a deadline may have passed

A window expiring is not an occurrence. No sensor fires, no interrupt, no
event is published. Without the tick, a crash with no cancel press would sit
in PENDING forever.

A confirmed or cancelled alert **persists until the consumer acknowledges
it**, rather than clearing itself. See the bug below for why that matters.

## The bug this rebuild fixes

The original signalled a crash by setting a boolean in the accelerometer's
shared snapshot. The accelerometer task runs at 10 Hz, so the next sample
overwrote the flag 100 ms later. A reader polling at 400 ms had to look
during that window or the crash never happened as far as the rest of the
system was concerned.

In practice this loses **100% of alerts, not the 75% you would predict**.
400 ms is an exact multiple of 100 ms, so the reader lands on the same point
in the producer's cycle every single time and repeats the identical miss
forever. Had the periods been unrelated - 100 ms against 430 ms, say - the
phase would drift and some alerts would be caught. Being an exact multiple
is what turns intermittent loss into total loss, and it is why the bug never
showed up as flakiness.

The firmware keeps both paths side by side, deliberately, so the contrast is
visible in a single run:

```
E  MISSED 1 alert(s) - flag was already gone. missed 1 of 1 so far
W  event: CRASH 5000, 0, 1000 mg (seq 5, at 4581 ms)
W  alert: pending
E  alert: CONFIRMED - would send 5000, 0, 1000 mg from 4581 ms
```

Same crash, same reader, same cycle. The flag lost it; the queue delivered
it with its payload intact. Set `DEMO_SHOW_FLAG_LOSS` to 0 in `main.c` to
turn the instrumentation off.

## Repository layout

```
firmware/
  main/                  app_main, the supervisor loop, status LED
  components/
    event_bus/           the shared event queue
    alert_manager/       alert_state - pure crash/cancel state machine
    accel_manager/       ADXL345 driver + crash_detect (pure)
    heart_manager/       ADC sampling + pulse_detect (pure)
    gnss_manager/        UART + nmea parser (pure)
    battery_manager/     ADC + discharge curve
    jacket_controls/     GPIO polling + debounce (pure)
    ble_server/          Bluedroid GATT server, UUIDs, packet layouts
test/                    host-compiled unit tests, no hardware needed
```

Every piece of decision logic - crash detection, pulse detection, NMEA
parsing, debouncing, the alert state machine - is a **pure function over a
struct**, in its own file, with no FreeRTOS and no ESP-IDF dependencies.
That is what makes it testable on a laptop.

## Building and running

### Unit tests (no hardware, no ESP-IDF)

```bash
cd test
make test
```

84 checks across 5 suites, compiled by the host compiler, under a second.

### Firmware

Requires ESP-IDF v6.1.

```bash
cd firmware
idf.py build
idf.py qemu monitor               # runs in QEMU, no board needed
idf.py -p PORT flash monitor      # real hardware
```

### Simulation switches

The committed defaults run entirely in simulation. Each sensor manager has a
single define at the top of its .c file selecting between its simulated
source and its real driver, and the BLE server has one for the radio. To
list them:

```bash
grep -rn "USE_SIMULATED\|BLE_USE_RADIO" firmware/components/*/*.c | grep define
```

Set them to hardware values for a real board. The simulated sources sit
behind the real drivers' interfaces, so the detection logic, tasks, queues
and state machines being exercised are exactly the ones that would run on
hardware.

`BLE_USE_RADIO` is off by default because QEMU has no Bluetooth controller,
and `esp_bt_controller_init` **asserts** rather than returning an error when
it cannot find one. There is nothing to catch, and the device reboots in a
loop. Error handling only works where the layer beneath you agreed to report
errors instead of aborting.

## BLE interface

Device name `MotoVis Jacket`. One custom primary service, 128-bit UUID. The
name goes in the advertisement and the service UUID in the scan response,
because together they exceed the 31-byte advertising limit.

| Characteristic | Size | Contents |
|---|---|---|
| GPS | 13 B | timestamp, lat x 10^7, long x 10^7, fix valid |
| Heart rate | 9 B | timestamp, BPM, raw ADC, valid |
| Battery | 10 B | timestamp, millivolts, raw ADC, percent, valid |
| Accelerometer | 11 B | timestamp, x/y/z milli-g, alert byte |
| **Crash alert** | 20 B | crash time, lat, long, x/y/z milli-g, flags, sequence |

All read + notify, each with its own CCCD so a phone can subscribe to
individual streams. All layouts are packed structs with `_Static_assert` on
their sizes, so if one ever changes size the build fails rather than the app
silently misreading every field.

The crash alert characteristic is new in this rebuild. It is **exactly 20
bytes** so that it fits a notification at the default ATT MTU of 23 without
depending on MTU negotiation succeeding. The firmware does request 247, but
that is a ceiling the client has to agree to, and the one packet that must
never fail to send is the one that should not depend on a negotiation.

A phone that was not connected when the crash happened still gets it: the
characteristic's stored value persists in the attribute table, so it reads
the last alert on connecting. Notify is the event; the attribute value is
the state.

## What is and isn't tested

Tested, and demonstrated in QEMU:

- Crash detection against four recorded motion traces, including a
  90-degree lean in 200 ms that must *not* trigger
- Pulse detection at 60 and 120 BPM, with dicrotic-notch rejection, flat
  signal and noise-without-pulse cases
- NMEA parsing including bad checksums, sentences split across reads,
  southern and western hemispheres, and oversized-sentence recovery
- Debouncing: bouncy edges, short glitches, and chatter that never settles
- The alert state machine: confirmation, cancellation, late cancellation,
  repeated impacts, and correct behaviour across the 49-day millisecond
  counter wrap
- All of the above running concurrently in QEMU, six tasks on one core

Not tested:

- **Anything on real hardware.** No I2C device, no ADC input, no UART
  receiver and no radio has ever been attached to this build
- **BLE end to end.** The GATT server compiles and links, and the API usage
  is correct for ESP-IDF 6.1. It has never advertised, been connected to, or
  sent a notification
- Power consumption, thermal behaviour, RF range

## Known gaps

Named here rather than left to be discovered:

- **No BLE security.** No pairing requirement, no bonding, no encryption,
  and a public (trackable) device address. For a jacket broadcasting a
  rider's live position that is a real problem, not a theoretical one.
  Requiring encryption on the characteristics and using a resolvable private
  address is the first thing I would add
- **The ADXL345's FIFO is unused.** The sensor produces 100 samples per
  second and the firmware polls at 10 Hz, so 90 of every 100 are discarded.
  A crash's peak can be missed entirely between polls
- **The event bus has exactly one consumer by construction.** A FreeRTOS
  queue is point-to-point, so a second reader would steal half the stream.
  Genuine fan-out needs per-subscriber queues
- **Alert confirmation is quantised to the 400 ms main loop**, so it can
  fire up to 400 ms after the real deadline. Fine for a 5-second window,
  unacceptable for a short one
- **The Feather V2 I2C pinout has not been verified** against the datasheet;
  the current pins are DevKit defaults
- Battery raw-ADC values are not exposed by the manager, so that field in
  the BLE battery packet is always zero

## Attribution

Original capstone firmware by a three-person team (Aaron Guerrero, Pelagia
Karamanolis, Paul William Bradley). This rebuild is my own work, written to
understand the whole system; it reuses the original's BLE protocol design
and overall subsystem decomposition, and reimplements everything else.
