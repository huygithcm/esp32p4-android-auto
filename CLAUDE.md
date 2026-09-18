# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Shared Claude/Codex workspace

Claude, Codex, and the user edit the same working tree at
`C:\esp32p4-android-auto`. Before editing, read `AGENTS.md` and
`COLLABORATION_LOG.md`, inspect `git status --short --branch` plus relevant
diffs, and preserve every pre-existing change. Record material work and its
verification in `COLLABORATION_LOG.md` before handoff. Use repository-relative
paths in project files and do not pull, rebase, reset, clean, or stash across a
dirty shared working tree without explicit coordination.

---

## Đây là dự án gì

Firmware ESP32-P4 cho một thiết bị **hai vai trò** trên cùng một màn cảm ứng 800×480:

1. **Chính — dashboard VESC**: telemetry xe điện qua CAN/TWAI (pin, tốc độ, nhiệt
   độ, odometer, trip), menu cấu hình VESC ngay trên thiết bị, cầu BLE cho VESC Tool.
2. **Bonus — Android Auto Wireless**: điện thoại chiếu màn hình qua Wi-Fi
   (H.264 software decode), touch được forward ngược về máy.

Kèm theo: app Flutter companion (`flutter-application/`), script LispBM chạy
**trên VESC** (`lisp/main.lisp`), và hai firmware phụ được nhúng sẵn trong image P4.

> **Trạng thái thực tế, đừng tin phần comment cũ trong code:**
> `CONNECTION_MODE` mặc định là `MODE_WIRELESS_HELPER` (Mode B).
> **`MODE_BT_CLASSIC` (Mode A) CHƯA implement** — `main/main.c:606` là `#error`.
> Thực tế đang chạy: Mode B + **module BT agent ngoài** (ESP32-WROOM/D1 Mini) lo
> phần Classic BT handshake và đẩy Wi-Fi creds sang P4 qua UART1.
> Cổng AA là **5288** (`AA_TCP_PORT`), mDNS service là **`_aawireless._tcp`**
> (Wireless Helper APK hardcode port, chỉ lấy IP từ mDNS).

Tài liệu kiến trúc chi tiết (sơ đồ thư mục đầy đủ, luồng boot từng bước, luồng
AA / VESC / BLE): **`docs/ARCHITECTURE.md`**. File này chỉ giữ phần cần cho việc
sửa code hằng ngày.

---

## Lệnh thường dùng

Luôn `. "$IDF_PATH/export.sh"` trước. ESP-IDF **v5.5+**, target `esp32p4`
(thêm `esp32` nếu build firmware BT agent).

### Build & flash firmware P4

```bash
idf.py build                                       # = board Waveshare, vào build/
idf.py -p <PORT> flash monitor

scripts/build_board.sh                             # build TẤT CẢ board
scripts/build_board.sh waveshare build             # → build_waveshare/
scripts/build_board.sh jc4880 -p <PORT> flash monitor   # → build_jc4880/
scripts/build_board.sh waveshare menuconfig        # đổi Kconfig cho 1 board
scripts/build_board.sh jc4880 size                 # báo cáo size — xem §"ngân sách flash"
```

`build_board.sh` chạy trong build dir riêng + overlay
`SDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.defaults.<board>"`.
Không dùng `idf.py` trần cho jc4880 — sẽ build nhầm sdkconfig của Waveshare.

### OTA (sau lần flash USB đầu tiên)

```bash
scripts/ota_push.sh                                # build/…bin → SoftAP 192.168.4.1/ota
BIN=build_jc4880/esp32p4_android_auto.bin scripts/ota_push.sh
```
BLE OTA thì qua app Flutter. Lần flash đầu bắt buộc USB hoặc Wi-Fi.

### Test

Interactive Windows simulator: launch outside the sandbox with the required
tool approval, `-WindowStyle Normal`, and `SDL_VIDEODRIVER=windows`.
Sandboxed launch previously created a responsive but user-invisible window;
the user confirmed visibility after relaunch outside the sandbox. Build and
headless tests may remain sandboxed. See `docs/SIMULATOR_WINDOWS_LAUNCH.md`.

Không có test on-target. Có 2 bộ test chạy trên host:

```bash
# 1. Serdes config VESC (C, host) — verify signature crc32c + byte stream
#    khớp với reference Python
cc -std=c11 -I components/vesc_config/include -I components/vesc_can/include \
   tools/test/test_serdes.c components/vesc_config/vesc_config_serdes.c \
   components/vesc_config/generated/*.c \
   components/vesc_can/buffer.c components/vesc_can/crc.c -lm -o /tmp/test_serdes
/tmp/test_serdes

python3 tools/gen_vesc_config.py --self-test       # reference của test trên

# 2. App Flutter (dart test — lisp lint/syntax/patch, BLE agent, helper fw…)
cd flutter-application && flutter test
flutter test test/lisp_lint_test.dart              # chạy 1 file
flutter test --plain-name "<tên test>"             # chạy 1 test
flutter analyze                                    # lint (analysis_options.yaml)
```

### Debug trên phần cứng thật

```bash
scripts/capture.sh 60 [--flash]      # log serial P4 → logs/<timestamp>.log
scripts/capture_bt.sh                # log module BT agent
scripts/capture_phone.sh             # logcat điện thoại (AA/Gearhead)
scripts/capture_all.sh               # cả ba cùng lúc

# Screenshot + inject touch (cần CONFIG_DEBUG_UART_BRIDGE=y, mặc định OFF)
python3 scripts/uart_debug.py -p <PORT> screenshot scr.png
python3 scripts/uart_debug.py -p <PORT> tap 400 240
python3 scripts/uart_debug.py -p <PORT> swipe 700 240 100 240 300
```
`uart_debug.py` mở port ở chế độ **no-reset** → kết nối KHÔNG reboot thiết bị,
màn hình đang hiện được giữ nguyên.

### App Flutter & release

```bash
scripts/build_app.sh [--install|--debug|--with-key]   # APK (mặc định KHÔNG nhúng API key)
scripts/install_app.sh                                # adb install -r bản release mới nhất

scripts/release.sh                  # patch-bump fw+app, build mọi board, bundle APK
scripts/release.sh 1.3.0 0.2.0      # chỉ định <fw_version> <app_version>
```
`release.sh` **không commit** — tự review rồi commit `version.txt`,
`flutter-application/pubspec.yaml`, `release/*`, assets firmware đã stage.

---

## Kiến trúc — những gì cần đọc nhiều file mới hiểu

### Ba tầng OTA, chỉ cần flash P4

| Tầng | Cơ chế | File |
|---|---|---|
| **P4** | HTTP `/ota` hoặc BLE OTA, 2 slot `ota_0`/`ota_1` | `main/ota_http.c`, `main/ble_ota.c` |
| **ESP32-C6** (Wi-Fi/BLE radio) | `network_adapter.bin` nhúng vào image P4 qua `EMBED_FILES`; boot so version qua `esp_hosted_get_coprocessor_fwversion` → lệch thì OTA qua SDIO → P4 tự restart | `main/c6_ota.c`, `components/c6_ota_partition/` |
| **BT agent ngoài** | blob `.bin.gz` nhúng; boot đọc dòng `BT-VER:` qua UART → lệch thì ép vào ROM bootloader bằng RST/IO0 và reflash | `main/bt_agent_ota.c`, `components/bt_agent_fw/` |

→ Không bao giờ phải flash C6 hay BT agent riêng; rebuild P4 với blob mới là đủ.
BT agent OTA mặc định **tắt** (`CONFIG_BT_AGENT_OTA_ENABLED=n`) — không bật thì
toàn bộ đường BT là no-op kể cả khi có module cắm vào.

### Thứ tự boot trong `app_main` là ràng buộc, không phải ngẫu nhiên

Chi tiết 14 bước ở `docs/ARCHITECTURE.md` §3. Những phụ thuộc dễ phá nhất:

- `log_capture_init` phải chạy **trước mọi log khác**.
- `aa_overclock_400mhz_apply` phải chạy **trước khi init peripheral**.
- `install_lvgl_touch_indev` gỡ indev của BSP và thay bằng indev đọc từ
  `touch_input` — **chỉ được có MỘT reader GT911**, hai reader sẽ race trên I2C.
- `ui_mode_init` dựng dashboard GUI-Guider mất ~5 s → phải tạm gỡ IDLE0 khỏi TWDT.
- `display_video_init` (sink) phải trước `h264_pipe_init` (decoder).
- `trip_log` pre-erase lúc boot: erase flash giữa chuyến làm khựng DSI.

### Hai chế độ touch, một reader

`touch_input.c` giữ `TOUCH_MODE_AA` ↔ `TOUCH_MODE_LVGL`; `ui_mode.c` đổi mode
bằng **gesture 3 ngón**. Ở mode AA, indev LVGL luôn báo `released` nên dashboard
không nhận touch (và ngược lại). Thêm màn hình mới phải nghĩ tới mode nào đang bật.

### Dashboard LVGL: `generated/` vs `custom/`

`Super_VESC_Display/` là project **GUI-Guider**, compile vào firmware qua component `vesc_ui`.

- `Super_VESC_Display/generated/` bị **ghi đè** mỗi lần export lại từ GUI-Guider
  → **không bao giờ để logic cần giữ ở đây**.
- `Super_VESC_Display/custom/` là code viết tay, sống sót qua regen: framework
  theme (`dashboard_theme.c` + `theme_*.c`), menu config VESC, LISP editor,
  trip statistics, PAS screen.
- Glue phía firmware nằm ở `main/` (`vesc_ui_updater.c`, `ui_mode.c`, `*_screen.c`).
- Theme: hàm `update_*()` do GUI-Guider sinh là **dispatcher** gọi ops của theme
  đang active; theme lưu ở NVS key `dash_theme`.

### Bẫy LVGL trên board này (đã trả giá, đừng lặp lại)

- **LVGL v8.4**, RGB565. Config duy nhất là `main/lv_conf.h` với
  `CONFIG_LV_CONF_SKIP=n` → **mọi symbol `CONFIG_LV_*` trong Kconfig bị bỏ qua**.
  `CMakeLists.txt` gốc thêm `main/` vào include path toàn cục chính vì lý do này.
- **Không đụng NVS trong thread LVGL** — `nvs_commit()`/erase làm đơ màn hình.
  Debounce bằng cache RAM + persist trễ ngoài thread LVGL.
- Display mode phải là **DOUBLE_FULL + ROTATE_90**. Đổi sang TRIPLE_PARTIAL +
  ROTATE_90 sẽ dính bug mất ISR của PPA/DMA2D trên P4 → UI đứng.
- Panel gốc **480×800 portrait**, xoay 90° → không gian logic 800×480 landscape.
- Font mặc định Montserrat **chỉ có ASCII**: ký tự `…` (U+2026) ra ô tofu →
  luôn dùng `...`.
- Mỗi lần show/hide keyboard **bắt buộc** resize container nội dung kèm theo.

### Ngân sách flash — jc4880 (16 MB) là ràng buộc chặt nhất

`partitions_16mb.csv`: OTA slot 5 MB ×2, image ~3.8 MB → chỉ dư ~1.2 MB.
(Waveshare 32 MB: OTA 2×7.875 MB, thoải mái.) Thêm font hay feature lớn phải
kiểm `scripts/build_board.sh jc4880 size`. Giữ option debug
(`CONFIG_DEBUG_UART_BRIDGE`, tốn flash + ~1 MB PSRAM) **tắt** trên jc4880 và
trong bản release. Font Antonio được **auto-subset lúc build firmware** — bản
ASCII đầy đủ ~1.4 MB sẽ vỡ slot. (Simulator KHÔNG auto-subset, phải regen tay.)

### Codegen bảng config VESC

`components/vesc_config/generated/` được sinh **offline**, không phải build-time:

```bash
python3 tools/gen_vesc_config.py --vesc-root <vesc_tool-master> \
    --out components/vesc_config/generated --versions 6.05,6.06,7.00
```
Nó parse XML metadata của VESC Tool và bake sẵn *signature* crc32c theo thứ tự
serialize, để thiết bị tự kiểm bảng của mình có khớp firmware VESC đang nói
chuyện không. Sửa `vesc_config_serdes.c` hay bảng generated → **chạy lại
`test_serdes`**, và cập nhật `EXPECT[]` trong test nếu thêm version firmware.

---

## Hỗ trợ nhiều board (mục cũ: "Поддержка нескольких девайсов")

`idf.py build` trần = **Waveshare 4.3"** (mặc định, giữ tương thích ngược).
Board khác dùng `scripts/build_board.sh <board> <idf.py args>`.

- **Kconfig `choice BOARD_MODEL`** (`main/Kconfig.projbuild`):
  `CONFIG_BOARD_WAVESHARE_43` (default) / `CONFIG_BOARD_JC4880P443C`. Global →
  đọc được trong BSP và `main/bt_link.h`.
- Choice này **không** set flash size / partition table / chân CAN — những thứ
  đó nằm ở overlay `sdkconfig.defaults.<board>`: `waveshare` (32 MB,
  `partitions.csv`, CAN 48/47) và `jc4880` (16 MB, `partitions_16mb.csv`,
  CAN 51/52). **Phải giữ hai bên lockstep.**
- Pin/timing không diễn đạt được bằng Kconfig sẵn có (backlight/reset LCD, I2S
  DSIN/PA, DPI timing, vendor-init ST7701, pin BT agent) → `#if
  CONFIG_BOARD_JC4880P443C` trong BSP `components/esp32_p4_wifi6_touch_lcd_4_3/`
  và `main/bt_link.h`.
- **Pin JC4880**: BT agent `TX=33 RX=31 RST=30 IO0=29`, CAN `RX=52 TX=51`,
  backlight LCD `23`, reset LCD `5`. Panel ST7701S, DPI 34 MHz. Wi-Fi (SDIO→C6),
  I2C touch, SD giống Waveshare.
- **`BOARD_MODEL_ID`** (`main/board.h`, `"waveshare"`/`"jc4880"`) được firmware
  báo cho app (BLE OTA-info `…0006` field thứ 6 + `GET /info`) để APK chọn đúng
  binary trong số firmware đã nhúng.
- Thêm board mới: thêm slug vào `BOARDS=()` trong `build_board.sh` **và**
  `release.sh`, thêm `sdkconfig.defaults.<board>`, thêm nhánh Kconfig + `#if`
  trong BSP, thêm `BOARD_MODEL_ID`.

Ba nhánh version độc lập: firmware P4 (`version.txt` → `PROJECT_VER`, đổi rồi
phải `reconfigure`), app (`pubspec.yaml`), firmware BT agent (bump riêng qua
`tools/pack_fw_blobs.sh`, `release.sh` không đụng tới).

---

## Tái tạo artifact bị .gitignore ("Воспроизведение игнорируемых артефактов")

`tools/` và `research/_sources/` cố ý **không commit** (xem `.gitignore`):

```bash
# Reference source AA / dongle / Waveshare BSP
mkdir -p research/_sources && cd research/_sources
git clone --depth 1 https://github.com/f1xpl/aasdk
git clone --depth 1 https://github.com/f1xpl/openauto
git clone --depth 1 https://github.com/andreknieriem/headunit-revived headunit
git clone --depth 1 https://github.com/Nicba1010/WirelessAndroidAutoDongle
git clone --depth 1 https://github.com/waveshareteam/ESP32-P4-WIFI6-Touch-LCD-4.3 waveshare_p4_4_3

# tools/c6_slave_fw — build firmware C6 (chỉ cần khi update network_adapter.bin)
cd <repo_root>/tools && idf.py create-project-from-example "espressif/esp_hosted^2.12.6:slave"
mv slave c6_slave_fw && cd c6_slave_fw && idf.py set-target esp32c6 && idf.py build
cp build/network_adapter.bin ../../components/c6_ota_partition/slave_fw_bin/

# tools/c6_ota_flasher — flasher OTA standalone (phòng khi firmware chính hỏng)
cd .. && idf.py create-project-from-example "espressif/esp_hosted^2.12.6:host_performs_slave_ota"
mv host_performs_slave_ota c6_ota_flasher
```

BSP trong `components/esp32_p4_wifi6_touch_lcd_4_3/` lấy từ
`research/_sources/waveshare_p4_4_3/examples/esp-idf/07_Displaycolorbar/components/`
rồi patch thêm cho board thứ hai. Demo hữu ích: `08_lvgl_demo_v9`,
`09_video_lcd_display`, `10_mp4_player`, `11_esp_brookesia_phone`.

`tools/bt_agent/` (firmware BT agent, ESP-IDF target `esp32`) cũng git-ignored;
blob đã build nằm sẵn trong `components/bt_agent_fw/` nên build firmware P4 không
cần nó.

---

## Lưu ý về Android Auto stack

Là bản port của [aasdk](https://github.com/f1xpl/aasdk) sang ESP-IDF:
Boost.Asio → FreeRTOS task + lwIP socket, OpenSSL → mbedTLS, protobuf → nanopb,
Boost.Log → `ESP_LOG`. File `.proto` lấy từ aasdk, regen cho nanopb.

Những hằng số trong `main/config.h` có comment giải thích **tại sao**, đọc trước
khi đổi. Đáng chú ý:

- `ENABLE_AUDIO 1` là **bắt buộc** — Gearhead 1.7 từ chối head unit không có
  audio channel (SD response 168 byte → `ERR_INVALID_STATE` → đóng TCP).
- `ENABLE_AUDIO_MEDIA`/`SPEECH` = 0 có chủ đích: nhạc và TTS nav ở lại điện
  thoại, tránh chiếm băng thông TCP của đường video.
- `aa_x509_patch.c` tồn tại để vá lỗi UTCTime wrap của mbedTLS với cert AA.

Reference: [aasdk](https://github.com/f1xpl/aasdk) ·
[openauto](https://github.com/f1xpl/openauto) ·
[headunit-revived](https://github.com/andreknieriem/headunit-revived) ·
[esp-h264](https://github.com/espressif/esp-h264)

---

## Skills có sẵn (`.claude/skills/`)

`head-unit` (bản đồ project) · `build-flash` · `release` · `capture-logs` ·
`device-screen` (screenshot + inject touch trên hardware thật) · `dashboard-ui`
(bẫy LVGL, theme, font). Dùng chúng thay vì suy luận lại quy trình.
