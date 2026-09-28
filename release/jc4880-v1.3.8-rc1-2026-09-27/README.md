# JC4880 v1.3.8-rc1 — 2026-09-27

Bản thử nghiệm để người dùng kiểm tra hardware. Đã build và chạy regression
trên host; **chưa flash/chạy thử trên P4 hoặc ESC thật**.

## File để sử dụng

| File | Mục đích |
|---|---|
| [esp32p4-jc4880-v1.3.8-rc1-ota.bin](./esp32p4-jc4880-v1.3.8-rc1-ota.bin) | Cập nhật ứng dụng P4 qua OTA trên JC4880 đã có layout 16 MB hiện tại |
| [merged.bin](./merged.bin) | Image USB toàn bộ phần boot/app, offset **0x0**; có padding qua vùng NVS, cần sao lưu setting trước |
| [main.lisp](./main.lisp) | Nạp riêng lên ESC bằng VESC Tool/Lisp editor; không nằm trong firmware P4 |
| [TEST_CASES.md](./TEST_CASES.md) | Bước tái hiện, kết quả trước/sau và bảng feedback hardware |
| [release.json](./release.json), [SHA256SUMS.txt](./SHA256SUMS.txt) | Board, version, source identity và checksum |
| [source-overlay.zip](./source-overlay.zip) | Các source/test đã thay đổi, đường dẫn giữ tương đối từ repo; áp dụng trên đúng base commit để tái tạo |
| [test-results/](./test-results/) | Log baseline, log sau sửa, build và kiểm tra image |
| [evidence/](./evidence/) | Ảnh trích video có timestamp |

Target: **Guition JC4880P443C, ESP32-P4, flash 16 MB, DIO 80 MHz**.
Image header giới hạn silicon revision 1.0–1.99. Không dùng cho S3/Waveshare.
Firmware hiển thị version **1.3.8-rc1**; file app 4.478.656 byte, còn 764.224 byte
trong OTA slot 5.242.880 byte. ESP-IDF 5.5.3, LVGL 8.3.11, UART debug bridge tắt.

Ưu tiên file OTA nếu thiết bị đang dùng đúng layout này để giữ cấu hình.
`merged.bin` không phải file đưa vào OTA. Không cấp điều kiện tạo lực kéo ngoài
ý muốn khi nạp Lisp hoặc kiểm tra PARK/ga; các test hardware thực hiện trên bàn
thử với bánh dẫn động được cố định/nâng an toàn và có ngắt nguồn chủ động.

## Đã sửa

- Drawer không dùng lượt nhả của thao tác mở làm cú chạm đóng; không mở chồng
  khi animation đóng còn sở hữu drawer cũ. Dựa trên API chờ nhả của LVGL.
- Poll trạng thái Lisp báo đúng safety/input fault và heartbeat mất/quá hạn,
  thay vì báo OK trong lúc mode bị khóa. Giữ nguyên ga, ADC, PARK và interlock.
  Vào PARK từ trạng thái chạy cần đứng yên, ga nhả và phanh hợp lệ; nút TX giữ
  dài cần ≥1 s sau debounce. Thiếu phanh sẽ bị từ chối với mã 11.
- Command 51 thiếu dữ liệu bị loại trước khi sửa snapshot/timestamp; tránh
  nhiệt cũ bị coi là mới và các trường phía sau bị đọc lệch.

## Kết quả kiểm tra

| Suite | Release cũ | Sau sửa |
|---|---:|---:|
| UI production + LVGL thật | 3/9 | **9/9** |
| Nhánh safety Lisp | 14/17 | **17/17** |
| Full Lisp trong LispBM 32-bit VESC 7.00 | 16/18 | **18/18** |
| Production telemetry decoder | 10/14 | **14/14** |

Ride safety 2.532 checks, transport 53 checks, gear 81 checks và target polling
đều PASS. Baseline FAIL là kết quả mong đợi để chứng minh test phát hiện lỗi.
Build exit 0; image checksum/hash hợp lệ và version đúng. Có warning ở mã hiện
hữu (unused, callback cast, deprecated touch API); không tuyên bố build sạch warning.

UI mock biên CAN/dashboard; LispBM dùng API ESC giả lập; host không kiểm chứng
GT911, FreeRTOS/CAN timing, ADC điện hoặc motor thực. Không đồng nhất số case
PASS với bảo đảm vận hành hardware.

## M-TEMP còn cần xác minh

Video có −99°C → 1–2°C → −99°C khi thao tác núm, speed/current bằng 0 và không
có cảm biến nhiệt. Nguồn số đo hoặc chuyển đổi nhiệt bên ESC là hướng cần kiểm
tra; VESC Tool cũng đổi theo phản hồi người dùng. Test không tái hiện việc một
packet ADC hợp lệ đổi M-TEMP. Không có bằng chứng gói thiếu là nguyên nhân video.
**RC này chưa khẳng định sửa được M-TEMP hoặc fault ngoài thực tế làm khóa mode.**

Phân tích và căn cứ official: [báo cáo video/regression](../../docs/M_TEMP_VIDEO_AND_REGRESSION_2026-09-27.md).
Feedback cần ghi model/firmware ESC, Sensor Type, chân núm, Tool trực tiếp hay
qua P4, Lisp đang nạp và kết quả theo [bảng test](./TEST_CASES.md).

## Source và cách build

Base repo: `78356d73b92d3a8c566915339de7dfa2e9b6418a`, working tree có thay đổi
chưa commit. Baseline để tái hiện: release `7c77f2b` ngày 2026-09-19.
Lisp gốc ở repo root và gói release cũ giữ nguyên; canonical mới ở
[lisp/main.lisp](../../lisp/main.lisp). Hash chính xác nằm trong manifest.

Sau khi kích hoạt môi trường ESP-IDF 5.5.3, chạy từ root repository:

```powershell
idf.py -B build_jc4880 -D SDKCONFIG=build_jc4880/sdkconfig -D 'SDKCONFIG_DEFAULTS=sdkconfig.defaults;sdkconfig.defaults.jc4880' reconfigure build
```

Trên máy hiện tại đã dùng Python/compiler/CMake/Ninja trong profile EIM cài sẵn;
đặt `ESP_IDF_VERSION=5.5` cho Kconfig WiFi remote. Export chuẩn tìm tool theo layout
khác nên không dùng để build sau khi nó báo thiếu tool. Không cài lại tool hoặc
thay đổi source để xử lý lỗi môi trường. Không build APK/board khác, không flash,
ghi cấu hình ESC, commit hoặc push trong lượt này.
