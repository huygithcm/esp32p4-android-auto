# ESP32-S3 collaboration log

## 2026-09-18 — Codex — Source-only split

- User requested a separate branch with only source needed to reimplement a
  VESC display with mode settings and BLE BMS on ESP32-S3, without Android Auto.
- Selected source from P4 commit 7c77f2b; source attribution/license headers
  preserved. Shared P4 branch and working tree are not deleted or switched.
- Root commit has no P4 ancestors; allowlist excludes compiled artifacts,
  vendor SDKs, Android Auto, companion apps and P4/C6/helper firmware.
- Kept UI support dependencies and documented platform integration gaps in
  README. CMake intentionally refuses a premature firmware build.
- Host parser, transport, gear mapping and limited Lisp evaluator are carried
  over; no S3 firmware build, flash or hardware validation is claimed.
- Verification on a checkout of the new source tree: Lisp 13/13, transport
  53/53, parser/freshness 2532/2532, gear mapping 81/81 pass. The tree has
  161 files and one root commit; excluded-artifact/path scan found no matches.
- Root whitespace check reports pre-existing whitespace in copied UI/generated
  assets; source blobs were deliberately preserved rather than reformatted.
