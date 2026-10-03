# Linux workspace for ESP32-P4

This setup installs ESP-IDF **v5.5.3** (matching `dependencies.lock`),
ESP32-P4 and ESP32 toolchains, CMake 3.x, Ninja and the IDF Python environment.
Component Manager is pinned to 2.4.11. A local compatibility patch uses its
documented Linux parent-PID method for temporary files because `psutil` cannot
read this workspace's process namespace. Firmware sources are unaffected.
The SDK and downloaded tools live beside the repository. Python 3, pip and
Git must already be installed. Downloads require access to GitHub, PyPI and
the Espressif component registry.

```bash
scripts/setup_workspace.sh
. scripts/workspace_env.sh
scripts/build_board.sh waveshare build
# For Guition JC4880P443C instead:
scripts/build_board.sh jc4880 build
```

In every new shell, source `scripts/workspace_env.sh` before using `idf.py`.
Use `P4_IDF_PATH` and `P4_IDF_TOOLS_PATH` to override the default locations.
Build outputs stay in `build_waveshare/` and `build_jc4880/`.

This is a Linux build environment. It does not emulate the ESP32-P4 hardware.
Flashing requires a serial-connected board on the machine running the flash
command, or a reachable device OTA endpoint. The Android companion app needs
a separate Flutter/Android SDK installation.

## Verified in this workspace

On 2026-10-03, Ubuntu 24.04 / Python 3.12, repository base `842b25e`:

- ESP-IDF v5.5.3 activated with its Python dependency check passing.
- All ESP-IDF submodules match their pinned commits.
- `scripts/build_board.sh waveshare build` completed successfully.
- Application binary: `build_waveshare/esp32p4_android_auto.bin`.
- Binary size: 4,385,600 bytes; smallest app slot: 8,257,536 bytes (47% free).
- Firmware has not been flashed or tested on physical hardware.
