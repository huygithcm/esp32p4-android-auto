# BMS over BLE — hardware bring-up test cases

Nothing in the BMS path has ever run against a real pack. Everything in it was
verified by compiling and by host tests against synthesised frames, which
proves the parser is self-consistent and proves nothing about the JK in front
of you.

This is the ordered list of what to check, what "pass" looks like, and what to
do when it does not. Work through it in order: each step depends on the one
above, and diagnosing step 4 while step 2 is broken wastes an evening.

Watch the console throughout:

```
scripts/capture.sh 120            # or idf.py -B build_jc4880 -p <PORT> monitor
```

Everything the BMS path prints is tagged `ble_bms`.

**Order of the eleven steps, at a glance.** Steps 1-4 must pass before any
later result means anything. Steps 5, 9 and 10 each cover a defect that was
found by reading code rather than by a test, and none of them can fail visibly
on the bench -- 5 shows correct numbers arriving half as often, 9 fails only
across a power cycle, 10 fails only when the pack is off at boot.

| | Step | Needs |
|---|---|---|
| 1 | Peer is discoverable | bench |
| 2 | Link up, GATT found, name right | bench |
| 3 | Layout detected | bench |
| 4 | Cell data arrives | bench |
| 5 | Update rate is 1 Hz, not 0.5 Hz | bench, one minute |
| 6 | Values match the JK app | bench + phone app |
| 7 | Absent fields show as absent | bench |
| 8 | Tab gate reaches the backend | bench |
| 9 | Pairing survives a power cycle, a mis-tap does not | bench, power cycle |
| 10 | Recovers when the pack is off at boot | bench, power cycle |
| 11 | Coexists with AA and the companion app | **a ride** |

---

## Preconditions

| | |
|---|---|
| Firmware | built from this branch, flashed to the board |
| JK BMS | powered, within a metre or two, **its phone app closed** |
| Model | note the exact model, hardware and software version from the JK app first |

The last two matter. JK generally allows **one BLE connection at a time**: if
the phone app holds it, the head unit cannot connect, and the symptom is a
connect that never completes rather than an error.

Record the model/hw/sw before you start. Several checks below compare against
it, and it is the first thing to report if something does not fit.

---

## 1. The peer is discoverable

**Do:** BMS tab → *Pair*.

**Pass:** the JK appears in the list, named something like `JK-B2A24S15P`, with
a plausible RSSI.

**Fail — nothing listed:**
- The scan filter matches on a name beginning `JK-` or `JK_`
  (`adv_looks_like_jk` in `main/ble_bms_client.c`). A unit advertising under a
  different name is invisible to it. Check the name in the JK app or any BLE
  scanner and widen the filter if it differs.
- Phone app still connected.

**Fail — the list says `Busy connecting - close and retry`:** not a fault. A
connect is already in flight and NimBLE runs one initiator at a time, so the
scan was refused. Close the modal, wait for the link to settle, try again. An
empty list used to be the only symptom, which was indistinguishable from no
packs in range.

---

## 2. The link comes up and GATT is found

**Do:** select the peer.

**Pass:**

```
I ble_bms: bound to xx:xx:xx:xx:xx:xx
I ble_bms: connected, conn=1
I ble_bms: MTU now 517
I ble_bms: subscribed; asking for device info
```

**Fail — `0xFFE0 not found` or `0xFFE1 not found`:** the unit does not expose
the service this driver expects. Dump its GATT table with a phone scanner
(nRF Connect) and compare. Link state will read `unsupported`.

**Fail — connects then immediately disconnects:** usually the phone app
reclaiming the link.

**Also check the name.** Whatever the tab shows must be the pack you just
selected. A JK states its name only in advertising, and bind stops the scan
first, so the name has to be carried in from the row you tapped --
`ble_bms_bind_named()`. If the FE still calls plain `ble_bms_bind()` the name
will be **empty** until a later sweep sees the pack again: that is expected and
harmless. Showing the *previous* pack's name over the new pack's numbers is
not, and is the bug this replaced.

**Note on MTU:** a small value here is not a failure. Reassembly handles any
fragment size; a large MTU only means fewer notifications per frame.

---

## 3. Layout detection — the first real decision point

**Pass:**

```
I ble_bms: JK hw=11.XW sw=11.26 layout=32S
```

Compare `hw` and `sw` against what the JK app reports. They must match.

**Fail — `unrecognised JK hw '...' — cannot pick layout`:** the mapping in
`decode_device_info()` accepts only known families and **requires the version
dot**: `11.` is 32S, `10.`/`9.`/`8.` are 24S. Anything else -- including `1123`
or `10XW`, which an earlier version would have accepted -- stays unknown and
the driver refuses to decode rather than guess. Report the exact string;
widening the mapping is a two-line change.

Refusing is deliberate. The catch-all this replaced treated every non-empty
string as 24S, so a garbled version field produced plausible, wrong numbers.

**Fail — the version strings are garbage:** the offsets for those fields
(22 and 30) are the least corroborated part of the driver. Everything else was
cross-checked against `research/_sources/esphome-jk-bms`; these were not.

**Do not skip this.** The 24S and 32S layouts differ by a fixed shift, and a
wrong choice still produces voltages, currents and temperatures that look
entirely reasonable. It cannot be caught by eye further down.

---

## 4. Cell data arrives

**Pass:** within a second or two of opening the tab,

```
I ble_bms: pack 40.010 V  +7.000 A  soc 77.0%  cells 16  3985..4012 mV ...
I ble_bms:   frames=1 crc_err=0 len_err=0 dropped=0 timeouts=0 reconnects=0
```

**Fail — nothing after `subscribed`, `timeouts` climbing:** this is the one
protocol question documentation could not settle. Two sources disagree on which
register yields the cell frame:

| Source | 0x95 | 0x96 |
|---|---|---|
| syssi/esphome-jk-bms | — | `COMMAND_CELL_INFO` |
| taraskinua | telemetry (type 0x02) | settings (type 0x01) |

Swap `JK_CMD_CELL_INFO` for the already-defined `JK_CMD_TELEMETRY` (0x95) in
`components/bms/include/bms/bms_jk.h` and reflash. Decoding is not at risk
either way — the parser dispatches on the reply's own type byte, not on what
was asked — so this only decides whether anything arrives at all.

**Fail — `crc_err` climbing instead of `frames`:** bytes are arriving but
failing the checksum. Suspect a fragment being dropped: check `dropped`. If
`dropped` is also climbing, the stream buffer is too small for the burst.

---

## 5. The update rate — the regression that hides in plain sight

**Do:** leave the tab open for a minute and watch the counters line.

**Pass:** `frames=` climbs by roughly **60 per minute** at the pack's own
cadence, and `dropped=` stays at 0.

**And listen.** The pack must be **silent**. A JK beeps on every command it
receives, so this driver sends one to learn the layout, one to start the
stream, and then nothing -- the frames arrive unasked. A beep once a second
means something is polling again; a beep every fifteen seconds is the silence
watchdog, which means the stream is dying and being restarted.

**Fail — `frames` climbs at about half that (25-35/min):** the transport is
losing every other frame. `jk_feed` stops at the end of each completed frame
and reports how far it got; if the caller resumes past that point instead of
at `*consumed`, the skipped bytes are the next frame's preamble and that whole
frame is lost. Chunks are 128 bytes against 300-byte frames, so a frame ends
mid-chunk almost every time.

This shipped once. It is invisible on the bench because the numbers are all
correct -- they just arrive half as often -- and no host test caught it because
every test fed whole frames. `tools/test/test_bms_jk.c` now sweeps five chunk
sizes for exactly this; note that sizes 20, 300 and 7 pass either way and only
128 and 512 expose it.

**What it costs on the vehicle:** a 2 s refresh where the poll asks for 1 s.

---

## 6. The values are actually right — the check that matters

Open the JK phone app side by side (after disconnecting the head unit, or on a
second pack) and compare the **first-frame dump**, which prints every cell:

```
I ble_bms:   cell 01  4008 mV  wire 3 mOhm
I ble_bms:   cell 02  4001 mV  wire 5 mOhm
```

| Observation | Meaning |
|---|---|
| Cells match the app, in order | Layout is right. |
| **Every cell shifted by the same amount** | Wrong layout — force the other one with `jk_set_proto()` and confirm. |
| Cell count wrong | Layout, or a pack smaller than the frame's capacity: unpopulated slots read 0 mV and are deliberately left invalid. |
| Voltages right, resistances nonsense | The resistance base uses **half** the 32S shift (64 on 24S, 80 on 32S). A test guards this, but hardware is the proof. |

Check the signs too:

- **Discharging must read positive current.** JK sends negative-for-discharge
  and the driver inverts, so it matches the VESC current on the dashboard.
  If the sign is backwards while the pack is clearly discharging, the
  inversion is wrong for this firmware.
- SoC against the app.
- Temperatures plausible for the room.

---

## 7. Fields that are meant to be absent

`charger_present` and `detail_log_count` are **not implemented** — the first is
not carried in the cell frame, the second lives in a frame type the driver does
not request. The tab must show them as unavailable, not as zero.

Check `valid_mask` in the dump against what the tab renders. Any field the mask
says is invalid must appear as `—`, never as a number.

---

## 8. The tab gate

**Do:** switch to the VESC tab, watch the console. Switch back.

**Pass:**

```
I ble_bms: tab hidden (stream keeps running either way)
... quiet ...
I ble_bms: tab shown (stream keeps running either way)
```

The link stays up — `connected` must not reappear — and reopening shows a
reading straight away, because the stream never stopped.

**This gate no longer changes what the radio does**, and that is deliberate.
Stopping the stream would take a command, and a command beeps; pausing and
resuming would cost two beeps for nothing. What it still does is reset the
first-frame dump so the next full listing appears in the log.

**Fail — no `tab hidden` line:** the gate is not reaching the backend. The two
sides must agree on their initial state; this has broken twice already.

---

## 9. Persistence

**Do:** wait ten seconds after pairing, power-cycle the board.

**Pass:** `restored bound peer from NVS`, then connect without pairing again.

**The address is written only after the pack answers device-info with a layout
the driver recognises** -- not on bind. So the wait is not superstition: pair,
confirm the `JK hw=... layout=...` line from step 3 has appeared, and only then
cut power. Power-cycling between the tap and that line leaves nothing stored,
and that is the intended behaviour, not a lost write.

**Also test the other half:** tap a pack you do NOT want -- a neighbour's, or
any non-JK device if one shows -- then power-cycle without waiting. It must
**not** come back. Before this gate, a mis-tap returned on every boot and the
only escape was unpairing something you never meant to pair.

---

## 10. Recovery when the pack is absent at boot

The path most likely to fail in the garage, and the newest code here.

**Do:** with a pack already paired, power the **board** up while the **pack is
off**. Wait a minute, then switch the pack on.

**Pass:**

```
I ble_bms: restored bound peer from NVS
I ble_bms: re-arming connect          <- every 5 s while nothing answers
...
I ble_bms: connected, conn=1          <- within seconds of the pack coming up
```

The tab must reach live on its own. Nobody should have to re-pair.

**Why this step exists:** `arm_connect()` is otherwise only reached from
connect-failed, disconnect, BLE sync and bind. When `ble_gap_connect()` is
refused outright it raises no GAP event, so none of those four ever fire
again -- the link sat in CONNECTING permanently and only re-pairing by hand
recovered it. The reachable trigger is boot: the peer restored from NVS arms
on sync while another central procedure still owns the single NimBLE
initiator.

**Fail — no `re-arming connect` line at all:** the guard is wrong. It requires
bound && not connected && not connecting && not scanning; check which one is
stuck, in that order.

**Fail — the line appears but the pack never joins:** re-arming is working and
the problem is elsewhere. Go back to step 1 with the pack powered.

**Note:** this step has no automated test and cannot get one on the host --
`main/ble_bms_client.c` depends on NimBLE and does not link off-target. The
whole transport and session layer is verified by inspection only. This test is
the coverage.

---

## 11. Coexistence — the one that needs a ride

Everything above can be done on a bench. This one cannot.

**Do:** with the BMS tab open and polling, start Android Auto projection over
Wi-Fi, and connect the phone companion app over BLE at the same time.

**Watch for:**
- AA video frame rate dropping below its usual 10–15 fps. The BLE radio is on
  the C6 and shares its SDIO path with Wi-Fi, so BMS polling competes with the
  video stream. This is why the poll gate exists.
- `reconnects` climbing on the BMS link.
- The companion app dropping.

`CONFIG_BT_NIMBLE_MAX_CONNECTIONS` is 3 and `ble_host.c` reserves two for
peripherals (phone app, VESC Tool over BLE). The BMS takes the third — the one
nominally set aside for the cadence sensor, which is unbound on this vehicle.
**Pairing a cadence sensor later will collide.**

---

## Reporting a failure

Include: model, hardware and software version from the JK app, the `JK hw=...
layout=...` line, the first-frame dump in full, and the counters line. Those
four together identify almost everything above without guesswork.
