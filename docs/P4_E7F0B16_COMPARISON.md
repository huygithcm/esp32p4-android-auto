# Current P4 source compared with e7f0b16

Comparison date: 2026-09-25. Baseline: e7f0b16 (2026-09-02).
Current means HEAD 7de2530 plus the existing uncommitted target-polling fix.
It does not mean the firmware currently flashed on the customer's device.

## Main behavior changes

| Area | e7f0b16 | Current working tree |
|---|---|---|
| Boot | Throttle enabled, subject to old safe-start | Starts in PARK, throttle disabled |
| Gear display | MODE 1/2/3/R from legacy DASH | Colored 40-pixel circle, 30-pixel font, 1/2/3/R/P/- from fresh safety state |
| No valid mode reply | Mode widget hidden if no DASH | '-' when safety state missing/stale/interlocked/faulted |
| Native ADC output | Rolling 1500 ms output suppression | Indefinite suppression; persisted ADC Control Type NONE required |
| TX mode button | Debounced press cycles mode | Short release exits P or cycles mode; >=1 s hold requests P; held-at-boot is rejected |
| Enter PARK | Not implemented | Standstill, released throttle and deliberate brake required |
| Exit PARK | Not implemented | Standstill, released throttle, healthy motor loop and released reverse button required |
| Reverse button | RX, active low | Same RX wiring; heartbeat monitored |
| Reverse arming | Forward-motion check | Absolute standstill for new arming; already-authorized reverse motion handled separately |
| Fault handling | No new monitor/supervisor heartbeats | ADC/native-control checks, TX/RX heartbeat expiry, motor-loop supervisor; faults latch |
| Brake in PARK | No PARK | Deliberate brake remains available in healthy P |
| Sounds / quick panel | Motor tones, beep/volume controls | No motor tones; Park plus three modes |
| Legacy helper unlock | Enables throttle | Cannot unlock P; returns PARK_REQUIRED |
| PAS | Source lock | Fresh zero from same source required after reset/interlock before assist |
| Unsupported reverse speed setting | Error could terminate panel handler before ACK | Trapped; reports unsupported result; boot can disable unsupported reverse |

Both versions already use absolute-ampere ride config format 2. RX reverse,
ampere modes and removal of cruise are NOT new relative to e7f0b16.

## Firmware/backend changes

- Adds safety request/reply 0x0B/0x8B and PARK command/ACK 0x0C/0x8C.
- Replaces live status polling 0x0A/0x89 with sequenced 0x0D/0x8D.
- Dashboard and Android Auto reverse indicator use safety state, fresh for
  less than 1000 ms; legacy DASH no longer authorizes the gear indicator.
- Adds a safety poll on a 200 ms interval alongside the existing status poll.
  Low-level CAN code is unchanged, but bus traffic is not identical.
- Correlates config/status replies with sent requests; rejects old replies,
  clears target-specific cached state/queued work on target changes, and
  separates send locking from state locking so RX can finish during a send.
- main/main.c now retargets the ride backend when Target VESC ID changes.

## Uncommitted correction, absent from the packaged September 19 firmware

The current local main/main.c snapshots RT/Lisp/IO activation before target
reinitialization and restores it afterward. New is_active getters support it.
This fixes polling stopping after changing target without resuming a poller
that was intentionally paused for a Lisp transfer. scripts/test_target_polling.py
is currently untracked. This patch has host coverage but has not been built
into new P4 firmware or flashed.

scripts/can_s3_fix/ is a pre-existing untracked staging package. Its proposed
CAN changes have NOT been applied to the actual P4 components/vesc_can sources.

## Verified unchanged

Git diff is empty for components/vesc_config, comm_can.c, packet_parser.c,
vesc_io_data.c, vesc_lisp_panel.c, main/ble_nus.c, main/ble_bms_client.c,
main/wifi_manager.c and version.txt. RT source changes only add an active-state
getter; SETUP request mask and temperature decoding are unchanged. The UI's
push_rt_locked temperature mapping is unchanged.

Both versions report P4 1.3.7 and contain native VESC config tables 6.05, 6.06,
7.00. Config format/signature restrictions have not changed in this interval.

## Customer-symptom implications

- New P4 with e7f0b16 Lisp: format2 config replies remain compatible, but no
  new safety replies means gear '-' and unavailable PARK. Legacy 0x89 does
  not refresh live status. This pairing alone does not explain all config
  read/write failures.
- Lisp older than format2 may instead cause config version mismatch; scripts
  older than the ride handlers can cause timeout.
- Current Lisp with old native ADC settings may deliberately lock propulsion.
  Correct configuration and restart are needed after a latched fault; updating
  Lisp alone is not a complete migration procedure.
- No source change maps throttle into motor temperature. Compare VESC Tool
  Motor Temperature and ADC readings with P4 before assigning that cause.
- A low-level CAN fault is not disproved by unchanged decoder code, especially
  since polling traffic changed. No device logs/raw CAN capture were available.

## Verification and limits

This follow-up used Git diffs and parallel read-only Lisp review. No firmware
source was changed, no new firmware build/flash and no motor actuation occurred.
Earlier checks in this conversation: host compatibility fixtures 15/15,
ride transport 53, safety parser 2532, gear mapping 81, Lisp host branches 13/13,
and target-polling tests with active/paused pollers passed. Those are host
checks, not real LispBM scheduling or physical CAN/hardware acceptance.

The packaged release remains based on 7c77f2b. e7f0b16 has a successful build
record, but the inspected records do not establish successful on-vehicle
reverse acceptance. Some older bring-up documentation describes pre-PARK
operation and is not a complete guide to the current script.

## Follow-up: confirmed working baseline, 2026-09-25

User confirms the working release is e7f0b16 (September 2), previously called
September 5. This is customer-reported acceptance, not a new hardware test.
Fresh Git audit finds one intervening firmware commit, 7c77f2b. HEAD adds
collaboration/docs/artifacts only. RT decoder/mask, CAN core, touch routing,
main/lv_conf.h and generated Settings entry are unchanged.

Indirect regression candidate: poll_safety adds a query every 200 ms with a
60 ms sync wait; status polling now uses a sequenced opcode. Old e7f0b16 Lisp
ignores both new queries, potentially costing about 120 ms per polling cycle,
subject to other CAN completion signals. This may slow telemetry; it does NOT
prove a temperature field swap or inaccessible Settings.

Dashboard get_safety takes a state mutex with portMAX_DELAY. Both poll_safety
and send_request release that mutex BEFORE waiting for CAN. Independent review
found no demonstrated deadlock or timeout-length state lock hold. The target
polling-stop defect existed in both versions; its unbuilt local correction
cannot be the cause of the released binary's new symptom.

Next discriminator: A/B old/new P4 on the same ESC/Lisp/configuration; capture
raw SETUP mask/temperature bytes and timeout cadence, comparing direct VESC
Tool Motor Temperature and ADC voltage. Negative values alone do not establish
whether the source is ESC data or malformed incoming data. No source fix,
build, flash or hardware test performed. Earlier unchanged-parser host results
were reused, not rerun. No single cause of both symptoms is established.
