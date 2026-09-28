# RX startup regression evidence

Hardware diag1 reported:

```
(5 12 5 18288522u32 0.000800f32 1828.852173f32 1828.852417f32)
```

Actual VESC 6.05 LispBM on the host reproduced the same failure structure:

```
(5 12 5 3812261116u32 0.000006f32 3812.261230f32 3812.261230f32)
```

The tick scales differ between host and ESC. In both cases TX is fresh while
RX and motor ages are approximately uptime: initial heartbeat value zero.
The old source publishes RX ready after GPIO configuration, before its first
successful input read. A motor iteration in that interval latches input fault.
Later healthy heartbeats cannot clear the latch, so the dashboard remains `-`.

The fix publishes heartbeat and readiness atomically after the first read.
No watchdog threshold or latch policy changes. Initial RX read failure remains
unsupported reverse hardware; after readiness, reverse-enabled stale monitoring
continues to latch input fault and zero current.

Tests load valid EEPROM reverse configuration and hold the first RX read while
allowing the motor context to run. They use the complete source, real upstream
scheduler and original host clock. No production heartbeat is assigned by tests.
ESC APIs are fixtures, so tests establish the code race, not physical driver timing.

The initial harness attempt rebinding a clock builtin stalled; its log is kept
as `rx-startup-fixture-clock-timeout.txt`. It is not regression evidence. The corrected
baseline log is `rx-startup-baseline-605-v2.txt`.

Hardware follow-up on diag2: user returned `(0 0 0 0 0.0 0.0 0.0)` (float32
format in the REPL). This confirms healthy PARK and no recorded fault in that
observed run. It does not yet confirm repeated startup, mode operations, reverse
operation, or M-TEMP behavior.
