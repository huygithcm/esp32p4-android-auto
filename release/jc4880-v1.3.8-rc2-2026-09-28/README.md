# JC4880 v1.3.8-rc2 — 2026-09-28

Bản thử nghiệm hardware sau khi sửa các lỗi đã tái hiện. Target **Guition
JC4880P443C, ESP32-P4, flash16MB, DIO80MHz**, silicon revision1.0–1.99.
Không dùng image này cho Waveshare hoặc ESP32-S3.

## File sử dụng

| File | Mục đích |
|---|---|
| [esp32p4-jc4880-v1.3.8-rc2-ota.bin](./esp32p4-jc4880-v1.3.8-rc2-ota.bin) | OTA ứng dụng P4 trên thiết bị đang có đúng layout16MB; ưu tiên để giữ settings |
| [merged.bin](./merged.bin) | Image USB offset0x0, gồm boot/partition/OTA/app; padding đi qua NVS, cần backup settings |
| [main.lisp](./main.lisp) | Nạp riêng lên ESC6.05; không nằm trong imageP4 |
| [TEST_CASES.md](./TEST_CASES.md) | Các bước kiểm và bảng feedback hardware |
| [release.json](./release.json) | Version, source/build identity, kết quả test, giới hạn |
| [SHA256SUMS.txt](./SHA256SUMS.txt) | Checksum để đối chiếu đúng gói |
| [source-overlay.zip](./source-overlay.zip) | Source/test thay đổi trên base commit ghi trong manifest, kèm sdkconfig hiệu lực |
| [test-results/](./test-results/) | Log baseline, sau sửa, build và kiểm image |

Không đưa merged.bin vào OTA. Các test ga/PARK chỉ thực hiện trên bàn thử
ngăn lực kéo ngoài ý muốn. Chưa flash hoặc kiểm chứng hoạt động hardware.

## Thay đổi

- CAN kiểm đủ fragment liên tục và độ dài trước CRC/dispatch, không ghép bằng
  byte đuôi cũ khi thiếu mảnh. Kiểm DLC control/STATUS và bỏ frameRTR.
- Dashboard xác minh motorID trong SETUP, yêu cầu bit17 khi poll. Tương thích
  motor2 của VESC6.05 trả envelopeID gốc; Tool xem motor khác không ghi đè
  nhiệt dashboard. Bridge vẫn chuyển reply của các node cho Tool.
- Legacy47 và selective51 thiếu trường bị loại trước khi thay snapshot/tuổi.
  Gói chỉ có current không làm nhiệt cũ thành mới; tuổi FET và motor độc lập.
- Dashboard giữ giá trị nhiệt cuối khi quá hạn theo hành vi trước đây;
  realtime viewer đánh dấu riêng trường nhiệt không có dữ liệu mới. Trip
  history và lưu trạng thái pin tiếp tục hoạt động như trước, không bị khóa
  theo nhiệt; log lịch sử vẫn là snapshot các giá trị gần nhất.
- Giữ các sửaRC1: drawer chờ nhả lượt vuốt mở, chống overlay khi mở trong lúc
  đóng; Lisp poll báo đúng safety/input fault thay vìOK khi mode bị khóa.

Lisp trongRC2 giốngRC1 về byte, không thay luật ga/PARK hoặc tự ghi cấu hìnhESC.
Đã chạy nguyên script trong LispBM6.05/7.00; mỗi phiên bản27/27 checks.
6.05 còn đạt27/27 với heap2464, auxiliary memory18KiB, GC160 theo nguồn
firmware. ESC API vẫn là fixture trên host; không phải xác minh driver/timing
hoặc lực kéo trên thiết bị.

## Kết quả cuối

| Bộ kiểm tra | Kết quả |
|---|---:|
| Ghép gói CAN, DLC, nguồn/motorID | 22/22 |
| Luồng nhiệt/UI/realtime viewer | 9/9 |
| Decoder telemetry | 16/16 |
| Drawer production + LVGL | 9/9 |
| Nhánh safety Lisp | 17/17 |
| LispBM6.05, heap2464 | 27/27 |
| LispBM7.00 | 27/27 |
| Chuyển đổi nhiệt6.05 | 603 checks,0fail |

Ride safety2532, transport53, gear81 và target polling cũng đạt. Baseline
CAN78356d7 đạt10/22; baseline7c77f2b thermalUI5/9, decoder9/16: các case lỗi
đã thất bại trước sửa. Logs ghi rõ nguồn và giới hạn fixture.

Final build exit0; app **4.479.264byte**, còn **763.616byte** trong OTA slot
5.242.880byte. Image version1.3.8-rc2, checksum/hash hợp lệ. Rà soát độc lập
không còn blocker sau khi sửa identity dual-motor và giữ nguyên trip logging.

## M-TEMP và mode còn cần kiểm trên ESC

FirmwareESC6.05 đã được người dùng xác nhận; model board, Sensor Type và
ADC Control Type đang lưu chưa xác định. Không có cảm biến không đồng nghĩa
Disabled: default chung của6.05 làNTC10k/beta3380, board có thể override.

Tái hiện công thức6.05 đạt603checks: đầu vào nhiệt không hợp lệ có thể tạo−99
qua fallback/filter/định dạng; đổi riêng ADCga không đổi nhiệt. RC2 sửa lỗi
phần mềm đã tái hiện, **chưa khẳng định đã sửa nguyên nhân M-TEMP đổi theo ga
trong video**. Cần ghi rawADC nhiệt/ga và MotorTemp từ đúng ESC/motor.

Lisp yêu cầu ADC Control TypeNONE; nếu khácNONE thì latch lỗi và khóa mode/
thoátPARK là chốt an toàn, không phải case cần bypass. Đọc cấu hình thực tế
và fault theo [TEST_CASES.md](./TEST_CASES.md) trước khi kết luận nút vẫn lỗi.

## Tái tạo build

Source nền và SHA từng file nằm trong manifest. Apply source-overlay trên
đúng base, dùng ESP-IDF5.5.3 và dependencies.lock của repo, sau đó chạy từ root:

```powershell
idf.py -B build_jc4880 -D SDKCONFIG=build_jc4880/sdkconfig -D 'SDKCONFIG_DEFAULTS=sdkconfig.defaults;sdkconfig.defaults.jc4880' reconfigure build
```

Máy build dùng compiler/Python/CMake/Ninja cài sẵn trongEIM; đặt
`ESP_IDF_VERSION=5.5` để Kconfig WiFi remote chọn đúng include, bổ sung gzip
trong Gitusrbin vàoPATH. Không cài lại tool hoặc đổi dependency. LVGL8.3.11,
UART debug bridge tắt, không có bench WiFi override. Build còn warning sẵn có;
không tuyên bố sạch warning. Phiên bản/size và kiểm image cuối xem manifest.
