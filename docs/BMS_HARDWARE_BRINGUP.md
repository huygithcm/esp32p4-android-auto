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
`decode_device_info()` treats hardware `11.x` as 32S and everything else as
24S. A version string outside that shape lands here and the driver refuses to
decode rather than guess. Report the exact string; widening the mapping is a
two-line change.

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

## 5. The values are actually right — the check that matters

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

## 6. Fields that are meant to be absent

`charger_present` and `detail_log_count` are **not implemented** — the first is
not carried in the cell frame, the second lives in a frame type the driver does
not request. The tab must show them as unavailable, not as zero.

Check `valid_mask` in the dump against what the tab renders. Any field the mask
says is invalid must appear as `—`, never as a number.

---

## 7. The poll gate

**Do:** switch to the VESC tab, watch the console. Switch back.

**Pass:**

```
I ble_bms: polling paused
... quiet ...
I ble_bms: polling resumed
```

The link stays up while paused — `connected` should not reappear. Reopening the
tab should show data within about a second, without a scan or reconnect.

**Fail — no `polling paused`:** the gate is not reaching the backend. The two
sides must agree on their initial state; this has broken twice already.

---

## 8. Persistence

**Do:** wait ten seconds after pairing, power-cycle the board.

**Pass:** `restored bound peer from NVS`, then connect without pairing again.

The wait matters: the write happens on bind, but cutting power immediately
after any settings change is a good way to lose it.

---

## 9. Coexistence — the one that needs a ride

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
