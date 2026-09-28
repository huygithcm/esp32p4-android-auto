# M-TEMP conversion replay

Run from the repository root:

```text
python scripts/test_mtemp_conversion.py --cc <host-gcc>
python scripts/test_mtemp_conversion.py --cc <host-gcc> --vesc-version 6.05
```

The runner downloads/verifies six SHA256-pinned files per version from official
VESC 7.00 commit `20cbb362687291242ab90b99f25fbfe8835540fc` (default), or 6.05
commit `a0d40e2c5a42c810888d8c379307e6b0a118a125`. The 6.05 option follows the
user's confirmed firmware version; the physical board is still unknown.
It extracts the original
motor sensor switch, invalid-value guard, low-pass expression, sensor macros
and float16 serializer without rewriting their logic. Whole current P4 RT/IO
decoders and buffer helpers are linked. The actual cockpit temperature function
and unit-conversion function are extracted unchanged; LVGL label/settings and
RTOS/CAN boundaries are fixtures. Compilation/execution uses a unique temporary
directory, no production or release files are modified.

**Hardware assumption is explicit:** example Trampa VESC6 macros, 10k pull-up,
beta3380, 3.3V illustration, LPF0.01, motor1. The ADC array indices are synthetic
so independence can be controlled. This is NOT identification of the user's
ESC, a model of analog coupling, a motor2 test or measured voltage data.

Results: **603 checks, 0 failures for each of 6.05 and 7.00**. The tested
conversion, sensor macros, filter and serializer blocks match between versions.

- Starting the filter at0 and holding invalid open-rail input settles at
  `-99.9996185`; official serialization truncates to `-99.9`, actual UI to`-99`.
- Starting exactly at`-100` produces label`-100`: `-99` is conditional, not a
  universal clamp. Invalid-to-valid midpoint recovers without resetting filter.
- Independent EXT sweep plus ADC32 packets leaves temperature unchanged;
  assertions verify decoded ADC level/voltage actually change.
- Controlled changes to the **temperature ADC itself** can produce displayed
  `2/1/2/-99/-17` with the original conversion. Example input voltages are
  deliberately chosen by searching the formula, not inferred from the video;
  this shows possibility, not independent evidence of physical causation.
- Disabled ignores ADC and uses the override value. Non-finite/out-of-range
  values take the original fallback. All eight enabled sensor branches are
  exercised at both ADC rails using the example macro family.
- Original Celsius/Fahrenheit and secondary-head formatting is exercised.

Do not turn these checks into a claim that a throttle pin and temperature pin
are connected on the unidentified ESC. Exact board source, Sensor Type and
simultaneous raw ADC/temperature observations are still required.
