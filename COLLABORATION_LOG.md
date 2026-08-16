# Collaboration log

Shared workspace: `C:\esp32p4-android-auto`

This is the handoff log for the user, Claude, and Codex. Add new entries at the
top of the **Entries** section. Keep entries factual; distinguish work already
present from work performed in the current session.

## Entry format

```text
### YYYY-MM-DD HH:MM +TZ - Actor - Short task
- Scope: purpose of the work
- Files: paths changed or inspected
- Checks: commands/tests run and their results
- Status: complete | in progress | blocked | observation
- Handoff: remaining work, risks, ownership, or "none"
```

## Shared rules

- Inspect `git status --short --branch` and relevant diffs before editing.
- Preserve all pre-existing and concurrently created changes.
- Use paths relative to the repository root in code and documentation.
- Refresh remote state only when needed and safe. Never pull, rebase, reset,
  clean, or stash across uncommitted shared work without coordination.
- Log material edits and verification before handing work to another actor.

## Entries

### 2026-08-17 00:02 +07:00 - Claude - Serial telemetry dump for BMS bring-up
- Scope: the backend logged connection lifecycle only - no decoded values ever
  reached the console. That left no way to check the parser against a real pack.
- Files: `main/ble_bms_client.c` (adds `log_snapshot()`).
- Behaviour:
  - **First cell frame of every session is dumped in full**: pack V/A, SoC,
    SoH, cycles, capacities, balance current, heater, both temperatures, MOS,
    timers, `valid_mask` in hex, and then **every cell with its wire
    resistance**. Reset on disconnect, so each reconnect re-dumps.
  - After that, a one-line summary every 5 s. The poll runs at 1 Hz; logging
    every frame would flood the console and the PSRAM log ring.
- Why cell-by-cell: the one failure that matters here - decoding with the wrong
  24S/32S layout - shifts every field by 32 bytes and still produces believable
  voltages, currents and temperatures. It cannot be spotted on the UI. Reading
  the cells off the console and comparing them against the JK phone app is the
  only check that catches it, which is also why the full dump fires once per
  session rather than continuously: it is a verification tool, not telemetry.
- Checks: `idf.py -B build_jc4880 ... build` exits 0; image 0x4374A0 =
  4,421,280 bytes, **16% free**. Host test unaffected, still 8 groups passing.
- Status: complete
- Handoff: this is the instrument for the first hardware session. Watch for
  `JK hw=... sw=... layout=24S|32S` first - if that line says UNSUPPORTED the
  hw-version mapping needs widening; if the cells are all shifted, the layout
  choice is wrong; if nothing arrives at all after `subscribed`, try
  `JK_CMD_TELEMETRY` (0x95) in place of `JK_CMD_CELL_INFO` (0x96).

### 2026-08-16 23:51 +07:00 - Claude - BE remainder: sleep timer, RSSI, NVS peer
- Scope: finish the backend items still open in my 23:39 handoff.
- **The adapter was already done by Codex** - `bms_ui_backend_get_snapshot()`
  and `bms_ui_backend_set_active()` are implemented in
  `Super_VESC_Display/custom/bms_view.c` (real-device and simulator variants).
  My earlier handoff claiming it was missing and mine was out of date. I did
  not touch it.
- **Sleep timer: Codex's report was right, the cause was different.** There are
  two distinct countdowns, and I had conflated them:
  - emergency countdown `186 + shift`, u16, **32S only** (already shipped);
  - **smart-sleep countdown `238 + shift`, u32, BOTH layouts** - this is the
    one that must hold 86400 s.
  `sleep_timer_s` is now `uint32_t` and decoded, behind its own
  `BMS_V_SLEEP_TIMER` bit. It needed a separate bit rather than sharing
  `BMS_V_TIMERS`: sleep exists on both layouts while emergency does not, so a
  single mask could not describe both without lying on 24S.
- RSSI: sampled from the advertisement while scanning (matched against the
  bound address, so there is a value before the link is up) and refreshed with
  `ble_gap_conn_rssi()` on each poll. The transport stamps it onto the snapshot
  just before publish - the parser has no knowledge of the radio.
- NVS: the bound peer now persists across reboots. Deliberately **not** in
  `dev_settings`: that is an upstream file the FE also edits, while the peer
  address is backend state. It lives in its own namespace (`bms_ble`/`peer`)
  inside `ble_bms_client.c`, which costs one handle and adds no merge conflict.
  `ble_bms_client_init()` restores it and leaves the link CONNECTING;
  `arm_connect()` stays a no-op until the stack syncs, so the ordering is safe.
- Checks: host test **PASSED, 0 failures**, 8 groups, `-Wall -Wextra` clean.
  `idf.py -B build_jc4880 ... build` exits 0; image 0x436F00 = 4,419,328 bytes,
  **16% free**.
- Status: complete
- Handoff: the backend has no remaining known gaps against the current
  contract. What is left is not code:
  1. **Nothing in the BMS path has ever run against hardware.** Every claim
     above is compile-time or host-test only. The first real pack will settle
     the `0x95` vs `0x96` question, the hw-version-to-layout mapping, and
     whether the 24S/32S offsets hold for the user's specific JK model.
  2. `charger_present` and `detail_log_count` remain unimplemented by design -
     see the 23:39 entry.
  3. `main.c` still does not reference the BMS at all; the client is brought up
     from `ble_host.c` only. That is sufficient today but worth a look when
     someone wires the settings entry point.

### 2026-08-16 23:39 +07:00 - Claude - BE fields for the expanded BMS UI
- Scope: implement the telemetry Codex requested at 23:32. Offsets were
  re-researched per field and per layout rather than extrapolated, per the
  "never publish unverified zeros" instruction.
- Files: `components/bms/include/bms/bms_model.h` (new fields + `BMS_V_*`
  bits), `components/bms/bms_jk.c` (settings-frame decoder, extra cell-frame
  fields), `components/bms/include/bms/bms_jk.h`, `main/ble_bms_client.c`
  (settings fetch in the session sequence), `tools/test/test_bms_jk.c`.
- **Two earlier assumptions were wrong and are now corrected:**
  1. `soh_permille` IS available - `158 + shift`, u8, both layouts. The 23:04
     entry said JK does not report SoH on this frame and left it permanently
     invalid. It is now decoded, which is what the FE's SOH readout needs.
  2. Balance-lead resistance is **not** in the cell frame at all. It lives in
     the settings frame (type 0x01) at byte **158 for 24S and 142 for 32S** -
     absolute offsets that do NOT follow `JK_32S_SHIFT`, unlike every other
     field. The session therefore fetches settings once after the layout is
     known, and the driver carries the resistances forward into every
     published snapshot so the UI still reads a single struct.
- Fields added (all valid-masked): `wire_res_mohm[32]` +
  `wire_res_valid_mask`, `balance_current_ma` (`138+shift`),
  `cycle_capacity_mah` (`154+shift`), `soh_permille` (`158+shift`),
  `heater_on` (`183+shift`) and `heater_current_ma` (`204+shift`),
  `emergency_timer_s` (`186+shift`).
- **`emergency_timer_s` is 32S-only.** On 24S those bytes mean something else,
  so `BMS_V_TIMERS` stays clear there. A test asserts the 24S case does not
  publish it.
- **Two requested fields deliberately NOT implemented** - please mark them
  unavailable in the FE rather than waiting for them:
  - `charger_present`: not carried in the 0x02 frame. The only lead is a log
    byte at `213+offset` that would have to be interpreted. Unverifiable.
  - `detail_log_count`: lives in the logbook frame (type 0x05, offset 6), not
    0x02. Would need a fourth frame type requested and decoded.
- Checks: host test **PASSED, 0 failures**, `-Wall -Wextra` clean, now 8
  groups including `[timers are 32S-only]` and `[wire resistance from settings
  frame]` (which also asserts resistance stays invalid until a settings frame
  has actually been seen). `idf.py -B build_jc4880 ... build` exits 0; image
  0x436C70 = 4,418,672 bytes, **16% free**.
- Also verified Codex's FE by running it: rebuilt the simulator and captured
  the window with `--bms-preview`. The BMS tab renders correctly (SoC arc,
  cell grid with min/max highlighting, delta, CHG/DSG/BAL, Live badge). Worth
  noting the first capture showed the VESC dashboard instead - "process stays
  alive" does not distinguish a working view from a wrong one.
- Status: complete
- Handoff: still mine and still missing - the `bms_ui_backend_get_snapshot()` /
  `bms_ui_backend_set_active()` adapter, RSSI population (the model has
  `rssi_dbm`, the transport never fills it), and NVS persistence of the bound
  address once the FE decides where that setting lives. Nothing is wired into
  `main.c` yet. No hardware verification anywhere in the BMS path.

### 2026-08-16 23:32 +07:00 - Codex - Parallel BE field handoff for expanded BMS UI
- Scope: record the extra telemetry visible in the user's JK reference screens
  while Codex implements the scrollable FE independently.
- Files: updated `docs/BMS_FE_BE_CONTRACT.md`; FE changes are in
  `Super_VESC_Display/custom/bms_view.c` and
  `components/vesc_ui/include/bms_ui_contract.h`.
- BE handoff to Claude: add optional valid-masked fields for per-cell balance
  wire resistance, balance current, cycle capacity, heater/current,
  charger-present state, emergency/sleep timers, and detail-log count. Verify
  offsets separately for JK02 24S and 32S; never publish unverified zeros.
- FE-derived values: cell average, voltage delta, used capacity, pack power,
  and charge/discharge flow require no new BE fields.
- FE result: connection/pair controls remain fixed; the 332 px telemetry body
  scrolls vertically through overview, cell/wire grid, pack details, BMS
  status, alarms, and an explicit list of unavailable JK fields. Wire values
  now have a per-cell validity mask so a real `0.000 ohm` is not confused with
  missing data. Display ABI is version 3.
- Parallel integration: Claude added model/parser fields for wire resistance,
  balance current, cycle capacity, heater/current, SoH, and timers. FE now maps
  the verified fields behind validity bits, including the 32S emergency timer.
  Sleep remains intentionally unavailable: `sleep_timer_s` is currently
  `uint16_t` (cannot represent the reference's 86400 s) and is not decoded.
- Checks: simulator rebuilds and `--bms-preview` remains running until the
  intentional test timeout. Full `build_jc4880` links and generates image
  `0x436D40` bytes with **16% free**. Extended JK host tests now pass with
  **0 failures**, including SoH, 32S-only timer, and settings-frame wire data.
- Status: FE complete; BE sleep-timer/hardware validation can continue without
  blocking the scroll/layout work.

### 2026-08-16 23:12 +07:00 - Codex - BMS BLE frontend and backend adapter
- Scope: implement the FE half of the BLE BMS feature while preserving
  Claude's ownership of transport, JK parsing, and the canonical model.
- Files added: `Super_VESC_Display/custom/bms_view.[ch]`,
  `components/vesc_ui/include/bms_ui_contract.h`, and
  `docs/BMS_FE_BE_CONTRACT.md`.
- Files modified: `Super_VESC_Display/custom/realtime_viewer.c` adds VESC/BMS
  tabs and lifecycle control; `Super_VESC_Display/lvgl-simulator/main.c` adds
  `--bms-preview`; `Super_VESC_Display/custom/custom.mk` and
  `components/vesc_ui/CMakeLists.txt` add the required include/dependencies.
- UI: read-only 800x480 dashboard with pack/SOC/current/power, cell min/max and
  delta, 32-cell grid, cell/wire toggle, temperature/MOS, health/cycles,
  CHG/DSG/balance state, alarms, stale/link state, and Pair/Forget flow.
- Integration: FE maps Claude's `bms_model_get()`, `bms_model_age_ms()`, and
  `bms_model_link_state()` snapshot into a display-only ABI. Pairing calls only
  backend discovery/bind APIs; FE does not perform GAP/GATT or frame parsing.
- Checks: LVGL simulator builds and `simulator.exe --bms-preview` remains
  running without a startup crash; ESP-IDF FE objects compile; full
  `ninja -C build __idf_vesc_ui` links `libvesc_ui.a`; JK host parser test
  passes with **0 failures**. Full `build_jc4880` links and generates
  `esp32p4_android_auto.bin` at `0x435B20` bytes with **16% free**. Existing
  unrelated warnings remain in `custom.c`/`lisp_panel.c`.
- Status: FE implementation complete; hardware integration not yet validated.
- Handoff to Claude: persist/reload the selected BLE address; expose
  balance-wire resistance if the JK protocol/model supports it; arbitrate the
  global NimBLE scanner/initiator with cadence and other clients, and confirm
  the configured connection-slot budget before hardware testing. The present
  backend is JK-only and has not been verified against the user's physical BMS.

### 2026-08-16 23:12 +07:00 - Claude - Fix wrong JK command header (corrects 23:04)
- Scope: the user asked me to check the community's connect flow. Doing so found
  a defect in the backend I had just written and logged below.
- Defect: **command frames were built with the response preamble.** JK uses
  `0x55 0xAA 0xEB 0x90` for frames it sends and the byte-swapped
  `0xAA 0x55 0x90 0xEB` for frames it accepts. `jk_build_cmd` used the former
  for both. The BMS does not reject a mis-headered command, it ignores it, so
  the link would have looked connected and subscribed while never answering -
  the failure mode most likely to be misdiagnosed as an RF or MTU problem.
  Corroborated by two independent sources: the taraskinua protocol write-up
  states commands are "the inverted sequence from incoming frames", and a
  captured device-info frame in the wild reads `AA 55 90 EB 97 00 ...`.
- Files: `components/bms/bms_jk.c` (separate `JK_CMD_HDR`),
  `components/bms/include/bms/bms_jk.h` (both layouts documented),
  `tools/test/test_bms_jk.c` (asserts the command header AND asserts it is not
  the response preamble), `main/ble_bms_client.c` (see below).
- Also aligned the connect flow with the community sequence: cell-info polling
  now waits until the device-info reply has identified the byte layout, rather
  than firing ~200 ms after subscribe. This is the same "wait after the first
  command" step the community documents, and it was pointless work regardless
  because the parser discards cell frames while the layout is unknown.
- Checks: host test PASSED, 0 failures, `-Wall -Wextra` clean.
  `idf.py -B build_jc4880 ... build` exits 0; image 0x435B20 = 4,414,240 bytes,
  16% free.
- Status: complete
- Handoff: one protocol ambiguity is **unresolved and cannot be settled from
  documentation**. syssi/esphome-jk-bms calls `0x96` COMMAND_CELL_INFO; the
  taraskinua write-up maps `0x95` to telemetry (response type 0x02) and `0x96`
  to settings (type 0x01). Decoding is not at risk either way - the parser
  dispatches on the response's own type byte, not on the request - but if a
  real pack never yields cell data, switch `JK_CMD_CELL_INFO` to the
  already-defined `JK_CMD_TELEMETRY` (0x95). Noted in the header too.

### 2026-08-16 23:04 +07:00 - Claude - BMS BLE backend: model, JK driver, transport
- Scope: the acquisition half of the BMS feature (user split BE to Claude, FE to
  Codex). Reads a JK BMS over BLE and publishes a normalized snapshot. No UI.
- Files added:
  - `components/bms/include/bms/bms_model.h`, `bms_model.c` - canonical backend
    model, atomic publish, staleness folding, diagnostic counters.
  - `components/bms/include/bms/bms_jk.h`, `bms_jk.c` - JK BLE reassembly and
    decode. Deliberately free of ESP-IDF and NimBLE so it is host-testable.
  - `components/bms/CMakeLists.txt`
  - `main/ble_bms_client.h`, `ble_bms_client.c` - NimBLE central, modelled on
    `ble_cadence_client.c`; own GAP callback, StreamBuffer + worker task.
  - `tools/test/test_bms_jk.c` - host test.
- Files modified (one inserted line each, to keep the v1.3.7 merge cheap):
  `main/ble_host.c` (include, `_on_ble_sync`, `_client_init`),
  `main/CMakeLists.txt` (SRCS entry + `bms` in REQUIRES).
- Checks:
  - Host test: `gcc -std=c11 -Wall -Wextra -I components/bms/include
    tools/test/test_bms_jk.c components/bms/bms_jk.c` - **PASSED, 0 failures,
    no warnings**. Covers both layouts, 20-byte fragmentation, corrupt
    checksum, unknown-layout refusal, resync, command framing.
  - Firmware: `idf.py -B build_jc4880 ... build` exits 0. Image
    0x4350F0 = 4,411,632 bytes, **16% free** in the 5 MB slot (was 16% before,
    so the feature costs well under 1%).
  - The first test run **found a real defect**: the parser only looked for the
    preamble while idle, so one lost fragment desynchronised it permanently.
    Fixed to resync on the preamble at any offset; regression test added.
- Protocol decisions worth knowing:
  - **Current sign is inverted in the JK driver.** JK sends negative-for-
    discharge; the model boundary fixes positive-for-discharge to match the
    VESC telemetry already on the dashboard. Asserted in the test.
  - **The decoder refuses to run until the layout is known.** JK02_24S and
    JK02_32S differ by a fixed 32-byte shift and a mis-guess still yields
    plausible voltages, so `jk_feed` returns IGNORED for cell frames until the
    `0x03` device-info frame identifies the hardware generation. That is why
    the handshake sends `0x97` before `0x96`.
  - Offsets follow syssi/esphome-jk-bms; sources are in
    `docs/BMS_BLE_ARCHITECTURE_RESEARCH.md`.
- Status: in progress
- Handoff to Codex:
  1. **No contract conflict.** `components/vesc_ui/include/bms_ui_contract.h`
     anticipates exactly this split ("backend may keep a richer internal model
     and map it in a small adapter"). `bms_model.h` is that internal model.
     The adapter implementing `bms_ui_backend_get_snapshot()` and
     `bms_ui_backend_set_active()` is **mine and still missing** - the FE will
     not link until I add it. Next thing I do unless told otherwise.
  2. Two field-width differences to reconcile in the adapter, not in the FE:
     `BMS_UI_MAX_TEMPS` is 8, the backend model carries 5 (JK exposes MOS plus
     up to 5 probes). Backend link states are a subset of `bms_ui_link_state_t`
     - `WAIT_SLOT`, `BACKOFF` and `ERROR` are never produced yet.
  3. Nothing is wired into `main.c` yet and no peer address is persisted:
     `ble_bms_bind()` / `ble_bms_get_bound()` are exposed for the settings
     layer, which is FE-side and owns NVS. The backend deliberately does not
     touch `dev_settings`.
  4. Untested against hardware - no JK BMS present. Everything above is
     compile- and host-test verified only.

### 2026-08-16 22:43 +07:00 - Codex - Create BMS UI reference folder
- Scope: establish a shared location for the two user-provided BMS UI samples
  and record the reusable visual requirements.
- Files: added `docs/ui-references/bms/README.md` and
  `docs/ui-references/bms/source/README.md`.
- Findings: the chat images are visible for analysis but are not exposed as
  binary files in the workspace. Reserved canonical names
  `source/01-pack-overview.png` and
  `source/02-cell-resistance-detail.png`; documented overview, cell-grid,
  balance-wire, state-color, stale-data, and 800x480 adaptation requirements.
- Checks: verified the new directory tree and repository status.
- Status: complete for directory/manifest; original PNG binaries remain
  pending until they are made available in the workspace.
- Handoff: preserve originals in `source/`; put annotated or resized variants
  in a separate sibling directory.

### 2026-08-16 22:27 +07:00 - Codex - Trace VESC Tool BLE connection
- Scope: explain the current end-to-end VESC Tool BLE connection and the
  BLE-to-CAN response path.
- Files: inspected `main/ble_host.c`, `main/ble_nus.[ch]`, `main/main.c`, and
  `components/vesc_can/comm_can.c`.
- Findings: the head unit advertises `SuperVESCDisplay` with Nordic UART
  Service UUID; VESC Tool writes framed VESC packets to NUS RX and subscribes
  to NUS TX. Complete payloads are forwarded to the configured Target VESC ID
  over CAN; reassembled CAN responses are reframed, queued, split by ATT MTU,
  and returned as TX notifications. NUS has one active owner even though two
  peripheral peers may be connected.
- Checks: traced advertising, GAP connect/subscribe, GATT access callback,
  packet parser, CAN dispatch, response ring buffer, and notification chunking.
- Status: complete, explanation only; runtime code was not changed.
- Handoff: none.

### 2026-08-16 22:21 +07:00 - Codex + Claude research - BMS BLE architecture
- Scope: research how the existing P4/C6 BLE stack should acquire a smart BMS
  and compare it with community implementations for JK, JBD, Daly, BatMon,
  aiobmsble, and Home Assistant BMS_BLE.
- Files: inspected `docs/ARCHITECTURE.md`, `sdkconfig.defaults`,
  `main/ble_host.c`, `main/ble_cadence_client.[ch]`, `main/ble_nus.c`, and the
  current UI lifecycle; added `docs/BMS_BLE_ARCHITECTURE_RESEARCH.md`.
- Findings: NimBLE host/GATT client belongs on P4 and C6 is only the hosted
  controller/radio. Do not route BMS through the external BT agent. A shared
  `ble_central_manager` is required because cadence currently owns an
  indefinite initiator and scan is global. The existing three BLE connection
  slots are already allocated to two peripheral peers plus cadence, so a BMS
  requires a verified fourth slot or an explicit one-peripheral tradeoff.
  Community implementations converge on vendor-specific session/parser
  drivers feeding a normalized, timestamped snapshot; read-only is required
  for the MVP.
- Checks: cross-checked the Bluetooth SIG Battery Service, Espressif BLE
  multi-connection documentation, JK protocol design, JBD/Daly ESPHome
  components, BatMon, aiobmsble, and BMS_BLE-HA; verified local path/line
  ownership and current `CONFIG_BT_NIMBLE_MAX_CONNECTIONS=3`.
- Status: complete, research/documentation only; runtime code was not changed.
- Handoff: identify the physical BMS model/firmware and capture its
  advertisement, GATT table, and sample notification before selecting the
  first driver. Implement/refactor the central manager before adding a second
  independent GATT client.

### 2026-08-16 22:18 +07:00 - Codex - Analyze BMS telemetry tab
- Scope: identify architecture, protocol, UI, lifecycle, fault cases, and test
  coverage needed for a new BMS telemetry tab.
- Files: inspected the CAN dispatcher/poller, realtime viewer, settings UI,
  simulator build wiring, and official VESC BMS protocol; added
  `docs/BMS_TAB_TECHNICAL_ANALYSIS.md`.
- Findings: the repo only identifies `HW_TYPE_VESC_BMS` and has no BMS parser.
  Recommended MVP is a read-only BMS tab in Realtime, direct
  `COMM_BMS_GET_VALUES` polling at about 1 Hz only while visible, serialized on
  the existing CAN poll task. BMS model/protocol and CAN ID must be confirmed.
- Checks: official VESC Tool parser/struct and VESC BMS firmware documentation
  were cross-checked; local source paths and build globs were verified.
- Status: complete, analysis/documentation only; runtime code was not changed.

### 2026-08-16 21:52 +07:00 - Codex - Record 40 V battery voltage
- Input: user confirmed the current battery voltage is 40 V.
- Files: updated `docs/VEHICLE_CALIBRATION_MAIN_LISP.md` with the distinction
  between 70 A motor phase current and battery current/power.
- Findings: power cannot be derived as 40 V times 70 A without the live Battery
  Current Max. Battery/BMS discharge and regen limits, cell count, and chemistry
  are still required for a safe final setting.
- Status: complete, documentation only; `lisp/main.lisp` was not changed.

### 2026-08-16 21:45 +07:00 - Claude - LVGL simulator builds and runs on Windows
- Scope: make `Super_VESC_Display/lvgl-simulator` build on this machine. It now
  builds clean and the window opens.
- Files: `Super_VESC_Display/lvgl-simulator/Makefile`, Windows branch only.
  Comments in place explain how to undo each removal.
  - Commented out the two `-include` lines pulling in `gg_external_data.mk`
    and `freemaster/freemaster.mk`.
  - Dropped `-ldecoder -ljansson -lcurl` from `LDFLAGS`.
- Why: `LV_USE_FREEMASTER` is already `0` in both `lv_conf.h` files and every
  FreeMASTER use in `main.c` sits behind that guard (lines 26, 87, 97, 122), but
  those two makefiles add the FreeMASTER sources unconditionally and the sources
  include `<curl/curl.h>` and `<jansson.h>` with no guard. MSYS2 has retired
  most of its 32-bit environment - the `mingw32` repo is down to 248 packages
  and carries no `mingw-w64-i686-jansson` at all - so those files cannot be
  built here and are dead weight regardless. Separately,
  `lib/native/libdecoder.a` is a **macOS Mach-O** archive that cannot link on
  Windows, and no simulator source includes `decoder.h`.
- Environment installed on this machine (not in the repo): MSYS2 at
  `C:\msys64` via winget, then `mingw-w64-i686-gcc`, `-make`, `-pkg-config`.
  **32-bit is mandatory** - the PE headers of the bundled `SDL2.dll` and
  `libopenh264.dll` are both i386, so an x86_64 toolchain will not link.
- Checks: `mingw32-make -j8` exits 0 and produces `build/bin/simulator.exe`
  (1,732,038 bytes). Launched it and the process stayed alive past startup, so
  SDL initialised and the UI came up.
- Status: complete
- Handoff: three traps for the next actor.
  1. **`mingw32-make` dirties 7 tracked files on every run.** `regen_fonts`
     rewrites `Super_VESC_Display/generated/guider_fonts/lv_font_Antonio_Regular_*.c`,
     changing only the `Opts:` comment that records the build machine's absolute
     path; glyph data is byte-identical. I reverted them twice - expect to do
     the same. The committed files carry the author's macOS path
     (`/Users/alexey/work/...`).
  2. `cp_lib` fails silently because it calls Unix `cp`, which cmd.exe lacks.
     Copy DLLs into `build/bin/` by hand: `SDL2.dll` and `libopenh264.dll` from
     `SDL2/lib/`, `libgcc_s_dw2-1.dll` and `pthreadGC-3.dll` from
     `multi_thread/`, plus `libstdc++-6.dll` and `libwinpthread-1.dll` from
     `C:\msys64\mingw32\bin`.
  3. **This adds a third conflict surface for the pending v1.3.7 merge**,
     alongside `CLAUDE.md` and `lisp/main.lisp`. Upstream `f7b1ddf` reworks
     host-portable build invocation and may already cover part of this, so
     re-check this Makefile against upstream after merging rather than carrying
     the patch forward blindly.

### 2026-08-16 21:42 +07:00 - Codex - Record live 70 A motor-current limit
- Input: user confirmed the ESC's current Motor Current Max is 70 A.
- Files: updated `docs/VEHICLE_CALIBRATION_MAIN_LISP.md` with effective current
  values for every proposed profile strategy and a staged validation sequence.
- Findings: current profiles yield nominal 21/42/70 A; balanced profiles yield
  35/52.5/70 A; pure speed-only profiles would permit 70/70/70 A below the ERPM
  taper. Motor phase current must not be confused with battery current.
- Status: complete, documentation only; `lisp/main.lisp` was not changed.

### 2026-08-16 21:28 +07:00 - Codex - Recommend speed-profile settings
- Scope: analyze how to configure speed-only profiles and when full current in
  every profile is technically appropriate.
- Files: inspected `lisp/main.lisp`, PAS defaults, and generated VESC 7.00
  configuration metadata; expanded `docs/VEHICLE_CALIBRATION_MAIN_LISP.md` with
  commissioning, balanced, and speed-only setting matrices.
- Findings: retain `l_erpm_start = 0.8` initially; do not use all scales at 1.0
  until base motor current is safe from standstill. The shared throttle/brake
  ramp and unrestricted profile switching are safety constraints in current code.
- Status: complete, analysis/documentation only; `lisp/main.lisp` was not changed.

### 2026-08-16 21:07 +07:00 - Codex - Clarify speed-only profile option
- Scope: determine whether profiles can retain full low-speed motor current
  while using different ESC speed limits.
- Files: inspected `lisp/main.lisp`; expanded
  `docs/VEHICLE_CALIBRATION_MAIN_LISP.md` with the full-current option and its
  safety tradeoff.
- Checks: confirmed that setting every `l-current-max-scale` to 1.0 removes the
  profile-wide derating, while the ESC ERPM limiter still must reduce propulsion
  current near the speed ceiling.
- Status: complete, analysis only; `lisp/main.lisp` was not changed.
- Handoff: if implementation is requested, change only the two 0.3/0.6 scales
  to 1.0 and test low-speed launch torque before riding.

### 2026-08-16 21:02 +07:00 - Codex - Trace ESC speed-limit behavior
- Scope: explain how Lisp profile `max-speed` becomes an ESC ERPM/current limit,
  and distinguish that mechanism from speed PID or active braking.
- Files: inspected `lisp/main.lisp`, local generated VESC config tables, and
  official VESC firmware sources; expanded
  `docs/VEHICLE_CALIBRATION_MAIN_LISP.md`.
- Checks: verified the VESC m/s-to-ERPM formula and calculated the current bench
  targets as approximately 4642/9284/18568 ERPM for 5/10/20 km/h.
- Status: complete
- Handoff: read the live ESC `l_erpm_start` value before assuming the default
  0.8 current-reduction window; multi-ESC configurations need explicit sync.

### 2026-08-16 20:50 +07:00 - Codex - Analyze vehicle calibration for Lisp
- Scope: map the current `lisp/main.lisp` motor arbiter to vehicle-specific
  VESC, throttle/brake, cruise, profile, and PAS calibration requirements.
- Files: inspected `lisp/main.lisp`, `main/pas.h`, `main/pas.c`, generated VESC
  config tables, and the Lisp reference; added
  `docs/VEHICLE_CALIBRATION_MAIN_LISP.md`.
- Checks: verified the current profile diff and cross-checked every config key
  read or written by the script; documentation-only change, so no runtime test.
- Status: complete
- Handoff: obtain the actual motor, ESC, battery/BMS, gearing, wheel, ADC, and
  load data before proposing numeric calibration values for this vehicle.

### 2026-08-16 20:48 +07:00 - Claude - Upstream v1.3.7 available; merge blocked
- Scope: check whether the tree is still current against the publisher. It is
  not. `git fetch upstream` (read-only, does not touch the working tree) brought
  in tag `v1.3.7` and 7 commits ahead of our `develop`.
- Files: none changed. `.git/config` already carries the `upstream` remote
  (`https://github.com/payalneg/esp32p4-android-auto`), added earlier today.
- Checks: `git rev-list --count` reports 7 commits ahead of us and 4 of ours
  ahead of upstream (3 docs commits plus the 1.3.6 merge). Notable upstream work:
  - `f7b1ddf build: host-portable python/gzip invocation + Windows build scripts`
    adds `scripts/compress_gz.py` and `scripts/*.bat`, which **fixes two of the
    three environment defects logged in the entry below** - the missing `gzip`
    and the need to avoid Git Bash.
  - `85e280d` render watchdog, gated log erases, dual-head temps.
  - `666c565` stops showing the VESC's raw counters as the trip.
  - `452e4d5` brightness-gesture setting, applied per theme.
- Status: blocked, deliberately not merged.
- Handoff: **upstream 1.3.7 modifies `CLAUDE.md`, which is currently dirty with
  Codex's uncommitted shared-protocol block.** A merge would refuse or endanger
  that edit, and `AGENTS.md` forbids pull/rebase/reset across a dirty shared
  tree without coordination. Suggested order once the owners agree: commit
  `CLAUDE.md` + `AGENTS.md` + `COLLABORATION_LOG.md` (Codex's work, Codex to
  confirm it is final), commit or park `lisp/main.lisp`, then merge
  `upstream/main`. No other dirty file is touched by the upstream diff.

### 2026-08-16 20:47 +07:00 - Claude - Claim the `lisp/main.lisp` profile edit
- Scope: resolve the unconfirmed ownership recorded below. The dirty change in
  `lisp/main.lisp` is mine, made at the user's request to retune the three speed
  profiles for a bench demo (40 mm wheel, 14-pole motor, 1:1 gearing).
- Files: `lisp/main.lisp` only.
  - `apply-profile` (lines 57-73): 25/40/60 km/h at 50/67/100% current became
    5/10/20 km/h at 30/60/100%. All three `print` strings updated to match.
  - `panel-send-ui` (lines 248-250): the profile radio labels became
    `Slow 5 km/h`, `Medium 10 km/h`, `Fast 20 km/h`, as `lisp/README.md`
    requires ("keep the labels in step with apply-profile").
- Checks: paren, brace, and quote balance verified as 0/0/0 with a throwaway
  script; `@const-start`/`@const-end` still one real directive each (lines 48
  and 543 - the other three occurrences are prose in comments). `UI_DESC`
  recomputed by hand at 101 bytes against the 128-byte `pbuf`, down from 102
  because "Slow 5" is one character shorter than "Slow 25". Profile count is
  unchanged at 3, so `num-profiles`, the `beep-freq` table, and panel ids 10-12
  needed no edit. `flutter test test/lisp_lint_test.dart` was NOT run - Flutter
  is not installed on this machine - so the checks above are the manual
  equivalent of what that linter enforces.
- Status: complete, uncommitted by request. The user is iterating on the values.
- Handoff: `l-current-max-scale` is a fraction of `Motor Current Max` in VESC
  Tool, so the effective bench current depends on a value that lives on the
  ESC, not in this file. If these numbers are edited again, the panel labels
  must move with them.

### 2026-08-16 20:47 +07:00 - Claude - Build-environment findings for this machine
- Scope: record two environment defects that silently break a `jc4880` build
  here, so the next actor does not rediscover them. Neither is a code defect.
- Files: none changed. `build_jc4880/` was produced (git-ignored).
- Checks: full `jc4880` build run to completion, exit 0,
  4,395,792 bytes, 16% free in the 5 MB OTA slot.
  1. `gzip` is absent from the Windows PATH, so the CMake step that compresses
     `main/web/lisp_editor.html` fails. `C:\Program Files\Git\usr\bin` supplies
     it; appending that directory to PATH is enough.
  2. The eim PowerShell profile exports `ESP_IDF_VERSION=5.5.3`, but
     `managed_components/espressif__esp_wifi_remote/Kconfig` does
     `orsource "./idf_v$ESP_IDF_VERSION/Kconfig.slave_select.in"` and the real
     directory is `idf_v5.5`. `orsource` is silent when the file is missing, so
     every `CONFIG_WIFI_RMT_*` symbol disappears and the build dies much later
     in `wifi_manager.c` with an unrelated-looking error. Forcing
     `ESP_IDF_VERSION=5.5` before `idf.py` restores them.
  3. `scripts/build_board.sh` cannot run from Git Bash here: `idf.py` prints
     "MSys/Mingw is no longer supported" and exits 0 without building. Invoke
     `idf.py` from PowerShell with the same `-B`/`-D` arguments the script uses.
- Status: observation
- Handoff: none of this is written down in the repo. If it should be, `CLAUDE.md`
  under "Lệnh thường dùng" is the natural home, or a `scripts/build_board.ps1`
  wrapper would remove the need to document it.

### 2026-08-16 20:45 +07:00 - Codex - Establish shared-workspace protocol
- Scope: create durable coordination rules and a Markdown handoff log for the
  shared Claude/Codex working tree.
- Files: added `AGENTS.md` and `COLLABORATION_LOG.md`; updated `CLAUDE.md` with
  the shared protocol pointer.
- Checks: inspected repository instructions, `git status --short --branch`,
  and the existing diff in `lisp/main.lisp`.
- Status: complete
- Handoff: keep adding new material work at the top of this section.

### 2026-08-16 20:45 +07:00 - Unknown prior actor - Existing profile changes
- Scope: record a dirty-tree change that existed before Codex began work.
- Files: `lisp/main.lisp` changes profile speed/current values and matching UI
  labels to 5 km/h at 30%, 10 km/h at 60%, and 20 km/h at 100%.
- Checks: diff inspected only; no test was run by Codex for this pre-existing
  change.
- Status: in progress
- Handoff: ownership is unconfirmed (user or Claude). Codex has not modified
  or reverted this file.
