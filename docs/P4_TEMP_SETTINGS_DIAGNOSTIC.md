# M-TEMP changes and inaccessible Settings: diagnostic cases

2026-09-25. Compare current P4 working tree with e7f0b16.
User clarification: cannot ENTER Settings, not a failed ride-mode Save.
This distinction supersedes interpreting the symptom as mode-protocol failure.

## Temperature data paths (unchanged in both versions)

Primary path:
CAN PROCESS_RX_BUFFER (CRC checked) or PROCESS_SHORT_BUFFER ->
main/main.c:vesc_packet_dispatch -> vesc_rt_data_process_response ->
s_rt_data.temp_motor -> main/vesc_ui_updater.c:push_rt_locked ->
update_temp_motor -> dashboard_theme dispatcher -> cockpit_temp_motor -> label.

For selective SETUP (command 51), when both temperature mask bits are present,
MOS temperature uses payload bytes 5..6 and motor temperature bytes 7..8,
signed big-endian int16 divided by 10. Full SETUP (command 47) uses bytes 1..2
and 3..4 respectively. Offsets include the command byte and depend on mask.
The classic widget truncates to integer after C/F conversion; it does not
clamp to -99 or substitute ADC. For example, -99.0 C is raw int16 -990 (FC22).
An on-screen -99 can also result from truncating another value near -99 C.

Other writers:
- Init/reset or leaving demo: zero.
- Firmware emulator: simulated temperature from simulated load, starts 22 C;
  physical throttle is not its input.
- Dashboard demo: time-driven 32..68 C.
- Second ESC STATUS_4: shown as primary/secondary, not overwriting primary.

## Host cases actually executed

Generated isolated harnesses from the actual temperature parser and freshness
functions in old/current sources: build/temperature_audit/{old,current}.c.
Compiled with host GCC and actual buffer.c. Each passed 309 checks.

| Input case | Observed old and current behavior | What it establishes |
|---|---|---|
| Sweep motor current 0..100 while temperature stays -99 | Temperature stays -99 | Current field is not mapped to temperature |
| Interleave decoded ADC packets through RT parser | Temperature unchanged | ADC command is filtered, no direct substitution |
| Change incoming temperature 20 -> 80 | Stored motor temperature changes 20 -> 80 | Upstream reported temperature controls display source |
| Expire data, then send only full SETUP command byte | Old temperature marked fresh again | Confirmed malformed-packet freshness defect |
| Cut tail after both temperature fields | Temperature decoded; old later values retained | Partial snapshots accepted; tail loss does not shift early M-TEMP bytes |
| Deliberately wrong mask with unrelated int16 in temperature slot | Those bytes interpreted as temperature | Synthetic sender-layout mismatch can reproduce false temperature; not observed on hardware |

These are parser tests, not live CAN, actual throttle input, or LVGL tests.
No single end-to-end cause reproducing BOTH customer symptoms has been proven.

## Settings entry path and source-derived reproduction cases

GT911 read -> single-touch/rotation/routing -> LVGL CLICKED ->
dashboard_Classic_Settings_text_event_handler -> ui_load_scr_animation.
There is no throttle, speed, PARK, VESC connection, or Lisp-version gate here.
Navigation and touch-routing sources are identical to e7f0b16.

| Case to try | Expected distinction | Evidence/status |
|---|---|---|
| Tap center (678,18), then lower edge of Settings | Center opens but lower edge may adjust brightness/intercept click | Label x610..746,y5..33; later invisible slider x624,y40 with LV_DPX(8) hit expansion; source-derived, not device-tested |
| Close Lisp drawer then tap Settings again | First tap dismisses scrim; next reaches Settings | Full-screen clickable drawer scrim; source-derived |
| Inspect touch count while touching once | count >=2 or latched gesture suppresses ordinary clicks | touch_input accepts cnt==1 only; source-derived |
| Inject tap at (678,18) if debug bridge enabled | Injection works but physical tap fails -> input/rotation/routing | Proposed hardware discriminator, not executed |
| Compare Statistics/VESC taps and live clock/telemetry | All taps fail but values update -> input path more likely than total LVGL freeze | Diagnostic inference, not proof |
| Inspect mode logs after AA transition | UI/touch consumer mismatch possible if display-lock timeout occurs | Existing ui_mode_set stores UI mode before lock, returns before touch update on failure; unchanged |

No need to bypass park or energize the motor for these UI/temperature checks.

## Plausible combined scenarios (require hardware evidence)

1. ESC sensor/wiring/configuration produces throttle-correlated temperature,
   while touchscreen has a separate input problem. Direct VESC Tool temperature
   agrees with P4; direct/physical UI tests isolate the second fault.
2. Electrical noise/ground disturbance under throttle affects both ESC analog
   temperature input and P4 touch. More plausible if touch fails ONLY during
   throttle movement/load, recovers at idle, and touch count/errors change.
   This is a hypothesis; ESC hardware/pin sharing is not known.
3. Valid SETUP replies from multiple ESCs overwrite the primary snapshot:
   dispatcher has no sender argument/filter. Requires multiple reply sources;
   not established by one photo, and does not itself disable Settings.
4. Bus reassembly/drop/partial-data issues plus unrelated UI failure. CAN
   reassembly checks CRC for long packets, so ordinary corruption should drop
   rather than reliably map throttle to temperature. Raw frames are required.

Old Lisp alone cannot directly block Settings navigation or remap temperature.
New P4 + old Lisp can independently produce the '-' gear indicator, which
must not be confused with the reason Settings cannot open.

## Minimal customer check

With propulsion inhibited, compare idle versus moving throttle:
1. Direct VESC Tool Motor Temperature + ADC1 voltage, alongside P4 M-TEMP.
2. Center Settings tap, Statistics tap, VESC tap; record whether clock/data move.
3. Touch raw coordinates/count/errors and click event if logs/debug available.

If both VESC Tool and P4 temperatures move, start at ESC input/sensor side.
If only P4 moves, inspect raw SETUP temperature bytes, mask and source ESC ID.
If touch injection works, inspect GT911/routing rather than Lisp. If neither
physical nor injected click works, inspect overlay/event delivery/screen load.
