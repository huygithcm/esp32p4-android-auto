# Lisp quick-action drawer regression cases

Run from the repository root using a host GCC installation:

```text
python scripts/test_lisp_panel_ui.py --cc <host-gcc>
python scripts/test_lisp_panel_ui.py --cc <host-gcc> --baseline-ref 7c77f2b
```

The first command tests working-tree production `lisp_panel.c`; the second
reads only that file from the release commit into a temporary directory,
without modifying the working tree. Both build actual managed LVGL 8.3.11,
the `LV_REALDEVICE` panel branch and the generated UI font. LVGL handles real
pointer events, hit testing, timers, layout and animation. Only the dashboard
lookup and CAN model/action boundary are stubbed. Builds use a unique system
temporary directory, with no simulator window or shared firmware output.

The input starts pressed at `(10, 240)`, moves to `(135, 240)`, and requests
opening through `lisp_panel_open_async()`. It stays pressed during the slide
animation before releasing. The dashboard root is clickable without
`PRESS_LOCK`, as in the generated dashboard. The descriptor contains Park
(button ID 6) and Modes 1/2/3 (toggle IDs 10/11/12), with Mode 1 initially on.

| Case | Steps and required result | Release baseline | Fixed |
|---|---|---|---|
| `opening_release_outside` | Open with held swipe; release at `(420, 240)` outside drawer. Drawer and polling remain open; no action emitted. | FAIL: closes | PASS |
| `opening_release_inside` | Same sequence, ending at `(240, 150)` inside drawer. Remains open with no accidental action. | FAIL: closes | PASS |
| `next_outside_tap` | Finish opening swipe, then make a new tap at `(600, 240)`. Only this new tap closes drawer; overlays disappear after animation. | FAIL: already closed | PASS |
| `mode_and_park_actions` | Tap Mode 2, Mode 3, Mode 1, then Park through pointer input. Assert each control ID/value. Incoming state refresh emits no action. | PASS | PASS |
| `missing_descriptor` | Open with transport reporting no descriptor; release, then wait 1 second. Drawer stays open in Loading state. | FAIL: closes | PASS |
| `late_descriptor` | Begin without descriptor; receive it after opening/release. All mode controls appear and drawer remains open. | FAIL: closes | PASS |
| `dashboard_only` | Attempt async open while a different screen is active. No drawer/polling starts. | PASS | PASS |
| `screen_change` | Open on dashboard, then load another screen. Drawer and polling close. | PASS | PASS |
| `reopen_during_close` | Close, request reopen 50 ms into closing animation, then reopen normally and close again. No old overlays survive and no current drawer is deleted by old animation callback. | FAIL: orphan overlay | PASS |

Baseline result: 3/9 passed; fixed result: 9/9 passed. A baseline command is
expected to return exit code 1. Individual cases can be selected with
`--case <name>`.

The fix follows managed LVGL's existing `lv_indev_wait_release()` mechanism
(`src/core/lv_indev.c`): held input is ignored and its eventual release clears
the active object without a click. The async opener enumerates pointer input
devices attached to the dashboard display because there is no current active
input device in the async callback. Reopening is ignored while the closing
animation still owns the drawer pointer.

These tests reproduce UI event behavior, not the physical GT911 reader, touch
task scheduling, CAN delivery, ESC execution or M-TEMP behavior. Mode action
queueing passing does not establish that the ESC accepted a mode change.
The user must verify the packaged P4/Lisp pair on hardware.
