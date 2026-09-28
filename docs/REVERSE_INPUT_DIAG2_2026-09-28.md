# Diag2 reverse entry: RX input not asserted

Hardware tuple, in the requested stopped/throttle-released/brake-held condition:

```text
(1i32 0 0 1 1 0 0 0 1 0.000000f32 1.000000f32 0.000016f32)
```

| Field | Value | Meaning |
|---|---:|---|
| rm-rev-en | 1 | Reverse enabled |
| park-on | 0 | Outside PARK |
| safety-fault | 0 | No latched input fault |
| rv-hw-ok | 1 | RX first read completed |
| rv-seen-release | 1 | Debounced release has been observed |
| rv-btn | 0 | Reverse button is NOT recognized as pressed |
| rv-brake-ticks | 0 | Arming timer has not started |
| rv-armed | 0 | Reverse not authorized |
| rv-dir | 1 | Forward direction |
| ADC0 | 0 | Throttle released |
| ADC1 | 1 | Brake fully asserted |
| speed | 0.000016 m/s | Below standstill threshold |

[reverse-step](../release/jc4880-v1.3.8-rc2-diag2-2026-09-28/main.lisp#L778)
short-circuits on rv-btn=0 before the brake arming branch. This accounts for
remaining Mode2 without R or the interlock dash at the captured instant.
It does not by itself prove a broken switch, wiring fault or incorrect pin.

The script reads a separate active-low UART RX input: raw0 means pressed,
raw1 means released. Mode uses UART TX and does not request reverse.
Official6.05 maps pin-rx to HW_UART_RX_PORT/HW_UART_RX_PIN; exact connector
location depends on the ESC hardware definition. No connector pin is guessed.

Requested paired read-only samples, released and held for at least0.2s:

```lisp
(list (gpio-read 'pin-rx) rv-btn-raw rv-btn (secs-since rv-seen))
```

Expected after debounce: release `(1 0 0 fresh-age)`, hold `(0 1 1 fresh-age)`.
If raw remains1, verify the actual reverse switch and RX path before changing
logic. If raw0 but debounced0 persists, inspect count and heartbeat/scheduling.
No output commands, polarity changes, interlock bypass, or config writes issued.

Reference audit: official6.05 min-speed uses -fabs(value)*speed_fact; the existing
positive speed magnitude is correct. No sign change is justified. Measured ADC1
also confirms brake decoding is available in this hardware sample.

## Confirmed follow-up and runtime checks

User confirmed the raw tuple `(1 0 0 0.006400f32)` was read while holding the
reverse button and brake. GPIO is high, both raw/debounced reverse flags are0,
and heartbeat is fresh6.4ms. This proves the configured RX input is not asserting
an active-low reverse request in that sample. It does not identify whether the
cause is switch/wiring, wrong connector, polarity, or competing pin ownership.
Requested ESC model/board and actual switch terminal labels before pin guidance.

142/142 actual VESC6.05VM checks PASS (heap2464), exit0: previous127 plus15
reverse-flow checks. Real monitor/motor/event code keeps Mode2 for RX released
and brake asserted; recognized RX with no brake enters interlock; stopped idle
throttle plus recognized RX/brake arms reverse. Releasing brake and requesting
40% throttle commands -2.8A with optional delay0.2 and 7A configured reverse cap.
This observes an ESC API command, not physical motor rotation.

[Full runtime log](diagnostics/reverse-diag2-2026-09-28/runtime-605.txt).

```powershell
python -B scripts/test_lisp_runtime.py --vesc-version 6.05 --heap-cells 2464 --source release/jc4880-v1.3.8-rc2-diag2-2026-09-28/main.lisp --fault-diagnostics --rx-startup --reverse-flow --cc C:/msys64/mingw32/bin/gcc.exe
```

Only test fixtures/runner and documentation changed in this investigation.
No production source, release package, GPIO configuration or motor command on
hardware changed. Hardware input mapping remains pending.
