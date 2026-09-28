# M-TEMP UI call-path checks

Run `python scripts/test_mtemp_ui.py --cc C:/msys64/mingw32/bin/gcc.exe`.

The runner extracts four **unaltered** functions from `main/vesc_ui_updater.c`
and `update_cb` from `Super_VESC_Display/custom/realtime_viewer.c` at runtime,
printing each block's SHA256. `--baseline-ref 7c77f2b` uses matching production
sources and RT/IO datatype headers from the selected commit. These execute with whole
production RT/IO decoders, buffer helpers, `vesc_head2.c`, and the production
gear mapping header. Boundary stubs capture UI setter arguments; unrelated
battery/trip/range/clock/BLE and RTOS services are mocked. All compilations use
unique temporary directories and `-Wall -Wextra -Werror`.

This is not a full updater translation-unit build, LVGL rendering, thread-race
test, CAN transport/reassembly test, or physical ESC validation. Raw temperature
fixtures are deliberately selected to exercise the video-like values; they are
not packets captured from that video. Formatter tests are separate.

The first five cases verify independence of ADC/current/gear and temperature,
stale-data hold/recovery, off-dashboard gating, demo exit, and secondary-head
ID/freshness (including tick wraparound). Temperature setter calls and changed
input values are asserted to avoid tests passing because all input is ignored.

The former `observe_*` cases now require rejection of truncated command47
and independent thermal ages: mask-zero cannot refresh the RT snapshot,
current-only updates current without reviving stale thermal samples, and
FET-only updates cannot revive motor temperature. Logging/persistence callbacks
continue during thermal-stale UI updates: the existing trip record schema and
lifecycle remain unchanged. This does not claim per-field trip-log freshness.
Additional controls cover primary timestamp wrap, target reset, emulator
injection, and real-time viewer callbacks marking each stale temperature
unavailable while current continues to update.

All nine cases are correctness assertions. The first five controls pass on
the baseline; four defect regressions fail there. Passing host tests does not
prove these packet conditions caused the video or validate physical hardware.
