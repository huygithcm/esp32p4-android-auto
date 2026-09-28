# M-TEMP transport reproduction

Run from the repository root:

```powershell
python scripts/test_mtemp_transport.py --cc C:/msys64/mingw32/bin/gcc.exe
python scripts/test_mtemp_transport.py --cc C:/msys64/mingw32/bin/gcc.exe --baseline-ref e7f0b16
python scripts/test_mtemp_transport.py --cc C:/msys64/mingw32/bin/gcc.exe --baseline-ref 7c77f2b
```

The runner compiles current production `comm_can.c`, `vesc_rt_data.c`,
`vesc_io_data.c`, `buffer.c` and `crc.c` into an executable in a unique temporary
directory. The receive decoder is exposed by including its unchanged production
translation unit. RT and IO stay separate translation units. Each case runs in a
fresh process. No shared firmware build, release package or hardware is touched.
Platform headers are adapted from the existing S3 transport host mocks; no S3
production patch is applied. The dispatcher calls the real RT/IO handlers as
`main/main.c` does; unrelated handlers are outside this test boundary. These are
synchronous parser tests, not thread, driver, BLE or bus timing simulations.
With `--baseline-ref`, all five production C files and VESC CAN public headers
are read from the resolved exact Git commit into the private temporary directory.
The fixture and platform stubs remain the same across revisions.

## RC2 acceptance results (2026-09-28)

Current code: **22/22 PASS**, exit 0. The same expanded fixture against exact
pre-fix commit `78356d73b92d3a8c566915339de7dfa2e9b6418a` returns **10/22 PASS**,
exit 1. The malformed control DLC case independently exits with Windows access
violation `3221225477` on the baseline; each case runs in its own process.
UTF-8 logs: `docs/diagnostics/rc2-2026-09-28/transport-{baseline,current}.txt`.

Fixes use the contiguous-offset/completed-length invariant in official VESC
6.05 `comm/comm_can.c:1567-1658` (also present in 7.00), with the port's existing
destination isolation retained. Missing, duplicate, out-of-order or oversized
fragments discard the in-progress message; a new offset-zero reply recovers.
Control DLC and STATUS1-6 minimum lengths are checked before reading bytes.
Incomplete assembly waits for the existing bounded poll timeout; CRC mismatch
still signals the poll completion semaphore as before.

The additive `comm_can_get_packet_sender_id()` is valid only in the synchronous
callback. Current fixture dispatch calls the actual RT CAN wrapper with that
sender, as `main/main.c` does. The RT wrapper requires SETUP bit17 motor identity
to match the configured target and allows the envelope sender to be that target
or its official dual-controller base ID (`(sender + 1) % 255`). This preserves
VESC 6.05 internal motor2 replies, whose envelope contains the base ID while
payload bit17 identifies motor2. Missing identity, another motor identity or an
inconsistent envelope cannot update the local RT snapshot, while
the generic transport callback still delivers other-node packets to bridge
consumers. Historical builds use their original unfiltered RT handler.

Additional checks beyond the original nine cover duplicate trailers and
missing prefix, out-of-order delivery/recovery, a full 1024-byte configuration
payload across SHORT/LONG fragment offsets, malformed control DLC, all six
STATUS lengths, overflow/recovery, sender scope for short bridge replies, and
duplicate nonzero fragment/recovery. The 1024-byte check verifies the delivered
length and CRC. Wrong-source RT checks verify the entire snapshot is unchanged
while the generic callback still receives the packet.

Identity fixtures explicitly cover target11/sender10/payloadID11 acceptance,
target10/sender10/payloadID11 rejection, target10/sender11/payloadID11 rejection,
target10/sender11/payloadID10 rejection, missing bit17 rejection and the official
base254/second-motor0 wrap. Every complete message still reaches the generic
callback. Reference: official 6.05 `comm/commands.c:849-857`,
`comm/comm_can.c:399-458`, and `util/utils_sys.c:64-70`.

Valid thermal fixtures now include bit17, matching the RC2 dashboard poll. The
retained-tail numerical case uses logical ID0 so B's overlapping identity byte
equals A's old motor high byte, preserving the original reconstruction defect
even with the added ID field. Its CRC still describes A, so this remains a
coverage/freshness defect, not numerical corruption relative to the trailer.

No physical bus or hardware cause is established by these host tests. CAN FILL
frames have no source ID or transaction ID; they cannot identify arbitrary
interleaved same-destination producers. CRC and contiguous coverage reject
ordinary corruption but do not provide authenticated source attribution.

## Original investigation results before RC2 fixes

The original nine-case suite returned **exit 1: 5/9 PASS**, retaining failing
assertions for four violated contracts. This is not an all-green acceptance test.

The same suite was compiled and run against both historical source revisions:

| Production source revision | Result |
|---|---|
| `e7f0b166c45d01a7c158ba3b2e1af3e27640fdb6` | 5/9 PASS, exit1, same four failures |
| `7c77f2b3595fcc0258e26d2049210c4e507bf8d8` | 5/9 PASS, exit1, same four failures |
| Pre-fix working tree | 5/9 PASS, exit1, same four failures |

These four transport conditions therefore predate the newer Lisp release. The
tests do not show they caused the symptom that appears only with the newer Lisp.

| Case | Desired contract and observed result |
|---|---|
| `valid_fragmented_video_values` | PASS: intact command51 CAN fragments reconstruct representative -99.9/2/1/-17 C exactly. These are fixtures, not captured hardware packets. |
| `adc_custom_interleaving` | PASS: complete ADC32 replies spanning 0/50/100% and Lisp VP safety replies between RT packets do not change any RT field or timestamp. |
| `overlapping_replies_crc_drop` | PASS: overlapping RT and ADC fragments with a mismatched CRC are dropped; subsequent intact reply recovers. |
| `missing_changed_tail_crc_drop` | PASS: missing new motor bytes that differ from retained bytes fail CRC and preserve old RT snapshot. |
| `missing_unchanged_tail_refresh` | FAIL: missing motor bytes identical to retained bytes pass CRC, redeliver the reply and refresh time1000 ->5000 ms without receiving those bytes. |
| `missing_tail_numeric_replay` | FAIL: receive9-byte FET+motor reply A (12.5 C), then7-byte motor-only reply B (-17 C); receive only A's first7 bytes and A's CRC trailer. Old A tail remains in the reused buffer; temperature reverts -17 ->12.5 C. |
| `wrong_sender_temperature` | FAIL: configured target10, then valid reply sender11 addressed to P4 changes temperature12.5 ->2 C. Callback supplies no sender identity to RT decoder. |
| `status4_is_separate` | PASS: STATUS4 (-17 C) updates its own per-ID store and leaves RT12.5 C unchanged. |
| `short_status4_dlc` | FAIL: DLC2 STATUS4 nevertheless reads motor bytes outside declared DLC and stores -17 C. All8 backing bytes are initialized to avoid undefined memory reads; bytes2..7 deliberately model stale backing storage. RT remains12.5 C. |

## What these mechanisms do and do not prove

CRC mismatch normally causes dropped packets, not ADC reinterpretation as
temperature. The stale-tail case passes CRC because all reconstructed bytes
exactly match an older valid packet. It requires missing fragments with retained
matching data and a matching process trailer; it does not make arbitrary ADC
bytes pass CRC. A changed numeric replay additionally requires an intervening
reply with another layout, or equivalent traffic retaining the old tail while
updating the snapshot. The normal dashboard poll uses a fixed selective mask;
this test does not establish that the alternate mask occurred on the user's bus.
In particular, `finish(temp_a)` supplies the CRC of the 12.5 C packet itself.
The accepted reconstructed bytes therefore match the synthetic sender's packet
intent; this test proves missing-fragment coverage and freshness checks, **not
numerical corruption relative to that CRC trailer or invented ADC conversion**.

The pinned official VESC 7.00 reference differed from the pre-fix P4 port here:
`research/_sources/vesc-official-audit/bldc-7.00/comm/comm_can.c:1717-1723`
selects a completed buffer by checking its accumulated `rx_buffer_offset` equals
the advertised `rxbuf_len`. The pre-fix P4 port selected by destination ID and
did not track fragment coverage. This reference is a source comparison only;
the official STM32 transport is not compiled by this host suite.

Wrong-source overwrite needs another valid reply sender, such as another node
or a routed response. A single isolated ESC cannot acquire a second sender just
because its throttle moves. The short-DLC bug affects STATUS4's separate store;
it does not explain the dashboard RT temperature path exercised here.

These software mechanisms are reproducible but **none is confirmed as the
video's cause**. Valid ADC/safety traffic does not produce the symptom. Bus/raw
reply capture, node topology, and direct-versus-bridged VESC Tool route are needed
to determine whether any relevant condition actually occurs on the user's ESC.
