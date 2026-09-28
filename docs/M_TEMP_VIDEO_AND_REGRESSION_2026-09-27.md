# M-TEMP trong video và kết quả tái hiện regression

Ngày: 2026-09-27. Video người dùng: `VID_20260925_061821.mp4`, 66.666 giây,
3840×2160, 1.994 frame, SHA256
`33219218b1663cb52a0d563ea441f13894df7bb09d6f245de48ed85bc1f5a14c`.
Đọc hình bằng OpenCV/Pillow, có xử lý rotation metadata. Không phân tích âm thanh.

## Kết luận có bằng chứng

M-TEMP thay đổi theo thao tác núm trong vài giây, không phải motor nóng lên thật.
Người dùng xác nhận motor không có cảm biến nhiệt và VESC Tool cũng đổi nhiệt.
Điều đó khiến lời giải thích chỉ do widget P4 không đủ. Chưa biết Tool nối trực
tiếp ESC hay qua P4, loại ESC/firmware, chân núm đang dùng hoặc Sensor Type.
**Chưa chứng minh nguyên nhân điện/cấu hình bên ESC, cũng chưa giải thích được
vì sao thay riêng Lisp kích hoạt hiện tượng.** Không sửa scale/offset, ẩn hoặc ép nhiệt về 0.

| Mốc video | M-TEMP đọc được | Quan sát đồng thời |
|---|---|---|
| 0, 3, 4 s | −99°C | Speed 00, current 0.0 A, C-TEMP 30°C |
| 5 s | 2°C | Có thao tác núm; điện áp 25.2 V |
| 6, 7 s | 1°C | Speed/current vẫn 0, C-TEMP 30°C |
| 9 s | 2°C | Không gán phần trăm ga hoặc chiều xoay từ hình |
| 10–12 s | −99°C | Trở về số âm trong vài giây |
| 49 s | −99°C | Giai đoạn dashboard sau Settings |
| 52 s | −17°C | Đơn vị nhiệt vẫn là °C |

Ảnh trích có timestamp nằm trong [gói RC](../release/jc4880-v1.3.8-rc1-2026-09-27/evidence/).
Ở 20/23/44/46/47 s thấy Park và Modes 1/2/3; ở 21/24/45/48 s drawer biến mất.
Điều này phù hợp phản hồi nhả tay thì menu thu vào; ảnh lấy mẫu không xác định
chính xác callback/timestamp nhả tay. Settings mở và cuộn được khoảng 26–40 s.
ODO 15 km thành 9 mi đi cùng đổi đơn vị, không phải bằng chứng reset ODO.
Màn hình VESC Tool cuối video quá mờ để tự đọc số nhiệt/lỗi/model.

## Đường dữ liệu và ý nghĩa −99°C

Đã đọc nguyên bản VESC 6.05/6.06/7.00; tham chiếu cụ thể
[mc_interface.c của VESC 7.00](https://github.com/vedderb/bldc/blob/20cbb362687291242ab90b99f25fbfe8835540fc/motor/mc_interface.c#L2271):

1. Sensor Type quyết định cách chuyển điện áp đầu vào thành nhiệt.
2. `TEMP_SENSOR_DISABLED` dùng `m_temp_override`, không dùng công thức NTC.
3. NaN/Inf hoặc nhiệt ngoài khoảng −200…600 được thay bằng −100, rồi lọc thấp.
4. SETUP command 47/51 gửi nhiệt dạng signed i16 ×10.
5. [RT decoder P4](../components/vesc_can/vesc_rt_data.c) chia đúng 10;
   [UI updater](../main/vesc_ui_updater.c) chuyển nhiệt lên dashboard;
   [cockpit formatter](../Super_VESC_Display/custom/custom.c) ép số nguyên.

Ví dụ giá trị lọc −99.9°C có thể hiện −99°C. Đây là cơ chế **phù hợp** video,
chưa phải bằng chứng ESC thực tế đã đi qua nhánh invalid này. Không có cảm biến
không đồng nghĩa cấu hình đã đặt Disabled. Nếu vẫn bật chuyển đổi cảm biến trên
một đầu vào bỏ trống/bị ảnh hưởng bởi ga, số hiển thị không đại diện nhiệt motor.
Chỉ xác định chung chân, nhiễu hoặc override sau khi biết đúng board/custom firmware.

Binding chính thức [get-adc](https://github.com/vedderb/bldc/blob/20cbb362687291242ab90b99f25fbfe8835540fc/lispBM/lispif_vesc_extensions.c#L1184)
đọc index 0/1/2 từ EXT/EXT2/EXT3 và index 3 từ TEMP_MOTOR. Đây là reference cho
phép đo chỉ đọc trên ESC tương thích; không phải xác nhận chân của ESC đang dùng.
Cả Lisp gốc và Lisp release không ghi Sensor Type, override nhiệt hoặc remap ADC.

## Lỗi đã tái hiện, sửa và kiểm tra lại

| Phạm vi | Tái hiện trên bản cũ | Thay đổi đã làm | Kết quả sau sửa |
|---|---|---|---|
| Drawer LVGL | Giữ lượt chạm mở, nhả trong/ngoài drawer: menu đóng; mở lại trong animation đóng để lại overlay | Dùng cơ chế `lv_indev_wait_release` sẵn có, chặn mở khi drawer cũ còn sở hữu animation | 9/9 case; cũ 3/9 |
| Lisp status/safety | Input fault chốt nhưng poll trả result 0; mode bị từ chối mà không báo đúng nguyên nhân | Poll ưu tiên safety fault hoặc motor heartbeat mất/quá hạn; giữ ACK và chốt an toàn | Python branch 17/17, cũ 14/17; LispBM 18/18, cũ 16/18 |
| Telemetry 51 | Gói thiếu dữ liệu vẫn refresh timestamp/cập nhật một phần/đọc lệch trường sau | Kiểm tra độ dài toàn bộ trường mask đã biết trước mọi cập nhật | 14/14, cũ 10/14; gồm 20 độ dài cắt ngắn và phục hồi |

UI chạy production drawer với LVGL 8.3.11 thật, input/timer/animation thật trên host;
mock biên CAN/dashboard. Lisp chạy full script với reader, const heap, scheduler,
trap và event thread của LispBM 32-bit ghim VESC 7.00; API ESC là fixture.
Telemetry compile production RT/IO decoder và buffer. Review độc lập phát hiện
một test all-fields có thể false PASS; đã đổi timestamp/giá trị và kiểm tra thực
sự nhận packet trước khi chốt kết quả.

Giá trị −99.9/2/1/−17°C được decoder đọc đúng. Thay ADC command 32 từ 0→50→100%
không đổi motor temperature. Gói lỗi là lỗi parser tái hiện được, **chưa chứng minh
là nguyên nhân video**. Mode UI và nút TX vẫn hoạt động với release cũ trong VM
khỏe mạnh: bản Lisp RC chỉ sửa báo lỗi, không tự xóa fault để ép mode chạy.

## Bàn giao kiểm tra hardware

Gói [JC4880 v1.3.8-rc1](../release/jc4880-v1.3.8-rc1-2026-09-27/README.md)
kèm binary, Lisp và [test case chi tiết](../release/jc4880-v1.3.8-rc1-2026-09-27/TEST_CASES.md).
Kết quả host không xác nhận GT911, timing FreeRTOS, CAN reassembly, ADC điện,
firmware ESC thực tế hoặc khả năng vận hành motor. M-TEMP và việc mode bị khóa
trên ESC của người dùng còn chờ feedback hardware.
