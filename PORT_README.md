# Nhánh `esp32s3-port` — trạng thái và cách dùng

Nhánh này là **điểm xuất phát** để port firmware sang ESP32-S3 / ESP32, đã bỏ
toàn bộ Android Auto. Nó **chưa build được** — xem §3.

Kế hoạch đầy đủ (kiểm kê tính năng, ngân sách flash/RAM, rủi ro fps, thứ tự
làm việc): [`docs/ESP32_S3_PORT_PLAN.md`](docs/ESP32_S3_PORT_PLAN.md).

---

## 1. Lịch sử nhánh

Hai commit, cắt từ `develop` tại `8230e83`:

1. `chore: snapshot uncommitted work from the shared develop tree`
   — `develop` tại thời điểm đó có phần ride-mode UI và BMS view chưa commit.
   Quan trọng: `components/vesc_can/vesc_ride_mode.c` đã được commit nhưng
   header `vesc_ride_mode.h` thì **chưa**, nên bản thân `HEAD` của `develop`
   không compile được. Commit này chép nguyên trạng cây làm việc để nhánh có
   điểm xuất phát nhất quán. Cây `develop` không bị đụng đến.
2. `feat: strip Android Auto` — phần trích xuất mô tả bên dưới.

---

## 2. Đã bỏ những gì

**Nguồn Android Auto** (`main/`): `aa_certs.h`, `aa_frame`, `aa_handshake`,
`aa_overlay`, `aa_proto`, `aa_service`, `aa_tls`, `aa_x509_patch`,
`aa_overclock`, `h264_pipe`, `display_video`, `tcp_server`, `mdns_advertise`.

**Co-processor** (P4 không có radio; S3/ESP32 có sẵn): `bt_link`,
`bt_agent_ota`, `c6_ota`, `components/bt_agent_fw/` (blob 572 KB),
`components/c6_ota_partition/` (blob 700 KB). Cùng với đó là các dependency
`esp_hosted`, `esp_wifi_remote`, `esp_h264`, `esp_image_effects`,
`esp-serial-flasher`, và ba dòng `--wrap=mbedtls_x509_get_time` trong
`main/CMakeLists.txt`.

**Chỉ có trên P4**: `vbat_routing` (định tuyến LP domain sang CR2032 qua PMU —
S3/ESP32 không có chân VBAT cho RTC domain).

**Không còn ý nghĩa**: `ui_mode` (chỉ tồn tại để lật giữa dashboard và màn
chiếu AA; giờ dashboard là màn duy nhất), `release/` (binary P4).

**Chuyển sang `reference/`, giữ để tham chiếu, ngoài đường build**: BSP P4
(`reference/bsp_p4/`), `sdkconfig.defaults.p4`, hai overlay board P4, hai
partition table P4.

## Đã đổi những gì

- `main/main.c` — bỏ hết nhánh AA / bt_link / c6_ota; thêm `dashboard_init()`
  thay cho `ui_mode_init()`. Wi-Fi chuyển xuống cuối `app_main` vì mọi thứ
  khác chạy được mà không cần nó.
- `main/CMakeLists.txt`, `main/idf_component.yml` — theo phần đã bỏ ở trên.
- `main/Kconfig.projbuild` — bỏ menu BT-agent OTA và `AA_OVERCLOCK_400`; đổi
  tiền tố `CONFIG_AA_WIFI_*` / `CONFIG_AA_AP_*` thành `CONFIG_WIFI_*`
  (`main/wifi_manager.c` đã đổi theo); `choice BOARD_MODEL` thay bằng ba
  target trong kế hoạch — **tên board là placeholder**.
- `main/config.h` — còn `ENABLE_WALL_CLOCK` và `DEVICE_MDNS_HOSTNAME`
  (`components/qr_info/qr_info.c` đổi theo).
- `sdkconfig.defaults` mới cho `esp32s3`; `partitions_s3_16mb.csv` mới
  (OTA 3 MB ×2, storage 1 MB, triplog phần còn lại).
- `CMakeLists.txt` gốc — đổi tên project thành `esp32_vesc_display`.

Toàn bộ `components/vesc_can`, `vesc_config`, `vesc_ui`, `bms`, `dev_settings`,
`log_capture`, `trip_log`, `qr_info` và `Super_VESC_Display/` **không đụng
đến** — không file nào trong đó chạm phần cứng P4.

---

## 3. Vì sao chưa build được, và làm gì tiếp

Ba việc, đúng thứ tự:

### 3.1 Viết BSP — chặn cứng, phải làm trước

`components/bsp_board/` hiện là component rỗng. `main/` sẽ dừng ngay ở
`#include "bsp/esp-bsp.h"`. Hợp đồng API (12 hàm) nằm ở
[`components/bsp_board/README.md`](components/bsp_board/README.md), bản P4 làm
mẫu ở `reference/bsp_p4/`.

Trước khi viết cần chốt: IC driver và giao tiếp của panel, loại touch
controller, và **những chân GPIO nào còn trống cho TWAI TX/RX** — panel RGB
16-bit cộng PSRAM octal ăn gần hết GPIO của S3. `CONFIG_VESC_CAN_TX_GPIO` /
`RX_GPIO` trong `sdkconfig.defaults` đang để 43/44 làm placeholder.

### 3.2 Hai file chưa được compile

Đã comment khỏi `main/CMakeLists.txt` vì phụ thuộc ngoại vi chỉ P4 mới có:

- `main/splash_screen.c` — dùng `driver/ppa.h` để xoay sẵn khung hình GIF.
  Panel S3 vốn landscape nên chỉ cần bỏ phần xoay là dùng lại được.
- `main/debug_uart_bridge.c` — dùng `driver/jpeg_encode.h` (JPEG phần cứng của
  P4). S3 không có; chính file này đã sẵn có đường raw RGB565, chuyển sang đó
  hoặc link một encoder phần mềm.

### 3.3 `main/touch_input.c` giữ nguyên có chủ ý

Còn nguyên `TOUCH_MODE_AA`, `touch_send_fn` và gesture 3 ngón — nay là code
chết (`main.c` gọi `touch_input_start(NULL, NULL)` và không bao giờ đặt sang
mode AA). Cố tình **không** dọn ở nhánh này: cùng file đó chứa phép ánh xạ toạ
độ xoay 90° vốn phải viết lại theo panel S3, nên dọn AA và sửa xoay là **một**
việc, làm cùng lúc ở bước BSP. Dọn trước khi biết panel là sửa mù.

### 3.4 mDNS

`mdns_advertise.c` bị bỏ cùng AA (nó chỉ quảng bá `_aawireless._tcp`). Vì vậy
`<host>.local` không phân giải được và URL trên màn QR chỉ dùng được qua IP.
Đăng ký lại một responder mDNS đơn giản nếu muốn `vesc-display.local`.

---

## 4. Lưu ý khi clone

- Tài liệu gốc của dự án (`CLAUDE.md`, `AGENTS.md`, `docs/ARCHITECTURE.md`,
  `README*.md`) **vẫn mô tả bản ESP32-P4 + Android Auto**. Chưa viết lại —
  đọc chúng như tài liệu lịch sử cho tới khi port xong.
- `scripts/build_board.sh` và `scripts/release.sh` vẫn liệt kê board
  `waveshare` / `jc4880`. Cập nhật `BOARDS=()` ở cả hai khi board thật đã chốt.
- `flutter-application/` giữ nguyên: BLE OTA, quản lý file và lint LispBM
  không phụ thuộc chip. Nhưng asset firmware nhúng trong APK đang đặt tên theo
  slug board P4 — xem `main/board.h`.
- `dependencies.lock` đã xoá; lần `idf.py reconfigure` đầu tiên sẽ sinh lại.
