# ESP32-S3 source branch

Read README.md and COLLABORATION_LOG.md before editing. This is a source-only
port seed, not a working S3 firmware. Keep Android Auto, ESP-Hosted/C6, P4 BSP,
BT agent binaries and build artifacts out of this branch. Preserve licenses.

Use repository-relative paths. Check git status/diffs and preserve others'
changes. Do not assume any GPIO, flash, PSRAM, LCD or touch configuration until
the board is identified. Do not remove the build guard until the S3 startup and
dependency graph are implemented. Tests must not actuate hardware implicitly.

Log material changes and checks in COLLABORATION_LOG.md. Interactive simulator
launches, if added later, must use approved outside-sandbox execution with a
normal visible window; headless tests/builds may run sandboxed.
