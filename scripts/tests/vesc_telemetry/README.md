# VESC telemetry decoder regression cases

Run `python scripts/test_vesc_telemetry.py --cc <host-gcc>`.
The runner compiles full production `vesc_rt_data.c`, `vesc_io_data.c` and
`buffer.c` into a unique temporary directory. No firmware/simulator output
directory or hardware is used. `--baseline-ref HEAD` checks committed sources.

Fixtures use the official VESC SETUP/ADC packet layout, verified identical in
6.05, 6.06 and 7.00. Pinned reference:
[commands.c](https://github.com/vedderb/bldc/blob/a0d40e2c5a42c810888d8c379307e6b0a118a125/comm/commands.c#L779).
Literal bytes are independent of the production encoders, so endian/scale
mistakes cannot cancel each other between test encoding and decoding.

| Case | Required behavior |
| --- | --- |
| video_temperature_values | Decode representative -99.9, 2, 1 and -17 C values while FET stays 30 C. These are fixtures, not captured hardware packets. |
| adc_does_not_change_temperature | ADC32 varying 0/50/100% and 1/2/3 V must never change/refresh RT temperature. |
| selective_motor_only | Selective mask without FET places motor immediately after the mask. |
| full_setup_layout | Check all fields in command47, then command51 with all known mask bits and independently changed motor temperature/current/time to prove acceptance. |
| selective_mixed_fields | Decode only advertised current/voltage/uptime; leave temperature values intact. |
| unknown_trailing_field | Future mask bit/trailing bytes do not prevent decoding known temperature fields. |
| legacy_all_prefixes_rejected | Reject every length0..69 prefix of the official70-byte command47 snapshot without modifying values/ages; the next complete response recovers. |
| truncated_adc | Every truncation of command32 leaves the IO snapshot untouched. |
| truncated_selective_no_refresh | A mask promising temperature bytes but no payload cannot make stale temperature fresh. |
| truncated_selective_atomic | Missing advertised current rejects the whole response, including temperature bytes. |
| truncated_current_no_field_shift | Partial 4-byte current cannot be consumed as a following 2-byte duty field. |
| all_selective_prefixes_and_recovery | Every prefix length 0..19 of a 20-byte selective reply is rejected atomically without refreshing stale data; a complete reply recovers after each rejection. |
| short_header | Missing selective header leaves snapshot unchanged. |
| other_commands | Lisp custom data/PPM/other values commands never affect SETUP snapshot. |
| request_includes_motor_identity | Poll always requests SETUP bit17 VESC_ID in addition to motor temperature. |
| can_motor_identity | Require payload logical motor ID; accept matching source or official dual-motor base alias including254-to0, reject inverse motor mixup, absent/truncated ID and invalid/unrelated envelope. |

Scope: packet-decoder behavior only. This cannot establish the ESC sensor
configuration, electrical input, Lisp scheduling, raw CAN integrity or actual
cause of the video. A temperature already encoded in a valid ESC packet is
preserved exactly; this suite does not hide or replace negative values.

Both command51 (echoed selective mask) and command47 (all22 known fields,
70 bytes including command) validate the complete known payload before any
snapshot mutation. Empty or unknown-only masks update no known data. Known
fields may still be followed by future fields. The malformed cases fail on
the pre-fix source; this is a reproducible parser defect, not proof that
malformed packets caused the recorded hardware temperature changes.

For baseline runs the runner uses matching RT/IO/datatype headers. Thermal
per-field freshness and CAN source identity are exercised in the separate
M-TEMP UI and CAN transport suites.

CAN identity follows official6.05 `comm/commands.c:848-855` (logical motorID),
`comm/comm_can.c:399-459` (base controller envelopeID), and
[`util/utils_sys.c:64-70`](https://github.com/vedderb/bldc/blob/a0d40e2c5a42c810888d8c379307e6b0a118a125/util/utils_sys.c#L64)
(second motorID=(base+1)%255). Both identity and the full known field layout
must validate before mutation; the direct parser remains usable for host
fixtures without transport metadata. These checks are not authentication.
For historical baseline identity cases, the fixture calls the baseline direct
parser as original `main.c` did; there was no sender-aware RT wrapper then.
