#pragma once

#include "sdkconfig.h"

/* Compile-time board identity.
 *
 * The active board is chosen by the Kconfig `choice BOARD_MODEL`
 * (main/Kconfig.projbuild), set per build via an sdkconfig.defaults.<board>
 * overlay.
 *
 * BOARD_MODEL_ID is the short slug the firmware advertises to the companion app
 * (BLE OTA-info characteristic + GET /info) so the app can pick the matching
 * bundled firmware image during over-WiFi OTA. Keep these strings in sync with
 * the asset names in flutter-application/ and the release artifact names in
 * scripts/release.sh.
 *
 * PLACEHOLDER: the three slugs below track the target tiers in
 * docs/ESP32_S3_PORT_PLAN.md. No BSP implements any of them yet — rename them
 * to the real board names once the panels are pinned down, and update the app
 * assets at the same time. */
#if CONFIG_BOARD_S3_SMALL
#define BOARD_MODEL_ID    "s3-small"
#define BOARD_MODEL_NAME  "ESP32-S3 2.8\""
#elif CONFIG_BOARD_ESP32_SMALL
#define BOARD_MODEL_ID    "esp32-small"
#define BOARD_MODEL_NAME  "ESP32 2.8\""
#else
#define BOARD_MODEL_ID    "s3-rgb"
#define BOARD_MODEL_NAME  "ESP32-S3 800x480 RGB"
#endif
