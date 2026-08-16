# Phân tích kỹ thuật tab BMS

## Kết luận kiến trúc

Nên thêm tab `BMS` vào màn hình Realtime hiện có, thay vì trộn dữ liệu BMS vào
dashboard hoặc Settings. Bản đầu chỉ đọc dữ liệu, dùng một backend riêng và cập
nhật UI từ snapshot; không gọi LVGL từ task CAN.

Luồng đề xuất:

```text
VESC BMS trên CAN
  -> COMM_BMS_GET_VALUES (CAN ID BMS đã chọn)
  -> comm_can reassembly/packet dispatch
  -> vesc_bms_data parser + snapshot nguyên tử
  -> lv_timer của tab BMS
  -> Overview / Cells / Temperatures
```

Polling đầy đủ nên chạy khoảng 1 Hz và chỉ hoạt động khi tab BMS đang mở. Tất cả
request/reply liên tục phải chạy trong task poll chung ở `vesc_rt_data.c`; không
tạo thêm task tự gọi `comm_can_send_buffer_sync`, vì reassembly hiện tại yêu cầu
các giao dịch được tuần tự hóa.

## Trạng thái repo hiện tại

- `components/vesc_can/include/vesc_can/vesc_datatypes.h` mới biết
  `HW_TYPE_VESC_BMS`, nhưng chưa khai báo `COMM_BMS_GET_VALUES = 96`, các CAN
  packet BMS hoặc cấu trúc dữ liệu BMS.
- `components/vesc_can/vesc_rt_data.c` chỉ đọc telemetry của ESC. `v_in`,
  `current_in` và `battery_level` hiện không phải dữ liệu cell-level từ BMS.
- `components/vesc_can/comm_can.c` đã nhận/reassemble packet dài và
  `comm_can_ping()` có thể trả `HW_TYPE`, nên có thể dùng hạ tầng hiện tại để
  truy vấn BMS theo CAN ID.
- `main/main.c::vesc_packet_dispatch()` đã fan-out response tới các parser; cần
  thêm parser BMS tại đây.
- `Super_VESC_Display/custom/realtime_viewer.c` đã có lifecycle phù hợp: tạo màn
  hình theo nhu cầu, timer cập nhật label, tắt poller và xóa object khi unload.
  Đây là vị trí ít xung đột nhất để thêm tab.
- Cả ESP-IDF và simulator đều glob `custom/*.c`, nên có thể tách phần dựng tab
  thành file mới mà không phải duy trì danh sách source UI thủ công. Component
  `vesc_can` vẫn phải thêm backend mới vào `components/vesc_can/CMakeLists.txt`.

## Điều kiện giao thức phải xác nhận

Thiết kế trên áp dụng trực tiếp khi BMS là VESC BMS hoặc thiết bị tương thích
giao thức VESC. Nếu BMS chỉ gửi CAN proprietary, BLE hoặc UART thì cần DBC/tài
liệu frame của đúng model và backend khác; `COMM_BMS_GET_VALUES` sẽ không hoạt
động.

VESC Tool chính thức dùng `COMM_BMS_GET_VALUES = 96`. Response có các trường:

- Điện áp pack, điện áp cổng sạc.
- Dòng pack và dòng đo tại IC.
- Bộ đếm Ah/Wh.
- Số cell, điện áp từng cell và trạng thái balance từng cell.
- Số cảm biến nhiệt, nhiệt độ từng cảm biến, nhiệt IC.
- Nhiệt/độ ẩm/áp suất nếu phần cứng hỗ trợ.
- Nhiệt cell cao nhất, SOC, SOH, CAN ID.
- Tổng Ah/Wh sạc/xả, `data_version` và status string ở firmware mới.

Protocol cho phép tối đa 50 cell và 50 cảm biến nhiệt. Parser phải kiểm tra số
byte còn lại và kẹp count trước mọi vòng lặp; packet lỗi không được ghi đè
snapshot cuối cùng còn hợp lệ.

Nguồn chính thức:

- <https://github.com/vedderb/vesc_tool/blob/master/commands.cpp>
- <https://github.com/vedderb/vesc_tool/blob/master/datatypes.h>
- <https://github.com/vedderb/vesc_bms_fw>

## Nội dung tab đề xuất

### Overview

- Trạng thái kết nối: `Live`, `Stale`, `Not found`, tuổi dữ liệu và CAN ID.
- Pack voltage, charge voltage, pack current và công suất `V_pack * I_pack`.
- SOC, SOH và status string.
- Cell count, cell thấp nhất/cao nhất, số thứ tự và `delta = max - min`.
- Nhiệt cell cao nhất, nhiệt IC và số cảm biến nhiệt.
- Trạng thái có cell đang balancing hay không.
- Ah/Wh phiên hiện tại và tổng charge/discharge nếu data version hỗ trợ.

SOH phải có chú thích: VESC BMS hiện có thể báo 100% vì thuật toán SOH chưa được
triển khai đầy đủ. Không dùng SOH làm điều kiện an toàn duy nhất.

### Cells

- Một row/card cho mỗi cell: số cell, điện áp mV/V và icon balancing.
- Luôn đánh dấu cell thấp nhất và cao nhất; hiển thị delta pack ở đầu danh sách.
- Chỉ tạo/xóa object khi cell count thay đổi; timer bình thường chỉ cập nhật
  label/style để tránh churn bộ nhớ LVGL.
- Hỗ trợ danh sách cuộn tới 50 cell, không giả định pack hiện tại là 10S chỉ vì
  đang đo 40 V.

Không hard-code ngưỡng đỏ theo hóa học pin trong bản đầu. Điện áp an toàn của
Li-ion, LiFePO4 và các hóa học khác khác nhau; trước khi biết chemistry, UI chỉ
nên highlight min/max, delta và status do BMS cung cấp.

### Temperatures

- Danh sách cảm biến T1..Tn, nhiệt IC và nhiệt cell cao nhất.
- Cảm biến không tồn tại phải hiển thị `N/A`, không hiển thị `0 C` như một số đo.
- Chỉ đổi sang Fahrenheit qua setting đơn vị hiện có; snapshot backend luôn lưu
  Celsius.

## Chiến lược CAN ID và nhiều BMS

### Bản đầu

- Thêm `BMS CAN ID` trong Settings/NVS, kèm nút `Detect` và lựa chọn Disabled.
- Khi Detect, ping tuần tự và chỉ nhận node trả `HW_TYPE_VESC_BMS`.
- Không quét toàn bus lúc boot; quét chỉ khi người dùng yêu cầu để tránh tăng
  thời gian khởi động và lưu lượng CAN.
- Nếu BMS cũ trả PONG không có byte HW type, không tự kết luận chắc chắn đó là
  BMS; phải thử `COMM_FW_VERSION` hoặc `COMM_BMS_GET_VALUES` trước khi lưu ID.

### Nhiều BMS

- Hiển thị selector theo CAN ID và giữ snapshot riêng cho từng node.
- Không cộng điện áp, dòng hoặc trung bình SOC nếu chưa biết các pack mắc nối
  tiếp hay song song.
- Distributed balancing có thể có nhiều BMS; tab phải coi từng BMS là một nguồn
  độc lập. Aggregation chỉ làm ở giai đoạn sau khi có topology rõ ràng.

## Polling, freshness và đồng bộ

- Full BMS poll: 1000 ms khi tab đang nhìn thấy; dừng khi chuyển tab hoặc unload.
- UI timer: 250-500 ms; timer chỉ đọc snapshot, không tự gửi CAN.
- Đánh dấu stale sau khoảng 3 lần chu kỳ poll; sau đó giữ giá trị cuối nhưng đổi
  màu và hiện tuổi dữ liệu, thay vì biến toàn bộ thành số 0.
- Một timeout không xóa snapshot. Sau nhiều timeout liên tiếp mới chuyển sang
  `Not found` và cho phép Retry/Detect.
- Mảng cell/temperature cần double-buffer hoặc critical section ngắn. Không để
  LVGL đọc giữa lúc parser đang thay count và copy mảng.
- Thêm `vesc_bms_data_loop()` vào task poll chung trong `vesc_rt_data.c`, giống
  `vesc_io_data_loop()`. Không tạo poll task BMS riêng.
- Response parser được gọi từ `main.c::vesc_packet_dispatch()` và chỉ làm parse,
  timestamp, swap snapshot; không tạo object hay gọi hàm LVGL.

## Phân biệt dữ liệu ESC và BMS

Tab phải ghi rõ nguồn:

| Giá trị | ESC hiện tại | BMS mới |
|---|---|---|
| Điện áp | `vesc_rt_data.v_in` tại đầu vào ESC | `v_tot` tại BMS |
| Dòng | `current_in` do ESC đo/ước lượng | `i_in` do BMS đo |
| SOC | `battery_level` do cấu hình ESC tính | `soc` do BMS báo |
| Cell | Không có | `v_cells[]` |
| Nhiệt pin | Không có trực tiếp | `temps[]`, `temp_cells_highest` |

Không âm thầm thay SOC dashboard bằng SOC BMS trong cùng thay đổi. Sau khi tab
đọc ổn định mới thêm setting `Battery data source: ESC / BMS`, quy tắc fallback
và cảnh báo stale. Chênh lệch điện áp ESC/BMS có thể do sụt áp trên dây khi tải,
nên chỉ hiển thị cả hai trước khi đặt ngưỡng fault.

## Các case bắt buộc xử lý

| Case | Hành vi yêu cầu |
|---|---|
| Không có BMS | Tab hiện `No BMS configured`, nút Detect/Settings; không hiện 0 V |
| Sai CAN ID | Timeout có giới hạn, không block UI; cho Retry/Detect |
| BMS đang sleep | Thử ping/query theo nhịp thấp; không spam bus liên tục |
| BMS mất kết nối khi đang xem | Giữ số cuối, hiện `Stale` và tuổi dữ liệu |
| Packet cũ thiếu field optional | Parse phần có mặt, field thiếu là invalid/N/A |
| Packet malformed/count > 50 | Bỏ toàn bộ packet, log throttled, giữ snapshot cũ |
| Cell count thay đổi | Rebuild riêng danh sách cell an toàn trên LVGL task |
| Một cell không hợp lệ | Đánh dấu riêng cell; không làm cả màn hình thành 0 |
| Đang sạc/xả/regen | Giữ nguyên dấu dòng từ giao thức và ghi rõ quy ước sau khi đo thực tế |
| Balancing | Icon theo từng cell; overview báo số cell đang balance |
| Nhiều BMS | Selector theo ID, không tự cộng/trung bình |
| BMS proprietary | Báo protocol unsupported; cần driver/DBC của nhà sản xuất |
| Simulator | Có fixture bình thường, unbalanced, charging, hot, stale và no-BMS |
| Chuyển màn hình liên tục | Không double timer, không use-after-free, poll active đúng lifecycle |
| CAN bận do RT/Lisp/PAS | BMS poll được tuần tự trong task chung và ưu tiên thấp hơn motor/PAS |

## Phạm vi file dự kiến

### Backend mới

- `components/vesc_can/include/vesc_can/vesc_bms_data.h`
- `components/vesc_can/vesc_bms_data.c`

Backend chịu trách nhiệm target ID, active flag, request interval, parser,
snapshot, freshness và injection cho test.

### File cần sửa

- `components/vesc_can/include/vesc_can/vesc_datatypes.h`: command ID và struct.
- `components/vesc_can/CMakeLists.txt`: thêm source backend.
- `components/vesc_can/vesc_rt_data.c`: pump BMS trong task CAN poll chung.
- `main/main.c`: init backend và fan-out response.
- `components/dev_settings/*`: lưu BMS CAN ID/enabled.
- `Super_VESC_Display/custom/settings_wrapper.*`: bridge device/simulator.
- `Super_VESC_Display/custom/realtime_viewer.c`: thêm tabview/lifecycle tab BMS.
- Có thể thêm `Super_VESC_Display/custom/bms_view.c/.h` để giới hạn kích thước
  `realtime_viewer.c`.
- `main/vesc_sim.c` hoặc injection riêng: dữ liệu BMS giả cho simulator/test.

## Những lệnh không đưa vào bản đầu

VESC protocol có các lệnh cho phép/khóa sạc, override balance, reset counter,
force balance và zero current offset. Đây là lệnh thay đổi trạng thái BMS, có thể
ảnh hưởng sạc và bảo vệ pin. Bản đầu chỉ đọc. Nếu bổ sung sau này phải có kiểm
tra quyền, xác nhận hai bước, trạng thái xe/sạc phù hợp và feedback ACK/timeout.

## Thứ tự triển khai đề xuất

1. Xác nhận model BMS, giao thức và CAN ID thực tế.
2. Viết struct/parser với fixture packet và kiểm thử bounds/version trước.
3. Tích hợp poll 1 Hz vào task chung; log snapshot trên bench, chưa làm UI.
4. Thêm BMS ID/Detect và các trạng thái no-data/stale.
5. Dựng tab Overview, sau đó Cells và Temperatures.
6. Thêm simulator fixtures và test lifecycle chuyển tab/màn hình.
7. Bench với pack thật: đối chiếu VESC Tool từng giá trị, dấu dòng và balancing.
8. Chỉ sau khi ổn định mới cân nhắc dùng SOC BMS cho dashboard hoặc thêm cảnh báo.

## Tiêu chí hoàn thành bản đầu

- Giá trị pack/cell/nhiệt/SOC khớp VESC Tool trong sai số format.
- Không crash với 0, 1, số cell thực và tối đa 50 cell.
- Mất BMS không làm treo UI hoặc ảnh hưởng RT/PAS/Lisp polling.
- Tab đóng thì full BMS polling dừng.
- Không có lệnh ghi/thay trạng thái BMS trong UI.
- Simulator và firmware device đều build; parser có test packet normal,
  truncated, oversized count và firmware cũ thiếu optional fields.
