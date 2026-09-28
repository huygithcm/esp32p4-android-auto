# Diag2: persistent-brake report versus unarmed reverse interlock

User reports braking persists after brake release. Read-only hardware capture:

```text
(ADC1 brk-rel rv-btn rv-armed rv-dir ADC0 fault motor-age ramp-neg)
(0.0  0.0     1      0        0      0.0  0     0.0116    0.1)
```

The decoded brake and brake ramp are both zero. These values do not select the
software brake branch (ADC1>0.05 OR brk-rel>0.001). RX is now recognized as
pressed, but reverse is not armed and direction is INTERLOCK. Motor heartbeat
is fresh; stored ramp-negative setting is0.1 seconds nominal.

Earlier held-brake capture had RXbutton0 / ADC1=1. This new capture has
RXbutton1 / ADC1=0. Neither capture proves RX and brake were recognized together
for the required20 motor iterations. Do not infer the physical button was
released early or that wiring is faulty without the simultaneous capture.

Requested while holding RX plus brake for about1s, stopped with throttle released:

```lisp
(list rv-btn (get-adc-decoded 1) rv-brake-ticks rv-armed rv-dir
      (get-speed) (get-adc-decoded 0) park-on)
```

Source behavior:

- PARK/fault prevents arming; missing reverse enable/readiness/release/button
  clears authorization. Moving beyond standstill threshold prevents new arming.
- Valid RX plus decoded brake>0.05 and throttle<0.05 for20 iterations arms.
- Brake outranks reverse output while held; releasing brake allows reverse
  after brake slew drops to <=0.001. Tiny positive float residual can remain
  below this threshold; it is not an active braking command.
- Ramp uses0.01 per iteration, so the configured duration is nominal, not a
  strict wall-clock bound. A slow loop can extend elapsed time.
- Official VESC6.05 FOC set_current changes control mode from BRAKE to CURRENT.
  No separate brake-clear command is needed. Current0 with refreshed off-delay
  can retain PWM, so zero brake command is not proof of physical freewheel.

[Diag2 source](../release/jc4880-v1.3.8-rc2-diag2-2026-09-28/main.lisp).
[Official FOC source](https://github.com/vedderb/bldc/blob/a0d40e2c5a42c810888d8c379307e6b0a118a125/motor/mcpwm_foc.c#L797).

No production source, release contents, hardware pins or ESC configuration were
changed for this investigation. Await simultaneous hardware capture before a fix.

## Runtime verification

156/156 actual VESC6.05 LispBM checks PASS, heap2464, exit0: previous142 plus14
brake-priority/release cases. ESC APIs are mocked; no physical motor test.
Tests verify last command kind, not historical stored current/brake values.
Maximum5s nominal ramp took approximately7.99s on Windows host, ending with
brk-rel0.000006 and an absolute negative-current command. This demonstrates
iteration-based timing and release at threshold; it is not an ESC timing bound.

[Final log](diagnostics/reverse-brake-diag2-2026-09-28/runtime-605-v2.txt).
Initial runtime-605.txt retained: three test assertions incorrectly demanded
exact zero. Corrected to <=0.001 plus actual output API checks after independent
review; source was unchanged. No permanent brake latch reproduced.

```powershell
python -B scripts/test_lisp_runtime.py --vesc-version 6.05 --heap-cells 2464 --source release/jc4880-v1.3.8-rc2-diag2-2026-09-28/main.lisp --fault-diagnostics --rx-startup --reverse-flow --reverse-brake --cc C:/msys64/mingw32/bin/gcc.exe
```
