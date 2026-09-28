# Kiểm tra RC2 — JC4880 / ESC VESC 6.05

## Phạm vi

Test tự động chạy production C/LVGL và LispBM upstream với biên phần cứng
giả lập. Kết quả host không xác nhận timing CAN, ADC điện, cảm ứng GT911 hoặc
motor thật. Firmware6.05 đã được người dùng xác nhận; model ESC và cấu hình
nhiệt thực tế chưa được cung cấp.

## Các lỗi phải bị chặn sau sửa

| ID | Tái hiện | Kỳ vọng RC2 |
|---|---|---|
| CAN-01 | Gói thiếu đuôi nhưng buffer còn byte cũ giống đuôi cần nhận | Không dispatch, không cập nhật snapshot/thời gian |
| CAN-02 | Xen layout A/B rồi chỉ gửi prefixA + CRC_A | Không dùng đuôi cũ để hoàn thành gói thiếu |
| CAN-03 | Dashboard target10, reply mang motorID11 | Dashboard giữ nhiệt motor10; bridge vẫn nhận reply motor11 |
| CAN-04 | STATUS4 hoặc control frame thiếu DLC | Không đọc byte ngoài DLC, không tạo mẫu nhiệt; không crash |
| CAN-05 | Fragment gap, sai offset, quá giới hạn; sau đó gửi gói đủ | Gói sai bị loại, gói đủ tiếp theo phục hồi |
| CAN-06 | Gói1024byte qua fragment short/LONG | Nhận đủ và đúng byte/CRC |
| CAN-07 | VESC6.05 dual: baseID10, hỏi motorID11 | Nhận nhiệt motor11 dù envelope sender10; Tool hỏi motor11 không ghi đè dashboard target10 |
| CAN-08 | SETUP selective thiếu motorID hoặc identity không nhất quán | Không cập nhật RT dashboard; request dashboard luôn yêu cầu bit17 |
| RT-01 | Cắt legacy47 tại các độ dài thiếu bất kỳ | Không commit một phần và không làm mới tuổi dữ liệu |
| RT-02 | Cắt selective51 thiếu trường được chọn | Không commit một phần; gói đủ tiếp theo được nhận |
| RT-03 | Nhiệt quá hạn, tiếp tục nhận mask0/current-only | Current vẫn cập nhật; nhiệt không được coi là mới |
| RT-04 | Đổi target ID hoặc restart RT cache | Xóa hiệu lực mẫu nhiệt của target cũ; polling giữ active/paused |
| UI-01 | Vuốt mở drawer rồi nhả ngoài/trên vùng mode | Drawer còn mở; lượt mở không tự kích hoạt nút |
| UI-02 | Chạm ngoài bằng lượt mới; mở lại khi đang đóng | Đóng bình thường, không overlay mồ côi |
| L-01 | ADC NONE, ga nhả, worker khỏe, chọn1→2→3 trong PARK | Chọn mode được xác nhận; PARK vẫn khóa lực kéo |
| L-02 | ADC range lỗi hoặc native control khácNONE | Giữ khóa truyền động/mode; status báo fault, không báoOK |
| TEMP-01 | Đổi riêng ADC ga trong fixture6.05 | Nhiệt không đổi; decoder thực sự nhận ADC mới |
| TEMP-02 | Đổi chính ADC nhiệt / Disabled override | Đi đúng công thức nguồn6.05; không tự zero/che số nhiệt |

Log trước/sau và phiên bản source nằm trong [test-results/](./test-results/).
Case baseline FAIL là bằng chứng tái hiện, không phải kết quả của RC2.
CAN-02 chứng minh thiếu kiểm tra đủ mảnh; CRC_A mô tả giá trịA, không chứng
minh sender gửi một nhiệt độ nhưng decoder đọc thành nhiệt khác.

## Hardware feedback

Ghi model ESC, firmware/build6.05, Sensor Type, app đang dùng, ADC Control Type,
CAN target và Tool nối trực tiếp ESC hay BLE qua P4. Giữ bản cấu hình/Lisp cũ
để đối chiếu. Test trên bàn thử ngăn lực kéo ngoài ý muốn, bắt đầu ga nhả.

| ID | Thao tác | Kỳ vọng / dữ liệu cần ghi | Kết quả |
|---|---|---|---|
| HW-00 | Nạp P4RC2 và Lisp trong cùng folder, khởi động lại | Version1.3.8-rc2, Lisp không runtime error, ghi trạng thái ban đầu | PENDING |
| HW-01 | Vuốt drawer và nhả10lần ngoài/trên mode | Giữ mở; chọn mode bằng lượt chạm tiếp theo | PENDING |
| HW-02 | Với input khỏe, ởPARK chọn1→2→3→1 | Mode được xác nhận, không tự thoátPARK | PENDING |
| HW-03 | NútTX đã nhả sau boot: bấm ngắn ở ga0 | TrongPARK: yêu cầu thoát; sau khi thoát: lần bấm tiếp mới đổi mode | PENDING |
| HW-04 | Đứng yên, ga nhả, có phanh hợp lệ, giữTX≥1,2s | VàoPARK, nhả không đổi mode; thiếu phanh thì từ chối11 | PENDING |
| HW-05 | Settings→dashboard, mở/đóng nhanh | Không layer mờ/touch bị giữ hoặc số vẽ lên Settings | PENDING |
| HW-06 | Nếu mode bị khóa | Ghi fault9/12, ADC Control Type, ADC range và lỗi Lisp; không ép xóa fault | PENDING |
| HW-07 | TrongPARK, thao tác ga và ghi P4/Tool đồng thời | Ghi M-TEMP, C-TEMP, ADC ga, ADC nhiệt, CAN node/đường Tool | PENDING |
| HW-08 | Tool quaBLE chọn node khác nếu có | Tool đọc node đã chọn; nhiệt dashboard vẫn từ target của dashboard | PENDING |
| HW-09 | Realtime viewer khi mất nhiệt/nguồn telemetry | Trường nhiệt quá hạn hiện không có dữ liệu; có mẫu mới thì phục hồi | PENDING |

Lisp yêu cầu ADC Control TypeNONE để một nguồn điều khiển motor. “Ga mặc
định” không cho biết giá trị đang lưu. Nếu khácNONE, lỗi cấu hình được báo và
giữ khóa an toàn; RC2 không tự ghi cấu hình ESC hoặc bỏ interlock.

## Đọc M-TEMP trên 6.05

Các expression chỉ đọc official6.05 hỗ trợ: `(get-temp-mot)`, `(get-adc 0)`,
`(get-adc 3)`, `(get-adc-decoded 0)`, `(conf-get 'adc-ctrl-type)`,
`(conf-get 'm-motor-temp-sens-type)`, `(conf-get 'm-ntc-motor-beta)`.
`get-adc3` đọc kênh nhiệt motor1; không dùng để kết luận nhiệt motor2.
`get-temp-mot-res` không có trên official6.05. Sensor enumNTC10k=0, Disabled=8.

Chạy lại VM6.05 với heap theo source firmware:

```text
python scripts/test_lisp_runtime.py --vesc-version 6.05 --heap-cells 2464 --cc <mingw32-gcc>
python scripts/test_mtemp_conversion.py --vesc-version 6.05 --cc <host-gcc>
python scripts/test_mtemp_transport.py --cc <host-gcc>
python scripts/test_mtemp_ui.py --cc <host-gcc>
python scripts/test_vesc_telemetry.py --cc <host-gcc>
python scripts/test_lisp_panel_ui.py --cc <host-gcc>
python scripts/test_lisp_safety.py
python scripts/test_target_polling.py --cc <host-gcc>
```

VM6.05 heap2464 đạt27/27; auxiliary memory18KiB và GC160 theo nguồn official.
API ESC vẫn là fixture, nên đây không phải bằng chứng kiểm thử driver/hardware.

Nếu Tool trực tiếp và rawADC nhiệt cùng đổi theo ga, tiếp tục truy board/
wiring/mux/config; nếu Tool trực tiếp ổn định nhưng P4 đổi, thu packetCAN và
sender. RC2 chưa được xác nhận sửa nguyên nhân M-TEMP trong video.

Khi A/B, giữ P4/ESC/wiring/config cố định, thử nạp lại cùng Lisp cũ để tách
tác dụng restart khỏi nội dung mới. Lisp gốc khởi động ga bật và không có
PARK: chỉ thử khi bàn thử đã ngăn lực kéo ngoài ý muốn; trạng tháiP trên P4
không chứng minh bản Lisp gốc khóa motor.
