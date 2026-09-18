# Ride mode and reverse BE contract

> **SUPERSEDED / KHONG IMPLEMENT THEO TAI LIEU NAY.** Contract nay mo ta mode
> theo speed/percent va reverse tren `pin-ppm`, nen da loi thoi. Source-of-truth
> hien tai la `docs/RIDE_MODE_ABSOLUTE_CURRENT_PLAN.md`: mode theo ampere tuyet
> doi, bo cruise, `pin-tx = Mode`, `pin-rx = Reverse hold`. Giu file nay chi de
> doi chieu lich su cho den khi contract v2 duoc tach/chot.

Tài liệu này là handoff để Claude triển khai backend cho:

- chỉnh trực tiếp tốc độ của ba ride mode;
- tách giới hạn tốc độ khỏi giới hạn dòng/mô-men;
- lưu cấu hình trên VESC;
- đọc nút lùi vật lý và điều khiển lùi an toàn;
- cung cấp trạng thái thật cho FE ESP32-P4.

Phạm vi backend gồm `lisp/main.lisp` (nguồn quyết định an toàn và điều khiển
motor) cùng transport/model trong `components/vesc_can/`. FE màn hình sẽ được
triển khai riêng trong `Super_VESC_Display/custom/`.

## 1. Hiện trạng phải giữ tương thích

- VP magic là `0x56 0x50` (`V`, `P`) trên `COMM_CUSTOM_APP_DATA`.
- Message đang dùng: `0x01` REQ_UI, `0x02` ACTION, `0x03` REQ_STATE,
  `0x04` REQ_DASH, `0x05` PAS_SET và `0x06` throttle toggle.
- Profile 0/1/2 hiện là 5/10/20 km/h và 30/60/100% dòng.
- RX đang điều khiển cruise; TX đang đổi profile/giảm cruise.
- ADC1/ADC2 đang là ga/phanh.
- `motor-control-loop` phải tiếp tục là nơi duy nhất phát lệnh motor.
- Không đổi hoặc dùng lại các message/control ID đã tồn tại.

## 2. Quyền sở hữu dữ liệu

VESC Lisp là nguồn sự thật cho ride config và mọi interlock. P4 chỉ:

1. yêu cầu bản cấu hình;
2. gửi một bản cấu hình đầy đủ khi người dùng nhấn Save;
3. nhận ACK cùng các giá trị thực đã áp dụng;
4. hiển thị snapshot/status.

Không đặt điều kiện an toàn chỉ ở FE. Không lưu một bản cấu hình độc lập trong
NVS của P4 vì sẽ tạo hai nguồn sự thật.

## 3. Data model bắt buộc

```text
mode[0..2].speed_dkmh       uint16   km/h x 10
mode[0..2].current_permille uint16   0..1000 của Motor Current Max
reverse_enabled             bool
reverse_speed_dkmh          uint16   km/h x 10
reverse_current_dA          uint16   ampere x 10
config_revision             uint16   tăng sau mỗi SET hợp lệ
current_profile             uint8    0..2, runtime
direction_state             int8     1=forward, 0=interlock/coast, -1=reverse
reverse_button              bool     trạng thái raw đã debounce
reverse_armed               bool
persist_pending             bool
```

Default phải giữ hành vi hiện tại:

| Giá trị | Default |
|---|---:|
| Mode 1 | 5.0 km/h, 300 permille |
| Mode 2 | 10.0 km/h, 600 permille |
| Mode 3 | 20.0 km/h, 1000 permille |
| Reverse | disabled, 3.0 km/h, 7.0 A |

Giới hạn backend ban đầu:

- tốc độ tiến: `1.0 .. 150.0 km/h` (`10 .. 1500 dkmh`). Đây là trần cấu hình
  chung của protocol/FE; Lisp vẫn phải giới hạn theo ERPM, motor, điện áp và
  truyền động thực tế của từng xe;
- current scale tiến: `100 .. 1000` permille;
- tốc độ lùi: `1.0 .. 5.0 km/h`;
- dòng lùi: `1.0 .. min(14.0 A, abs(l-current-min), l-current-max)`;
- mode phải có tốc độ không giảm: mode 1 <= mode 2 <= mode 3.

SET có trường ngoài giới hạn phải bị reject, không âm thầm nhận một cấu hình
khác. Response luôn mang lại cấu hình hiện hành để FE tự đồng bộ.

## 4. VP protocol mới

Giữ big-endian như protocol hiện tại. Mọi request P4 -> Lisp vẫn có
`reply_can_id` ngay sau message type. Chỉ gửi request từ CAN poll task hiện có
để không va chạm bộ reassembly đa frame.

### 4.1 Message IDs

```text
P4 -> Lisp
0x07 REQ_RIDE_CONFIG
0x08 SET_RIDE_CONFIG
0x09 SELECT_RIDE_MODE

Lisp -> P4
0x87 RIDE_CONFIG
0x89 RIDE_STATUS
```

### 4.2 REQ_RIDE_CONFIG

```text
56 50 07 <reply_id> <seq:u16>
```

### 4.3 SET_RIDE_CONFIG

SET là transaction toàn bộ, không gửi từng trường khi người dùng bấm `+/-`.

```text
56 50 08 <reply_id> <seq:u16> <format_ver:u8=1>
  <m0_speed_dkmh:u16> <m0_current_permille:u16>
  <m1_speed_dkmh:u16> <m1_current_permille:u16>
  <m2_speed_dkmh:u16> <m2_current_permille:u16>
  <reverse_enabled:u8>
  <reverse_speed_dkmh:u16>
  <reverse_current_dA:u16>
```

Chỉ nhận SET khi:

- `abs(get-speed) <= 0.3 km/h`;
- ga <= 5%;
- reverse không active;
- payload đủ độ dài và `format_ver` được hỗ trợ.

### 4.4 SELECT_RIDE_MODE

```text
56 50 09 <reply_id> <seq:u16> <profile:u8>
```

Cho phép chọn profile tiến khi không ở reverse. Nếu xe đang chạy thì vẫn có thể
đổi profile như hiện tại, nhưng không được đổi direction. Lisp áp dụng ngay
profile mới và trả `RIDE_STATUS`.

### 4.5 RIDE_CONFIG response/ACK

GET và SET đều trả đúng một response loại `0x87`:

```text
56 50 87 <seq:u16> <result:u8> <format_ver:u8=1>
  <config_revision:u16>
  <m0_speed_dkmh:u16> <m0_current_permille:u16>
  <m1_speed_dkmh:u16> <m1_current_permille:u16>
  <m2_speed_dkmh:u16> <m2_current_permille:u16>
  <reverse_enabled:u8>
  <reverse_speed_dkmh:u16>
  <reverse_current_dA:u16>
  <persist_pending:u8>
```

Result codes:

```text
0 OK
1 BAD_LENGTH
2 BAD_VERSION
3 OUT_OF_RANGE
4 ORDER_INVALID
5 VEHICLE_MOVING
6 THROTTLE_NOT_RELEASED
7 REVERSE_ACTIVE
8 STORAGE_ERROR
9 UNSUPPORTED_HARDWARE
```

### 4.6 RIDE_STATUS

Được trả sau SELECT và có thể được poll cùng cadence DASH 200 ms:

```text
56 50 89
  <config_revision:u16>
  <current_profile:u8>
  <active_speed_dkmh:u16>
  <direction_state:i8>
  <reverse_button:u8>
  <reverse_armed:u8>
  <persist_pending:u8>
  <fault_reason:u8>
```

Backend P4 cần cung cấp snapshot thread-safe và epoch giống `vlp_model_t`.

## 5. P4 backend API cần cung cấp cho FE

Đặt trong `components/vesc_can/include/vesc_can/vesc_lisp_panel.h` hoặc module
`vesc_ride_mode.{c,h}` riêng nếu tách ra rõ hơn:

```c
bool vesc_ride_mode_get_config(vesc_ride_config_t *out);
bool vesc_ride_mode_get_status(vesc_ride_status_t *out);
bool vesc_ride_mode_request_config(void);
bool vesc_ride_mode_set_config(const vesc_ride_config_t *cfg,
                               uint16_t *seq_out);
bool vesc_ride_mode_select(uint8_t profile, uint16_t *seq_out);
void vesc_ride_mode_set_screen_active(bool active);
```

Yêu cầu implementation:

- UI thread chỉ enqueue; CAN poll task thực hiện gửi.
- Mỗi request có `seq`; bỏ response cũ/không khớp.
- GET retry có timeout hữu hạn; không poll config liên tục khi screen đóng.
- STATUS có thể đi cùng poll dashboard 200 ms.
- Không block LVGL task chờ CAN.
- Parser phải kiểm tra length trước mọi lần đọc buffer.
- Giữ tương thích khi Lisp cũ chưa biết `0x07..0x09`: API trả unavailable/timeout,
  FE hiển thị `Ride mode backend unavailable`.

## 6. Lưu cấu hình trong Lisp EEPROM

Không gọi `conf-store` cho thay đổi mode. Dùng `eeprom-store-i` với fixed-point.
Địa chỉ 0 đã dành cho beep volume; dành block mới từ 16:

```text
16 format magic/version (ghi cuối)
17 config revision
18 checksum
20 mode0 speed_dkmh
21 mode0 current_permille
22 mode1 speed_dkmh
23 mode1 current_permille
24 mode2 speed_dkmh
25 mode2 current_permille
26 reverse_enabled
27 reverse_speed_dkmh
28 reverse_current_dA
```

Luồng lưu:

1. validate toàn bộ SET;
2. copy toàn bộ sang biến runtime;
3. tăng revision và áp dụng profile hiện hành;
4. đặt `persist_pending = 1`;
5. worker ghi các field, checksum rồi magic/version cuối cùng;
6. clear pending sau khi ghi xong;
7. shutdown handler flush nếu vẫn pending.

Boot đọc magic/version/checksum và validate bounds. Bất kỳ lỗi nào phải dùng
toàn bộ default, không trộn field EEPROM lỗi với default. Active mode không lưu:
mọi lần boot luôn vào mode 0 để tránh khởi động ở full-power mode.

## 7. Áp dụng profile

Thay hard-code trong `apply-profile` bằng lookup từ data model:

```text
conf-set 'max-speed              = speed_dkmh / 10 / 3.6
conf-set 'l-current-max-scale    = current_permille / 1000
```

Khi cấu hình reverse thay đổi:

```text
conf-set 'min-speed = reverse_speed_dkmh / 10 / 3.6
```

`min-speed` nhận độ lớn m/s; firmware VESC tự lưu thành `l_min_erpm` âm. Không
ghi flash motor config cho mỗi profile. Việc giới hạn speed vẫn là soft current
taper, không phải speed PID và không tự phanh xe khi xuống dốc.

## 8. Nút lùi vật lý

Phương án ưu tiên khi phần cứng hỗ trợ và PPM đang rảnh:

```text
ESC PPM signal ---- normally-open momentary button ---- ESC GND
```

Lisp:

```lisp
(gpio-configure 'pin-ppm 'pin-mode-in-pu)
```

Nút active-low. Không nối vào 5 V, pack voltage hoặc ground ngoài ESC. PPM App
trong VESC Tool phải là Off. Không triển khai bằng RX/TX vì hai chân đó đang có
chức năng. Nếu target không expose `pin-ppm`, reverse phải báo
`UNSUPPORTED_HARDWARE` và giữ disabled; không tự chọn một chân khác.

Claude chưa được hard-code số chân connector. Cần model ESC/schematic hoặc ảnh
giắc rõ ràng để map tên `PPM`/`GND` sang pin vật lý.

## 9. Reverse state machine bắt buộc

```text
FORWARD
  -> R_REQUESTED       R được giữ, đã từng thấy R nhả sau boot
  -> R_ARMED           speed <= 0.3 km/h, ga <= 5%, phanh xác nhận >= 200 ms
  -> REVERSE_ACTIVE    vẫn giữ R, đã nhả phanh, ga > 5%
  -> REVERSE_COAST     R nhả hoặc có interlock
  -> FORWARD           speed <= 0.3 km/h và ga đã nhả
```

Quy tắc:

- Sau boot phải quan sát nút R ở trạng thái nhả ít nhất một lần. Nút chập xuống
  GND từ lúc boot không được arm reverse.
- Nhấn R khi xe đang tiến chỉ tạo interlock/coast, tuyệt đối không phát dòng âm.
- Khi bắt đầu request reverse: hủy cruise, đặt PAS setpoint về 0 và release
  nguồn PAS lock.
- Trong reverse: cruise và PAS luôn bị bỏ qua.
- Phanh có ưu tiên cao hơn reverse throttle.
- Nhả R: lập tức ramp dòng về 0; không cho ga tiến trở lại khi xe còn đang lăn
  lùi hoặc tay ga chưa nhả.
- Không tự phanh chỉ vì người dùng nhả R; người lái dùng phanh. Interlock chỉ
  cấm mô-men sai chiều.
- Debounce GPIO 30..50 ms và motor loop vẫn chạy 100 Hz.
- Reverse không được là toggle lưu trạng thái; luôn là hold-to-run.

Thứ tự arbiter mới:

```text
master off > brake > direction interlock/reverse > forward throttle
           > cruise > PAS > coast
```

## 10. Lệnh motor khi lùi

Không tái sử dụng `set-current-rel` dương. Tạo ramp riêng `reverse-rel` và phát
dòng âm được giới hạn rõ ràng:

```text
reverse_limit = min(config_reverse_A,
                    abs(conf-get 'l-current-min),
                    conf-get 'l-current-max)
command = -reverse_rel * reverse_limit
set-current command 0.2
```

Không thay `l-current-min-scale` theo mode vì nó còn ảnh hưởng phanh tái sinh.
Firmware vẫn là lớp clamp cuối cho giới hạn nhiệt, motor và battery.

## 11. Multi-ESC

Phase đầu chỉ cho phép reverse khi một VESC duy nhất chịu trách nhiệm truyền
động. Nếu xe có nhiều ESC/motor cùng truyền động, phải áp dụng speed config và
lệnh dòng âm cho tất cả node tham gia một cách đồng bộ. Nếu chưa implement sync,
backend phải disable reverse thay vì chỉ đảo một motor.

## 12. Test acceptance cho Claude

### Host/static

- Lisp lint/syntax pass; mọi mutable variable nằm trước `@const-start`.
- Control/message IDs không trùng.
- Buffer length được kiểm tra trước parse.
- C parser test có: valid packet, short packet, bad version, invalid mode,
  stale sequence và unknown message.
- Existing VESC/BMS/PAS tests không regress.

### Simulator

- GET trả ba default mode và reverse config.
- SET hợp lệ trả ACK cùng revision mới.
- SET ngoài range/reversed ordering bị reject và snapshot không đổi.
- Dashboard/status đổi đúng profile và tốc độ đang áp dụng.
- Lisp cũ/timeout được FE nhìn thấy là unavailable, không treo UI.

### Bench, bánh xe nhấc khỏi mặt đất

1. Boot với nút R nhả: mode 0 được áp dụng.
2. Boot với nút R giữ/chập GND: reverse không arm.
3. Nhấn R khi bánh đang quay tiến: không có dòng âm.
4. Đứng yên, ga nhả, giữ R và xác nhận phanh: reverse arm.
5. Giữ R, nhả phanh, tăng ga chậm: bánh quay lùi, dòng <= cấu hình.
6. Nhả R khi đang lùi: dòng về 0; ga tiến bị khóa đến khi dừng và nhả ga.
7. Bóp phanh: brake luôn thắng reverse throttle.
8. Cruise/PAS không thể tạo torque trong reverse.
9. Reverse chạm 3 km/h thì ESC taper dòng theo `min-speed`.
10. Power-cycle sau Save: config còn nguyên nhưng active mode về mode 0.

Chỉ thử xe trên mặt đất sau khi mười case bench pass. Bắt đầu ở 3 km/h và 7 A.

## 13. Files dự kiến Claude thay đổi

- `lisp/main.lisp`
- `lisp/README.md`
- `components/vesc_can/include/vesc_can/vesc_lisp_panel.h`
- `components/vesc_can/vesc_lisp_panel.c`
- test transport/parser tương ứng
- `COLLABORATION_LOG.md`

Không sửa `Super_VESC_Display/generated/`. Không chạm các file BMS đang dirty
nếu không cần cho contract này. FE screen/button sẽ được Codex nối sau khi BE
API và packet contract ổn định.
