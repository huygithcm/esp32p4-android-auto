# ESP32-S3 VESC dashboard + BLE BMS — source seed

Nhánh nguồn tối giản để port sang ESP32-S3, tách từ commit
`7c77f2b3595fcc0258e26d2049210c4e507bf8d8` của dự án P4.
**Chưa phải firmware S3 build/flash được.** Không mang bootloader, partition,
pin map hoặc sdkconfig P4 sang S3. CMake chủ động dừng để tránh build nhầm.

## Giữ lại

- `Super_VESC_Display/custom`, `generated`: dashboard, setting dòng mỗi mode,
  vòng tròn 1/2/3/R/P, BMS overview/cell/wire và các nguồn UI phụ thuộc.
- `components/vesc_can`, `vesc_config`: CAN, telemetry, config ESC, safety
  sequence/timeout/P/R; `components/bms`: model và parser JK BMS.
- `components/vesc_ui`, `dev_settings`, `trip_log`, `log_capture`, `qr_info`:
  các phụ thuộc hiện có của UI; không có BSP P4 hoặc blob firmware phụ.
- `main/ble_bms_client.*`: scan/chọn/connect và nhận BMS qua NimBLE;
  cadence/PAS được giữ vì UI có phụ thuộc, có thể tách thành tùy chọn khi port.
- `main/vesc_ui_updater.*`, `ride_gear_state.h`, các header giao tiếp UI:
  nguồn cần sửa cho integration mới, không phải app entry point S3.
- `lisp/main.lisp`: script chạy trên **ESC VESC**, không chạy trên ESP32-S3.
- `scripts/tests`, `scripts/test_lisp_safety.py`: host regression tests.
- Font nguồn/subset tool và generated assets C cần cho UI được giữ;
  không giữ GUI project backup, thư viện SDK hoặc simulator nhị phân.

## Đã loại khỏi snapshot

Android Auto (protocol/TLS/cert/video/H264/audio/overlay), Flutter companion,
BT Classic helper, firmware C6/ESP-Hosted, BSP P4/DSI/PPA/JPEG, release/build
binary, managed_components, vendor LVGL/SDL, lịch sử nghiên cứu, log máy,
IDE settings, ảnh mẫu, script melody, Lisp tiếng Nga cũ có cruise/beep.

Các tên widget/comment cruise/music/QR cũ vẫn có thể xuất hiện trong UI được
tái sử dụng; không có Android Auto runtime trong snapshot. Việc xóa nốt UI
không dùng và code beep persistence chết là task port, không được coi đã xong.

## Việc cần làm trước khi build S3

1. Chốt board S3, dung lượng flash/PSRAM, LCD (SPI/RGB), touch, độ phân giải,
   backlight và chân TWAI + CAN transceiver. UI nguồn hiện thiết kế 800x480.
2. Tạo main entry point, CMake/component manifest, partition và sdkconfig S3.
   Dùng LVGL 8.4 tương thích nguồn UI; không tái sử dụng cấu hình P4 nguyên xi.
3. Viết `ble_host.c` cho NimBLE tích hợp S3, không ESP-Hosted. Header cũ chỉ là
   contract cần rà lại. Bind các callback GAP vào `ble_bms_client` và quản lý
   scan/connect; kiểm tra số BLE slot và tránh cadence tranh slot với BMS.
4. Viết display/touch driver và LVGL lock, thay BSP và `ui_mode` từ updater.
   Header `notif_bridge.h` là contract cũ; bỏ các call notification/navigation
   trong updater nếu không giữ tính năng này, không khôi phục Android Auto.
5. Nối NVS, CAN target, poll task, BMS UI backend, freshness và P/R. Rà default
   chân CAN trong Kconfig, không dùng giá trị P4 để đấu phần cứng S3.
6. QR info hiện là phụ thuộc của Settings; bỏ trang đó hoặc cấu hình Wi-Fi/BLE
   phù hợp S3. Không triển khai lại AA discovery/handshake.
7. Chạy host tests, build rồi bench có inhibit; không chạy xe chỉ vì test host
   pass. ESC phải dùng Lisp mới, kiểm tra native ADC NONE và timeout thực tế.

## Lấy vào folder khác

```sh
git clone --single-branch --depth 1 --branch port/esp32s3-vesc-bms https://github.com/huygithcm/esp32p4-android-auto.git esp32s3-vesc-bms
```

Nhánh có một root commit độc lập để không kéo lịch sử binary cũ.
Không merge nhánh này trở lại P4; làm việc trên clone S3 riêng.

## Host test

```sh
python scripts/test_lisp_safety.py
gcc -std=c11 -I scripts/tests/ride_mocks -I components/vesc_can/include scripts/tests/test_ride_transport.c components/vesc_can/vesc_ride_mode_parse.c components/vesc_can/buffer.c -lm -o test_ride_transport
gcc -std=c11 -I components/vesc_can/include scripts/tests/test_ride_safety.c components/vesc_can/vesc_ride_mode_parse.c components/vesc_can/buffer.c -lm -o test_ride_safety
gcc -std=c11 -I components/vesc_can/include scripts/tests/test_ride_gear.c -o test_ride_gear
```

Chạy các executable test sau khi compile. Đây không phải test LispBM runtime,
FreeRTOS scheduler, BLE radio hoặc an toàn motor trên hardware.
