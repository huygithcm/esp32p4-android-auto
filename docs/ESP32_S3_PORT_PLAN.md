# Kế hoạch tách bản không-Android-Auto sang ESP32 / ESP32-S3

Tài liệu này mô tả cách trích xuất toàn bộ tính năng của firmware hiện tại
**trừ Android Auto** sang một repo mới chạy trên ESP32-S3 (đầy đủ) và ESP32
classic (rút gọn). Số liệu lấy từ `build_jc4880/esp32p4_android_auto.map`
(image 4.429.456 B) và từ chính cây nguồn ở thời điểm viết.

---

## 0. Kết luận ngắn

| Tier | Chip | Panel | UI | Tính năng |
|---|---|---|---|---|
| **A** | ESP32-S3 (8 MB PSRAM octal, 16 MB flash) | 4.3" / 7" RGB 800×480 | **giữ nguyên 1:1** | ~100% (trừ AA) |
| **B** | ESP32-S3 | 2.8" (320×240 hoặc 480×320, SPI/QSPI) | layout compact mới | ~80% |
| **C** | ESP32 classic (PSRAM 4 MB, ≥8 MB flash) | 2.8" SPI | layout compact (dùng chung tier B) | ~50%, xem §6 |

Tier A là port thẳng: panel S3 vốn landscape nên **bỏ được cả đường
ROTATE_90/PPA** vốn là nguồn phức tạp nhất của bản P4. Tier B/C là làm UI mới,
vì UI hiện tại có ~1.000 lời gọi toạ độ tuyệt đối viết cho 800×480.

---

## 1. Kiểm kê tính năng giữ lại

### VESC / CAN — `components/vesc_can/`
`comm_can` (TWAI + reassembly packet VESC + identity cho CAN-scan của VESC Tool),
`vesc_rt_data` (telemetry RT), `vesc_io_data` (ADC/PPM), `vesc_lisp_poll`,
`vesc_lisp_console`, `vesc_lisp_code`, `vesc_lisp_panel`, `vesc_ride_mode`
(ride mode + reverse), `vesc_sim` (emulator, chạy không cần CAN).

### Config VESC — `components/vesc_config/`
Bảng sinh offline cho fw 6.05 / 6.06 / 7.00, serdes có signature crc32c,
menu chỉnh VESC ngay trên máy (`vesc_tool_menu.c`, 1.591 dòng).

### UI LVGL — `Super_VESC_Display/`
4 theme dashboard (Classic, Classic_Max, Lamborghini, Supermoto) + framework
theme; Settings; Trip statistics; BMS view; PAS; Realtime viewer; LISP editor +
LISP quick panel; Ride mode; Files; Logs; QR; Music tile; Notification toast;
Idle / OTA / Splash.

### BLE
NUS bridge cho VESC Tool, JK-BMS client, cadence client (PAS), notification /
media bridge từ điện thoại, BLE OTA, BLE file transfer. NimBLE, 3 kết nối.

### Wi-Fi / HTTP
SoftAP + STA, OTA HTTP, web file manager `/files`, web LISP editor `/lisp`
(HTML gzip nhúng .rodata), mDNS (chỉ còn để quảng bá web UI).

### Lưu trữ
LittleFS `/vescfs`, partition `triplog` raw circular, NVS settings, thẻ SD,
log ring trong PSRAM.

### Khác
battery calc, trip persist, PAS control task, debug UART bridge (screenshot +
inject touch).

---

## 2. Manifest: giữ / port / bỏ

### `main/` — BỎ HẲN (30 file)

```
aa_certs.h  aa_frame.*  aa_handshake.*  aa_overlay.*  aa_proto.*
aa_service.*  aa_tls.*  aa_x509_patch.c  aa_overclock.*
h264_pipe.*  display_video.*  tcp_server.*
bt_link.*  bt_agent_ota.*  c6_ota.*  vbat_routing.*  ui_mode.*
mdns_advertise.*   (giữ lại nếu vẫn muốn mDNS quảng bá web UI)
```

- `aa_overclock` — S3 tối đa 240 MHz.
- `vbat_routing` — PMU LP + CR2032 là đặc thù P4. S3/ESP32 không có chân VBAT;
  wall-clock sống qua mất điện cần DS3231 ngoài (hiện `ENABLE_WALL_CLOCK` = 0
  nên chưa ảnh hưởng gì).
- `ui_mode.c` (~100 dòng) chỉ tồn tại để chuyển VESC ↔ AA → xoá, dashboard trở
  thành màn duy nhất.

### `main/` — GIỮ, SỬA

| File | Việc phải làm |
|---|---|
| `main.c` | Cắt nhánh `CONNECTION_MODE`, bỏ khối AA / bt_link / c6_ota. Còn ~380/667 dòng. |
| `ble_host.c` | Đổi `esp_hosted_bt_controller_init/enable` (dòng 255-266) → `esp_bt_controller_init/enable` + `nimble_port_init` native. ~20 dòng. |
| `wifi_manager.c` | Bỏ `esp_wifi_remote`, dùng `esp_wifi` native; bỏ logic chờ C6 sẵn sàng. |
| `touch_input.c` | Xoá nhánh `TOUCH_MODE_AA` và gesture 3 ngón. Còn ~250/387 dòng. |
| `display_init.c` | Viết lại phần bring-up: bỏ `ESP_LV_ADAPTER_ROTATE_90`, bỏ hook PPA `draw_ctx_init`, bỏ tear-avoid `DOUBLE_DIRECT` của DSI. Giữ nguyên `monitor_cb`, scrub timer và perf log — chúng còn quý hơn trên S3. |
| `idle_screen.c` | Đổi nội dung ("Waiting for phone" → trạng thái CAN / Wi-Fi) hoặc bỏ. |
| `splash_screen.c` | Bỏ đường PPA flip, giữ LVGL GIF. |

### `main/` — GIỮ NGUYÊN

```
ble_nus  ble_bms_client  ble_cadence_client  ble_files  ble_ota
notif_bridge  notif_toast  music_info_view  pas
files_http  files_screen  lisp_http  ota_http  ota_screen
debug_uart_bridge  vesc_sim  vesc_ui_updater
lv_conf.h  board.h  fonts/aabridge_font_*.c
```

> `fonts/aabridge_font_*.c` (~128 KB) **không phải của Android Auto** dù mang
> tên đó — `notif_toast.c` và `music_info_view.c` dùng chúng để render text
> Unicode của notification. Phải mang theo.

### `components/` — BỎ

`bt_agent_fw/` (blob 572 KB), `c6_ota_partition/` (blob 700 KB),
`esp32_p4_wifi6_touch_lcd_4_3/` (thay bằng BSP mới).

### `components/` — GIỮ NGUYÊN

`vesc_can/  vesc_config/  vesc_ui/  bms/  dev_settings/  log_capture/
trip_log/  qr_info/` — không file nào trong nhóm này chạm phần cứng P4.

### Dependency trong `idf_component.yml` — BỎ

`espressif/esp_hosted` · `espressif/esp_wifi_remote` · `espressif/esp_h264` ·
`espressif/esp_image_effects` · `espressif/esp-serial-flasher` ·
`espressif/esp_new_jpeg`

Bỏ luôn 3 dòng `--wrap=mbedtls_x509_get_time` ở cuối `main/CMakeLists.txt`.

### `Super_VESC_Display/` — giữ toàn bộ cho tier A. Tier B/C xem §5.

---

## 3. Ngân sách flash / RAM

### Flash — đo từ map hiện tại

| Nhóm | KB |
|---|---|
| Blob C6 + BT agent + serial-flasher | 1349 (bỏ) |
| esp-hosted + esp_wifi_remote | 435 (bỏ) |
| AA-only (codec + main + cert) | 275 (bỏ) |
| mbedtls | 268 (phần lớn bỏ, giữ phần OTA/HTTPS nếu cần) |
| **VESC app + UI** | **696** (giữ) |
| **LVGL** | **528** (giữ) |
| IDF nền | ~800-900 |

→ Image tier A ước **2,2–2,5 MB**. Với flash 16 MB: `ota_0` / `ota_1` 3 MB,
`storage` 1 MB, `triplog` phần còn lại.
→ Image tier C (ít font, UI nhỏ) ước **1,3–1,6 MB** → module 8 MB là đủ;
4 MB thì phải bỏ OTA kép.

### RAM nội — chỗ chật nhất của S3 (512 KB)

| Thành phần | KB nội |
|---|---|
| Wi-Fi (AP + STA) | 50-60 |
| BLE controller (buffer bắt buộc ở RAM nội) | 55-65 |
| NimBLE host | đẩy được sang PSRAM (`BT_NIMBLE_MEM_ALLOC_MODE_EXTERNAL`) |
| lwIP + HTTP server | 40-50 |
| LVGL draw buffer | 2 × (800×40×2) = 128 KB nếu để RAM nội |
| LittleFS + SD + task stack | 40-60 |

Khả thi, nhưng bắt buộc đẩy LVGL heap và NimBLE host sang PSRAM. `lv_conf.h`
hiện đã `LV_MEM_CUSTOM` → `MALLOC_CAP_SPIRAM`, giữ nguyên là đúng.

### Framebuffer

800×480 RGB565 = **768.000 B** mỗi buffer. Double buffer trong PSRAM = 1,5 MB
trên 8 MB — thoải mái về dung lượng, vấn đề là băng thông (§4).

---

## 4. Rủi ro lớn nhất: tốc độ vẽ trên S3

`main/display_init.c:167-180` ghi lại số đo thật trên P4: full-refresh
**~51 ms/frame (~19 fps)** dù *có* PPA và CPU 400 MHz. S3 chạy 240 MHz,
**không có PPA/DMA2D**, framebuffer nằm PSRAM → dự kiến **10–20 fps** ở
dashboard phức tạp.

Biện pháp bắt buộc, theo thứ tự ưu tiên:

1. **Không dùng `full_refresh`.** Cấu hình LVGL partial/direct để chỉ vẽ vùng
   bẩn. Bản P4 đã phải chuyển sang `DOUBLE_DIRECT` đúng vì lý do này.
2. **Draw buffer đặt trong SRAM nội**, flush sang framebuffer PSRAM — tránh để
   renderer phần mềm ghi thẳng vào PSRAM (thủ phạm chính của 51 ms).
3. **Bounce buffer cho RGB panel** (`bounce_buffer_size_px`) — gần như bắt
   buộc khi framebuffer nằm PSRAM trên S3, nếu không sẽ xé / trôi hình.
4. Giảm animation trên dashboard; font Antonio 200 px chỉ vẽ lại khi số đổi.

Đây là rủi ro cần đóng **trước** khi port nốt phần còn lại — xem bước 3 ở §7.

---

## 5. Lớp trừu tượng cần viết mới

### 5.1 BSP — nhỏ hơn tưởng

Toàn bộ code app chỉ gọi **12 hàm BSP** thật sự:
`bsp_display_lock/unlock` (27 lần), `bsp_display_brightness_set/init/get`,
`bsp_display_backlight_on/off`, `bsp_display_start_with_config`,
`bsp_display_get_panel_handle`, `bsp_display_get_touch_handle`,
`bsp_sdcard_mount/unmount`, `bsp_i2c_init`.

→ Mỗi board một BSP ~600-900 dòng, cùng chung header `bsp/esp-bsp.h`:

```
components/bsp_s3_rgb_800x480/     tier A (4.3" và 7")
components/bsp_s3_qspi_small/      tier B (2.8")
components/bsp_esp32_spi_small/    tier C (2.8" SPI)
```

### 5.2 `ui_metrics.h` — thay số cứng

Điều kiện để tier B/C tái dùng được code UI. Thay
`lv_obj_set_size(s_screen, 800, 480)` bằng `UI_SCREEN_W` / `UI_SCREEN_H`, và
gom font theo *vai trò* thay vì theo px:

```c
#define UI_FONT_BODY    (&lv_font_montserratMedium_16)  /* tier A */
#define UI_FONT_GAUGE   (&lv_font_Antonio_Regular_200)
```

Có 169 chỗ dùng `montserratMedium_16` và 79 chỗ `montserrat_24` — sửa bằng
`sed` được, không phải gõ tay.

### 5.3 Tier B/C: dashboard riêng

4 file `setup_scr_dashboard_*.c` (tổng 7.693 dòng, 668 lời gọi toạ độ tuyệt
đối) **không scale được**. Tier B/C cần một dashboard compact viết mới
(~400 dòng), tái dùng nguyên `dashboard_theme.c` dispatcher và toàn bộ backend
phía dưới.

---

## 6. Tier C (ESP32 classic) — tập tính năng đề nghị

ESP32 classic dùng chung radio cho Wi-Fi và BT, chỉ có BLE 4.2, PSRAM quad
40 MHz dùng chung bus với flash. Đề nghị cắt:

| Giữ | Bỏ |
|---|---|
| CAN + telemetry + dashboard compact | BMS BLE client |
| BLE NUS (VESC Tool) | notification / media bridge |
| Settings + trip persist | web file manager `/files` |
| OTA (BLE + HTTP) | web LISP editor `/lisp` |
| PAS | splash GIF + PNG decoder |
| trip_log | realtime viewer, LISP editor on-device |

Chạy đồng thời SoftAP + 3 kết nối BLE + HTTP server + LVGL trên 520 KB SRAM
nội là không thực tế.

---

## 7. Thứ tự làm việc

1. **Dựng repo + skeleton build.** Copy nhóm "giữ nguyên" ở §2, BSP để stub,
   `main.c` chỉ init NVS + settings. *Mốc: build sạch cho target `esp32s3`.*
2. **BSP tier A + display.** RGB 800×480, GT911, backlight, SD.
   *Mốc: LVGL vẽ được `lv_label` và nhận touch.*
3. **CAN + dashboard.** Bật `comm_can` + `vesc_rt_data` + `vesc_ui_updater`,
   build nguyên `Super_VESC_Display`. *Mốc: kim đồng hồ chạy theo VESC thật
   (hoặc `settings_get_vesc_emulator()` để test không cần xe).*
   → **Đo fps ngay ở bước này.** Dưới 10 fps thì quay lại §4 trước khi đi tiếp.
4. **Wi-Fi + BLE native.** `ble_host.c` + `wifi_manager.c`.
   *Mốc: VESC Tool kết nối được qua BLE NUS; mở được `/files` từ trình duyệt.*
5. **Phần còn lại.** LittleFS, trip_log, OTA, notif bridge, BMS, PAS.
6. **Tier B/C.** `ui_metrics.h` + dashboard compact + 2 BSP nhỏ.
7. **Partition + release.** `partitions_s3_16mb.csv`, script build đa board
   theo mẫu `scripts/build_board.sh`.

Bước 1-5 cho ra tier A hoàn chỉnh. Bước 6 là khối lượng lớn thứ hai.

---

## 8. Cần chốt trước bước 2

Ba màn hình 2.8" / 4.3" / 7" cần biết chính xác:

- **IC driver + giao tiếp.** 4.3" là RGB 800×480 hay SPI 480×272? 7" gần như
  chắc chắn RGB 800×480. 2.8" là ILI9341 320×240 SPI hay ST7789?
- **Board tích hợp hay panel rời.** Nếu là board Sunton / Guition thì cần sơ đồ
  chân để biết còn dư chân cho TWAI TX/RX — panel RGB16 ăn ~20 chân, PSRAM
  octal ăn thêm. Đây là ràng buộc phần cứng thật, không phải chuyện code.
- **Touch.** GT911 (dùng lại code hiện tại được ngay) hay FT6336 / XPT2046
  (phải viết driver mới).
