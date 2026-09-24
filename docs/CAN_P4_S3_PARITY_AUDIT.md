# CAN P4 / S3 parity audit — 2026-09-24

## Scope and evidence

Read-only comparison of P4 `fix/gear-circle-park-reverse` at `7c77f2b`,
the separate `vesc-display-s3` clone at `d311601` on `esp32s3-port` (including
its existing dirty files), and source seed `port/esp32s3-vesc-bms` at `6642136`.
The separate clone is ahead of the P4 repository's local `esp32s3-port` ref.
No firmware source in either working tree was changed by this audit.

The S3 clone has a real BSP, native BLE integration and startup wiring.
Its collaboration log records an ESP-IDF 5.5.3 build and COM9 flash on
2026-09-04. Its existing app binary is 2,161,872 bytes. These are historical
artifacts, not a new build or proof of physical CAN operation in this audit.
`PORT_README.md` still says hardware untested and about 2.6 MB: this does not
fully reflect the later flash log or current artifact size.

## Bus and application structure

```text
S3/P4 TWAI TX/RX -> CAN transceiver -> CAN H/L -> VESC
   RX task -> frame processing -> buffer reassembly + CRC16
      -> main.c:vesc_packet_dispatch
         -> telemetry -> UI updater -> LVGL dashboard
         -> config transport -> firmware-specific config table
         -> Lisp stats/console/upload/panel/ride-mode parsers
         -> BLE NUS response bridge
   one RT task schedules continuous request/reply polling
      -> telemetry / IO / Lisp / ride mode / PAS dispatch
      -> CAN fragmentation -> TWAI TX

JK BMS -> BLE notifications -> bms_jk parser/model -> BMS UI
```

BMS data in this implementation uses BLE, not the VESC CAN packet parser.
PAS requests go to the Lisp arbiter on the ESC; the display is not a substitute
for the ESC's propulsion interlocks.

- Classic CAN, extended 29-bit identifier: `(packet_type << 8) | node_id`.
  Node interpretation depends on packet type: destination for addressed
  transactions; source for status broadcasts.
- Configured S3 defaults: 500 kbit/s, display ID 2, target ESC ID 10,
  RT interval 100 ms, initial polling holdoff 4000 ms. NVS settings may override
  IDs and speed; record runtime values when comparing devices.
- Short payloads fit PROCESS_SHORT_BUFFER. Longer payloads use FILL_RX_BUFFER
  (7 bytes/chunk), FILL_RX_BUFFER_LONG (6 bytes/chunk), then PROCESS_RX_BUFFER
  containing sender, routing mode, length and CRC16. Reassembly capacity 1024 B.
- Preserve the single continuous-poll task and reply waiting. Do not create
  independent UI polling loops during the S3 port.
- P4 JC4880 pins are TX51/RX52; actual S3 configuration is TX20/RX19.

## Cloned board references: distinguish LCD7 from LCD7B

The S3 clone contains both official examples under
`research/_sources/waveshare_s3_lcd7` and `waveshare_s3_lcd7b`.
The implemented BSP matches **LCD7B CAN initialization**:

- LCD7B `examples/ESP-IDF/04_CAN/components/can/can.h:24`: TX20/RX19.
- LCD7B `examples/ESP-IDF/04_CAN/main/main.c:23`: 500 kbit/s, NORMAL mode;
  `IO_EXTENSION_Output(IO_5, 1)` selects CAN.
- LCD7B expander uses I2C address 0x24 with registers 0x02 and 0x03.
  S3 BSP `waveshare_esp32_s3_touch_lcd_7.c:136` implements this and
  `:179` selects the CAN mux; S3 `main/main.c:412` calls it before TWAI starts.
- The older LCD7 example uses the CH422G single-byte I2C protocol and its
  CAN demo uses 50 kbit/s NO_ACK. Those are not the runtime settings to copy
  for LCD7B + VESC. Confirm physical board revision before relying on the BSP.
- USB and CAN share GPIO19/20 through the mux. Switching to CAN disconnects
  native USB; use the documented UART0 path for observation. Emulator mode
  skips the explicit mux call and physical CAN, but does not reliably preserve
  USB: `ioext_shadow = 0xFF` at BSP line 41 already sets EXIO5. Display/touch
  writes at lines 154-162 write the whole shadow with EXIO5 high. No explicit
  USB selection clears it. Fix initial output state and deliberate mux policy
  before relying on emulator USB access; this is a source finding, not a new
  measurement of the physical mux.

## Findings and required integration

| Priority | Finding | Required action |
|---|---|---|
| High | S3 `main/main.c:262` changes RT/IO/Lisp/panel target but omits `vesc_ride_mode_set_target(new_id)`; P4 includes it at `:271`. Ride requests can still address the old ESC after telemetry moves. | Port the target-change handling together with queue/cache invalidation; test with two distinct ESC IDs and delayed replies. |
| High | Actual clone `vesc_ride_mode.h:16` uses config format 1; P4 uses format 2 at `:33`. S3 lacks P4 safety/PARK and sequenced STATUS request 0x0D/reply 0x8D. | Move compatible backend, wire headers/parser, panel, UI and ESC Lisp as one versioned set. Do not pair old S3 UI/backend with new P4 Lisp and assume compatibility. |
| High | S3 `lisp/main.lisp:774` describes native ADC resuming after Lisp dies; `:787` uses rolling `app-disable-output 1500`. P4 instead requires native ADC NONE and implements fault/watchdog paths. | Use the current P4 safety design and validate actual ESC configuration, timeout and monitor/script failure under propulsion inhibit. |
| High | S3 updater uses legacy DASH for gear/reverse instead of P4 dedicated fresh safety state. | Port `ride_gear_state.h`, updater and corresponding custom UI; reject stale safety and delayed ACK. Current P4 safety freshness is 1000 ms. |
| Medium | Source equality cannot establish bus arbitration or scheduler behavior. Fragment reassembly shares destination-keyed storage; TX locking is per frame, not full request/reply ownership. This is inherited P4 behavior, not a new S3 defect. | Stress concurrent BLE NUS/config/upload/unsolicited replies and helper traffic. Observe CRC errors, dropped replies, queue pressure and recovery; redesign transaction ownership if reproduced. |
| High | Shared CAN decoder reads FILL payload bytes before checking minimum DLC (`comm_can.c:508` onward); subtracting an undersized length can feed a negative length to `memcpy`. | Add minimum-DLC/RTR rejection and malformed-frame host coverage to the shared transport before fault-injection acceptance. Existing ride parser tests do not exercise this decoder. |

Actual clone files `comm_can.c`, `packet_parser.c`, `vesc_rt_data.c` and
`vesc_config_transport.c` match P4 byte-for-byte; the complete config component
also has no diff. Main startup already initializes parsers, installs dispatch,
starts the RT task, then configuration probing. Keep that ordering.

The newer source seed `6642136` contains the P4 CAN/config/ride/Lisp/updater
implementation unchanged, but has no boot/BSP/native BLE integration.
Use it or P4 `7c77f2b` as the source for narrowly scoped integration into the
working S3 port. Preserve the S3 clone's existing BSP/display/native BLE work;
do not replace its whole tree or merge unrelated root histories.

## Verification performed now

Built fresh host executables under `build/can_s3_audit` from current P4 sources:

| Suite | Result |
|---|---|
| Limited Lisp host evaluator | 13/13 branch cases |
| Ride transport with platform mocks | 53 checks, 0 failures; 0 sends while state lock held |
| Ride parser / safety / freshness | 2532 checks, 0 failures |
| Gear mapping | 81 checks pass |
| JK BMS synthetic frame parser | 0 failures |
| ESC configuration serdes 6.05 / 6.06 / 7.00 | all signatures / default CRC / round trips pass |

These are P4 reference tests, not passing tests of the old S3 ride backend.
No physical CAN traffic, ESC actuation, new flash or S3 rebuild was performed.

## A/B acceptance sequence

1. Record board revision, ESC firmware, exact Lisp hash, display firmware hash,
   runtime IDs/speed, wiring, termination and emulator OFF. Use the same ESC
   and harness for sequential P4/S3 comparisons. Two displays must not share
   a CAN ID if attached together; independent controller traffic needs its
   own explicit test.
2. With propulsion inhibited, verify S3 mux selection and TWAI NORMAL 500k,
   frames/ACK and absence of persistent error or bus-off. Verify recovery
   after disconnect/reconnect rather than judging only startup.
3. Compare telemetry values/units and update age: voltage, current, ERPM,
   temperature, distance, then config firmware detection/readback. Confirm
   stale/disconnected UI, not just a populated gauge.
4. Change target ESC ID; every RT/config/Lisp/ride/PAS path must address the
   intended node. Delayed old replies must not refresh current gear authority.
5. After compatible backend/UI/Lisp integration, verify current ceilings,
   P entry/exit, reverse guards, old/duplicate ACK, 1000 ms display safety
   freshness, ESC reboot and settings persistence. Set ESC-specific timeout
   and acceptance limits before powered testing.
6. Exercise BLE NUS/config traffic and Lisp upload with their polling gates;
   add helper/PAS/BMS BLE traffic and display redraw load. Record latency,
   stale transitions, CRC errors, bus-off and recovery for both P4 and S3.
7. Validate native ADC NONE, throttle/brake/PAS arbitration and script/monitor
   failures under the existing hardware inhibit plan before any powered run.

Status: source/reference audit complete; S3 safety parity and CAN hardware
acceptance remain open. The user's P4-running baseline is useful, but does
not independently establish that the newest P4 P/R revision has passed every
fault case above.
