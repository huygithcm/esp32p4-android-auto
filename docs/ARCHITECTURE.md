# Kiến trúc & luồng hoạt động — ESP32-P4 Android Auto Head Unit

> Tài liệu tổng quan để nắm nhanh toàn bộ cấu trúc repo và các luồng dữ liệu.
> Spec chi tiết (ma trận board, 2 chế độ kết nối, repo tham khảo, quy trình
> release) nằm trong `CLAUDE.md`. Phiên bản firmware hiện tại: xem `version.txt`.

---

## 1. Bức tranh tổng thể

Thiết bị là **head unit Android Auto không dây** trên ESP32-P4 (màn cảm ứng
800×480), đồng thời là **dashboard VESC** cho xe điện qua CAN bus. Khi điện
thoại chiếu AA thì màn hình hiện video H.264; khi không chiếu thì hiện
dashboard LVGL với dữ liệu VESC realtime.

```
[Điện thoại Android]
   │  ①  BT Classic handshake (qua BT agent ngoài)  →  nhận SSID/PSK SoftAP
   │  ②  Join Wi-Fi SoftAP của P4 (radio nằm trên ESP32-C6, nối P4 qua SDIO)
   │  ③  TCP :5277  →  TLS  →  giao thức AA Wireless (protobuf)
   ▼
[ESP32-P4] ── decode H.264 (software) ──► LCD ST7701 800×480 MIPI-DSI
   ▲                                          ▲ touch GT911 → AA InputChannel
   │ CAN (TWAI)                               │ hoặc LVGL dashboard
[VESC controller] ◄── LispBM script (lisp/main.lisp)
```

### Phần cứng

| Thành phần | Vai trò |
|---|---|
| **ESP32-P4** | CPU chính: AA stack, H.264 decode, LVGL, CAN, HTTP |
| **ESP32-C6** (trên board) | Radio Wi-Fi 6 + BLE, nối P4 qua **SDIO (ESP-Hosted)** |
| **BT agent ngoài** (D1 Mini ESP32) | Classic BT handshake AA Wireless, nối P4 qua **UART1** |
| **LCD ST7701** 800×480 MIPI-DSI + **GT911** touch | Hiển thị + cảm ứng |
| **VESC** | Nối qua CAN (TWAI), chân theo board |

### Hai board được hỗ trợ (Kconfig `BOARD_MODEL`)

| | Waveshare 4.3" (mặc định) | Guition JC4880P443C |
|---|---|---|
| Flash | 32 MB → `partitions.csv` | 16 MB → `partitions_16mb.csv` |
| OTA slot | 2 × ~8 MB (dưới mốc 16 MB — bug bootloader_mmap idf#18051) | 2 × 5 MB (image ~3.8 MB, dư ~1.2 MB) |
| CAN RX/TX | 48/47 | 52/51 |
| Build | `idf.py build` hoặc `build_board.sh waveshare` | `build_board.sh jc4880` → `build_jc4880/` |

Khác biệt pin/timing panel nằm trong BSP qua `#if CONFIG_BOARD_JC4880P443C`;
định danh `BOARD_MODEL_ID` (`main/board.h`) được báo cho app để chọn đúng
firmware khi OTA.

---

## 2. Cấu trúc thư mục

```
esp32p4-android-auto/
├── main/                      # Firmware P4 — toàn bộ logic chính (~15k dòng C)
│   ├── config.h               #   Chế độ kết nối (CONNECTION_MODE), hằng số AA
│   ├── Kconfig.projbuild      #   Chọn board, vai trò Wi-Fi, debug bridge…
│   ├── main.c                 #   app_main: trình tự khởi động (xem §3)
│   │
│   │── ── Android Auto stack (port aasdk) ──
│   ├── aa_frame.c             #   Đóng/mở frame AA trên TCP
│   ├── aa_tls.c               #   TLS server (mbedTLS) + aa_certs.h
│   ├── aa_x509_patch.c        #   Vá UTCTime wrap cho cert AA
│   ├── aa_handshake.c         #   Version negotiation, SSL handshake flow
│   ├── aa_proto.c             #   Encode/decode protobuf (nanopb)
│   ├── aa_service.c           #   Service discovery + các channel (video/audio/input/sensor)
│   ├── aa_overlay.c           #   Overlay LVGL đè lên video AA
│   ├── aa_overclock.c         #   P4 lên 400 MHz (Kconfig)
│   │
│   │── ── Video ──
│   ├── h264_pipe.c            #   Ring buffer NAL → esp_h264 SW decode → I420
│   ├── display_video.c        #   I420 → blit thẳng ra panel (PPA scale)
│   ├── display_init.c         #   BSP display + LVGL bring-up
│   │
│   │── ── Mạng / transport ──
│   ├── wifi_manager.c         #   SoftAP (hoặc STA bench) qua C6/ESP-Hosted
│   ├── tcp_server.c           #   TCP server :5277
│   ├── mdns_advertise.c       #   _androidauto._tcp cho Wireless Helper
│   ├── ota_http.c             #   HTTP server: OTA P4 (/ota, ota_push.sh)
│   ├── files_http.c           #   └─ /files: web file manager (/vescfs + SD)
│   ├── lisp_http.c            #   └─ /lisp:  web editor LispBM cho VESC
│   │
│   │── ── BT agent ngoài ──
│   ├── bt_link.c              #   UART1 P4↔agent: đẩy Wi-Fi creds, lệnh reconnect
│   ├── bt_agent_ota.c         #   So BT-VER, reflash agent từ blob nhúng
│   │
│   │── ── BLE (NimBLE host trên C6 qua VHCI) ──
│   ├── ble_host.c             #   NimBLE init + advertising
│   ├── ble_nus.c              #   Nordic UART Service → cầu VESC Tool ↔ CAN
│   ├── ble_ota.c              #   OTA firmware P4 qua BLE (app Flutter)
│   ├── ble_files.c            #   Truyền file qua BLE (app Flutter)
│   ├── ble_cadence_client.c   #   BLE central: sensor cadence (pedal assist)
│   ├── notif_bridge.c         #   Nhận notification/media từ app điện thoại
│   ├── notif_toast.c          #   Toast LVGL cho notification
│   ├── music_info_view.c      #   Tile nhạc + album art (JPEG decode cứng)
│   │
│   │── ── UI / touch ──
│   ├── touch_input.c          #   Reader GT911 DUY NHẤT; mode AA vs LVGL;
│   │                          #   gesture 3 ngón, edge-swipe
│   ├── ui_mode.c              #   Chuyển AA projection ↔ VESC dashboard
│   ├── idle_screen.c          #   Màn chờ "Waiting for phone"
│   ├── splash_screen.c        #   GIF boot (/vescfs/splash.gif)
│   ├── ota_screen.c           #   Overlay tiến trình OTA
│   ├── files_screen.c         #   File browser trên thiết bị
│   ├── debug_uart_bridge.c    #   Screenshot + inject touch qua UART0 (test UI)
│   │
│   │── ── VESC / xe ──
│   ├── vesc_ui_updater.c      #   Bơm dữ liệu VESC → widget dashboard
│   ├── vesc_sim.c             #   Giả lập VESC (không cần CAN thật)
│   ├── pas.c                  #   Pedal assist: cadence BLE → setpoint → LISP
│   ├── vbat_routing.c         #   PMU/VBAT cho đồng hồ thời gian thực
│   ├── c6_ota.c               #   Tự OTA firmware C6 khi lệch version
│   └── web/                   #   Asset HTML cho các trang HTTP
│
├── components/
│   ├── esp32_p4_wifi6_touch_lcd_4_3/  # BSP (từ Waveshare) + patch 2 board
│   ├── vesc_can/              # TWAI driver, VESC packet framing (buffer/crc/
│   │                          #   packet_parser), pollers: rt_data, io_data,
│   │                          #   lisp_poll/console/code/panel
│   ├── vesc_config/           # Đọc/ghi config VESC (serdes sinh từ XML,
│   │                          #   transport qua CAN, chọn bảng theo FW VESC)
│   ├── vesc_ui/               # Wrapper LVGL dashboard (nguồn: Super_VESC_Display)
│   ├── dev_settings/          # Settings NVS + battery calc + trip persist + app FS
│   ├── trip_log/              # Time-series log chuyến đi → partition `triplog`
│   ├── log_capture/           # Ring buffer log trong PSRAM (màn Logs)
│   ├── qr_info/               # QR code (pair/info)
│   ├── c6_ota_partition/      # Blob network_adapter.bin (C6) nhúng vào image
│   └── bt_agent_fw/           # Blob firmware BT agent (gz) nhúng vào image
│
├── Super_VESC_Display/        # Project GUI-Guider (nguồn UI dashboard)
│   ├── generated/             #   Code GUI-Guider: setup_scr_dashboard_Classic,
│   │                          #   Classic_Max, Lamborghini, Supermoto, settings…
│   ├── custom/                #   Code tay: dashboard_theme (framework theme),
│   │                          #   settings_wrapper, lisp_editor/panel,
│   │                          #   realtime_viewer, pas_screen, trip_statistics
│   └── lvgl-simulator/        #   Simulator desktop (SDL)
│
├── flutter-application/       # App Android companion (BLE): OTA firmware,
│   │                          #   notification/media bridge, file manager,
│   │                          #   quản lý helper C3 (repo riêng)
│   └── lib/{agent,ble,bridge,firmware,helper,settings,ui,…}
│
├── lisp/main.lisp             # Script LispBM chạy TRÊN VESC: arbiter motor
│                              #   (throttle/PAS), quick-action panel (giao thức
│                              #   'VP' qua COMM_CUSTOM_APP_DATA — xem lisp/README.md)
│
├── scripts/                   # build_board.sh, release.sh, ota_push.sh,
│                              #   capture*.sh (log P4/agent/phone), uart_debug.py
│                              #   (debug bridge), gen_dashboard_themes.py…
├── tools/                     # bt_agent/ (fw agent, git-ignored, tái tạo được),
│                              #   c6_slave_fw, c6_ota_flasher (git-ignored)
├── 3d-model/                  # Vỏ hộp in 3D cho cả 2 board
├── partitions.csv             # Bảng phân vùng 32 MB (Waveshare)
├── partitions_16mb.csv        # Bảng phân vùng 16 MB (JC4880)
├── sdkconfig.defaults[.board] # Base = Waveshare; overlay per-board
└── release/                   # Artifact: bin per-board + APK bundle firmware
```

---

## 3. Luồng khởi động (`main/main.c: app_main`)

Thứ tự quan trọng — nhiều bước phụ thuộc bước trước:

1. **`log_capture_init`** — ring log PSRAM trước mọi log khác.
2. **`vbat_routing_enable`** — PMU giữ đồng hồ khi mất nguồn (nếu bật).
3. **NVS + `settings_init`** → `battery_calc_init`, `trip_persist_init`,
   `trip_log_init` (mount LittleFS `/vescfs`).
4. **`aa_overclock_400mhz_apply`** — trước mọi peripheral.
5. **`display_init`** → brightness từ settings → **splash GIF** →
   `idle_screen_init` + `ota_screen_init` (z-order: idle dưới, ota trên).
6. **`install_lvgl_touch_indev`** — gỡ indev của BSP, thay bằng indev đọc từ
   `touch_input` (một reader GT911 duy nhất, tránh race I2C).
7. **`ui_mode_init`** — build dashboard GUI-Guider (~5 s, phải tạm gỡ IDLE0
   khỏi TWDT) → tắt splash → đăng ký gesture 3 ngón (`ui_mode_toggle`) và
   edge-swipe (LISP panel) → `touch_input_start`.
8. **VESC CAN**: `comm_can_start` → init các poller (`vesc_rt_data`,
   `vesc_lisp_*`, `vesc_io_data`) → `vesc_packet_dispatch` fan-out →
   `vesc_config_probe_fw` → `vesc_ui_updater_start`. (Hoặc `vesc_sim_start`
   nếu bật emulator trong settings.)
9. **`c6_ota_check_and_update`** — lệch version C6 → flash blob nhúng qua
   SDIO → restart P4.
10. **BLE**: `ble_nus_init` → `ble_host_init` (NimBLE qua VHCI của C6) →
    `notif_toast_init` → `pas_init`.
11. **Mạng** (Mode B): `wifi_manager_start` (SoftAP) → `mdns_advertise_start`
    → `tcp_server_start(5277)` → `ota_http_start` + `files_http_register` +
    `lisp_http_register`.
12. **Video**: `display_video_init` (sink) trước `h264_pipe_init` (decoder).
13. **BT agent**: `bt_link_init` (reset agent, UART lên) →
    `bt_agent_ota_check_and_update` → `bt_link_publish_wifi(ssid, psk, bssid,
    ip, port)` → sync cờ auto-reconnect.
14. `idle_screen_show("Waiting for phone", …)` + nút Connect thủ công.

> **Mode A (`MODE_BT_CLASSIC`) chưa triển khai** — `#error` trong main.c.
> Thực tế hiện tại là Mode B + BT agent ngoài lo phần Classic BT.

---

## 4. Luồng Android Auto (kết nối → video lên màn)

```
Phone ──BT──► [BT agent D1 Mini] ──UART1──► bt_link.c
  agent gửi phone SSID/PSK/IP/port (AA Wireless setup protocol)
Phone join SoftAP (C6) ──► TCP :5277 (tcp_server.c)
  └► aa_frame.c   : tách frame AA
  └► aa_tls.c     : TLS handshake (mbedTLS, cert trong aa_certs.h,
  │                 aa_x509_patch.c vá ngày UTCTime)
  └► aa_handshake : version negotiation
  └► aa_service.c : service discovery, mở channel
        ├─ VideoChannel  → h264_pipe.c (ring NAL → esp_h264 SW decode → I420)
        │                   → display_video.c (PPA upscale → blit panel)
        ├─ InputChannel  ← touch_input.c (TOUCH_MODE_AA: toạ độ → protobuf)
        ├─ AudioChannel / SensorChannel …
        └─ aa_overlay.c : overlay LVGL đè lên video khi cần
```

Chuyển chế độ hiển thị: **gesture 3 ngón** → `ui_mode.c` đổi
`TOUCH_MODE_AA ↔ TOUCH_MODE_LVGL`; ở mode AA, indev LVGL luôn thấy
`released` nên dashboard không nhận touch, và ngược lại.

---

## 5. Luồng VESC dashboard

```
VESC ◄──CAN/TWAI──► components/vesc_can
  comm_can.c        : driver TWAI + framing (buffer/crc/packet_parser)
  vesc_rt_data.c    : poll COMM_GET_VALUES định kỳ (task CAN duy nhất)
  vesc_io_data.c    : ADC/PPM (chỉ khi realtime viewer mở)
  vesc_lisp_poll/console/code/panel : stats, print, upload script, quick panel
        │ mọi reply CAN gom về main.c:vesc_packet_dispatch() fan-out:
        ├─► vesc_rt_data ──► vesc_ui_updater.c ──► widget LVGL dashboard
        ├─► vesc_config_transport (đọc/ghi config VESC, bảng theo FW)
        ├─► vesc_lisp_* (editor web /lisp, console, quick panel 'VP')
        └─► ble_nus_forward_response (VESC Tool qua BLE thấy reply CAN)
```

- **UI dashboard**: sinh bởi GUI-Guider (`Super_VESC_Display/generated/`),
  framework **theme** trong `custom/dashboard_theme.c` — 4 theme (Classic,
  Classic Max, Lamborghini, Supermoto), đổi live trong Settings; hook trong
  main.c re-attach tile nhạc khi đổi theme.
- **Trip**: `trip_persist` (NVS, qua reboot) + `trip_log` (partition riêng,
  pre-erase lúc boot để không erase giữa chuyến — erase làm khựng DSI).
- **PAS**: `ble_cadence_client` (BLE central) → `pas.c` tính setpoint →
  gửi cho **arbiter trong `lisp/main.lisp`** chạy trên VESC.
- **LVGL v8.4, RGB565**, config duy nhất tại `main/lv_conf.h`
  (`LV_CONF_SKIP=n` → Kconfig `CONFIG_LV_*` bị bỏ qua). Panel gốc 480×800
  portrait, xoay 90° → 800×480.

---

## 6. Luồng BLE & app Flutter

C6 cấp cả Wi-Fi lẫn **BT controller**; P4 chạy NimBLE host qua VHCI/ESP-Hosted.

| GATT service | File | Chức năng |
|---|---|---|
| NUS | `ble_nus.c` | VESC Tool trên điện thoại ↔ CAN bridge |
| OTA | `ble_ota.c` | App Flutter đẩy firmware P4 (chọn theo `BOARD_MODEL_ID`) |
| Files | `ble_files.c` | App duyệt/tải file `/vescfs` + SD |
| Notif | `notif_bridge.c` | App đẩy notification + media/album-art → toast + music tile |

App Flutter (`flutter-application/`) bundle sẵn firmware **cả 2 board**
(nhúng lúc release), đọc `BOARD_MODEL_ID` từ OTA-info/`GET /info` để chọn
đúng bin; đồng thời quản lý firmware helper C3 (repo riêng, tải từ GitHub
releases của helper).

---

## 7. Ba tầng OTA

| Tầng | Cơ chế | File |
|---|---|---|
| **P4** | HTTP `/ota` (`ota_push.sh` qua SoftAP) hoặc BLE OTA từ app; 2 slot ota_0/ota_1 | `ota_http.c`, `ble_ota.c` |
| **C6** | Blob `network_adapter.bin` nhúng trong image P4; boot so version → OTA qua SDIO → restart | `c6_ota.c`, `components/c6_ota_partition/` |
| **BT agent** | Blob `bt_agent_fw.bin.gz` nhúng; boot so `BT-VER:` qua UART → reflash bằng RST/IO0 | `bt_agent_ota.c`, `components/bt_agent_fw/` |

→ Chỉ cần flash/OTA P4, hai firmware phụ tự đồng bộ theo.

---

## 8. Build, debug, release

```bash
. "$IDF_PATH/export.sh"
idf.py build                                  # = Waveshare (build/)
scripts/build_board.sh waveshare flash monitor
scripts/build_board.sh jc4880 -p <PORT> flash # build_jc4880/, overlay sdkconfig
scripts/build_board.sh                        # build TẤT CẢ board
scripts/ota_push.sh                           # OTA Wi-Fi → 192.168.4.1
scripts/release.sh                            # bump version, build all, APK bundle
```

- **Debug**: `scripts/capture*.sh` (serial P4 / BT agent / logcat phone),
  `scripts/uart_debug.py` + `CONFIG_DEBUG_UART_BRIDGE` (chụp màn hình +
  inject touch từ máy tính), màn Logs trong Settings (`log_capture`).
- **Versioning**: firmware `1.x.y` (`version.txt`) + app `0.x.y`
  (`pubspec.yaml`) bump cùng nhau bởi `release.sh`; BT agent version riêng.
- `tools/` và `research/_sources/` **không commit** — cách tái tạo trong
  `CLAUDE.md` (mục "Воспроизведение игнорируемых артефактов").

---

## 9. Skill hỗ trợ (cho Claude Code, trong `.claude/skills/`)

`head-unit` (bản đồ project) · `build-flash` · `capture-logs` ·
`device-screen` (screenshot/touch trên phần cứng thật) · `dashboard-ui`
(pitfall LVGL) · `release`.
