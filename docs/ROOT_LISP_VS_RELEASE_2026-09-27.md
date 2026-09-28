# Đối chiếu Lisp gốc với Lisp trong release JC4880

Ngày kiểm tra: 2026-09-27. Phần đối chiếu ban đầu bên dưới giữ nguyên phạm vi source/artifact.

**Cập nhật sau yêu cầu test/fix/build:** đã tái hiện và sửa lỗi drawer, báo lỗi Lisp và packet telemetry thiếu dữ liệu. Xem [video và regression](./M_TEMP_VIDEO_AND_REGRESSION_2026-09-27.md), cùng [gói RC/test hardware](../release/jc4880-v1.3.8-rc1-2026-09-27/README.md). Các câu “không build/implementation” bên dưới mô tả lượt đối chiếu ban đầu; Lisp canonical nay đã có bản sửa, còn Lisp gốc/release cũ được giữ nguyên. Chưa flash hoặc xác nhận hardware.

**Cập nhật theo xác nhận mới nhất của người dùng:** lỗi bắt đầu sau khi nạp Lisp mới lên ESC trong tổ hợp P4/release mới. Bảng mode mở được nhưng tự thu vào khi nhả tay; Motor Temperature trong VESC Tool cũng đổi theo ga; motor không gắn cảm biến nhiệt. Ghi nhận cũ về P4 mới + Lisp cũ không còn là ma trận chẩn đoán hiện tại. Xem [đối chiếu nguồn VESC và LVGL](./VESC_REFERENCE_FIRST_AUDIT_2026-09-27.md) cho các trích đoạn, đường sự kiện đóng bảng và phân biệt không có cảm biến với cấu hình Disabled.

## Danh tính hai bản

| Bản | File | Nguồn đã xác minh |
|---|---|---|
| Gốc, baseline trong yêu cầu | [main.lisp](../main.lisp) | 866 dòng; trùng `e7f0b16:lisp/main.lisp` sau chuẩn hóa CRLF/LF |
| Release mới nhất được đóng gói trong workspace | [main.lisp trong release](../release/jc4880-v1.3.7-2026-09-19-7c77f2b/main.lisp) | 1020 dòng; JC4880 v1.3.7, build 2026-09-19; trùng nguồn `7c77f2b` sau chuẩn hóa xuống dòng |

SHA256 byte gốc:

- Gốc: `9bd3764197f38f7989451c56d2960ceb12ba31460610e515e6d415548a9cb3b1`.
- Release: `4aa8c31bb64888a5421c5d96369899ca2ae29172d5d500a5a217e9f4d05616e6`.

Release trùng byte với [nguồn canonical](../lisp/main.lisp), [bản Drive staging](../release/drive-upload/ESP32-P4/releases/v1.3.7/7c77f2b/jc4880/main.lisp) và thành viên Lisp trong [ZIP staging](../release/drive-upload/esp32p4-jc4880-v1.3.7-7c77f2b-2026-09-19.zip). Diff trực tiếp: 290 dòng thêm, 136 dòng bỏ, bao gồm comment/cấu trúc. Đây là gói mới nhất tìm thấy trong workspace, không phải xác nhận mọi remote. Xem [manifest](../release/drive-upload/ESP32-P4/releases/v1.3.7/7c77f2b/jc4880/release.json).

Ghi nhận baseline `e7f0b16` chạy được là kết quả người dùng đã báo trước đây, không phải thử nghiệm phần cứng mới trong lượt này. Lần này xác minh được danh tính file. [Handoff trước](./P4_E7F0B16_COMPARISON.md) ghi rõ giới hạn. Lisp phải nạp riêng vào ESC; flash P4 không tự cập nhật Lisp ESC.

## Ga và ADC

Số dòng dưới đây tham chiếu hai file trực tiếp trong bảng trên.

| Nội dung | Gốc | Release | Hệ quả |
|---|---|---|---|
| Nguồn ADC | ADC0 ga, ADC1 phanh; 738-740 | Giữ nguyên; 873-875 | Không đổi kênh |
| Curve/ramp/current | Các hàm 558-609 | Giống cấu trúc tại 685-736 | Vẫn tick 10 ms, ngưỡng 0,05; ramp từ cấu hình ESC, giới hạn 0,05-5 s |
| Safe-start | Chờ thấy ga nhả một lần; 741-743 | Giữ nguyên; 876-878 | Đã có trước release |
| Boot | `throttle-on=1`; 19 | `throttle-on=0`, `park-on=1`; 16-17 | Chưa có lực kéo trước khi mở P |
| Native ADC | `app-disable-output 1500`; 736 | `app-disable-output -1` khi nạp và mỗi tick; 113, 864 | Từ tạm khóa 1,5 s sang khóa vô thời hạn |
| Cấu hình ADC | Không kiểm tra Control Type NONE | Kiểm tra `adc-ctrl-type=0`; 115-122, 865 | Phải lưu ADC Control Type NONE trên ESC; script không tự lưu |
| Guard mới | Không có các guard mới | `app-adc-range-ok`, heartbeat TX/RX, supervisor; 863-870, 926-933 | Có thể chốt lỗi và khóa ga |
| Ưu tiên | Master-off trước phanh; 748-763 | Fault, phanh, rồi PARK; 883-900 | P khỏe vẫn cho phanh chủ động; fault bỏ qua cả nhánh phanh Lisp |

**Phép tính ga không đổi; điều kiện cho phép chạy đổi.** Giữ cấu hình native ADC cũ với Control Type khác NONE là một điều kiện đủ để bản release khóa lực kéo theo source. Chưa đọc cấu hình ESC thực tế trong lượt này.

Release không tự xóa `safety-fault`: chỉ có các phép gán tại dòng 18, 122, 152. Sau khi lỗi chốt, sửa ADC trong lúc script chạy không tự mở lại ga; cần khởi động lại script với điều kiện đúng. TX heartbeat được kiểm tra sau `tx-live=1`; RX chỉ khi reverse bật và `rv-hw-ok=1`. Ngưỡng trễ >100 ms. Supervisor mỗi 20 ms xử lý motor loop đã từng sống rồi bị trễ >100 ms.

`app-adc-range-ok` tại dòng 866 không được trap ngay tại API. Đối chiếu bổ sung xác nhận binding này và các API cần thiết có trong cả ba snapshot official 6.05/6.06/7.00; không lấy “thiếu API” làm giả thuyết chính nếu ESC đúng các nguồn đó. Firmware custom thực tế chưa được xác định. Nếu API ném lỗi ở tick đầu thì `motor-live=0`, safety báo FAULT, còn supervisor chỉ xử lý loop đã từng live. Nguồn ADC official xác nhận vẫn decode trước cổng khóa output khi ADC app đang chạy; host test không chứng minh cấu hình/runtime trên ESC. VM chết hoàn toàn vẫn phụ thuộc native NONE và timeout ESC.

Bản gốc có thể trả quyền cho native ADC khoảng 1500 ms sau khi loop dừng. Vì vậy chỉ thấy ga chạy chưa tự chứng minh motor loop Lisp đang sống; cần phân biệt khi diễn giải A/B. Điều này không phủ nhận baseline người dùng đã xác nhận.

## PARK, mode, reverse và PAS

| Thao tác | Gốc | Release |
|---|---|---|
| Nút TX/MODE ngắn | Đổi mode ở cạnh nhấn sau debounce; 258-273 | Thực hiện khi nhả: đang P thì mở P, đang forward thì đổi mode; 321-349 |
| Giữ TX >=1 s | Không có PARK | Yêu cầu vào P; dừng và ga nhả từ lúc nhấn, kiểm tra lại lúc thực thi; phanh >0,05 lúc thực thi |
| Giữ nút từ boot | Chưa có guard mới | Phải thấy nhả ổn định trước khi nhận gesture |
| Vào P | Không có | `abs(speed)<=0,083 m/s` (~0,3 km/h), ga <=0,05, phanh >0,05; 133-149 |
| Thoát P | Không có | Dừng, ga nhả, không fault, motor loop khỏe, nút reverse nhả; 133-149 |
| Helper/panel bật ga | Đổi trực tiếp flag; 357-363 | ON trả mã 13, không mở P; OFF yêu cầu P có guard; 517-518 |
| Quick panel | Throttle, Beep, Beep Vol và ba mode; 314-338 | Park và ba mode; state chỉ ba radio mode; 405-424 |
| Chuyển mode | Có tone motor sau lần khởi tạo đầu | Bỏ tone, thêm guard không safety-fault; 193-202, 596-602 |
| Reverse mới arm | Chỉ kiểm tra chuyển động tiến | Đòi dừng theo trị tuyệt đối; nhánh riêng giữ reverse đã arm khi đang lùi; 743-802 |
| PAS | Non-zero hợp lệ khóa nguồn | Sau PARK/interlock cần zero mới từ chính nguồn đó, rồi non-zero trong cửa sổ 0,4 s; 493-511 |

PARK khóa lực kéo, không tự giữ phanh. Yêu cầu trạng thái đã đạt trả thành công trước guard. Long press bị từ chối vẫn được tiêu thụ; nhả nút không biến thành đổi mode.

Quick panel/helper cmd2 có thể đổi mode đang chọn khi P khỏe và `rv-dir=1`, vì hàm chọn mode không kiểm tra `park-on`; điều đó không mở P. Nhấn ngắn nút vật lý khi P chỉ mở P, không tăng mode.

Hai bản đều có mode theo **dòng tuyệt đối** mặc định 50/70/100 A, kẹp theo Motor Current Max thực, chỉ đổi `l-current-max-scale`, không đổi giới hạn tốc độ khi chọn mode. Cấu hình đã lưu có thể thay mặc định. Nếu ESC max là 70 A, trần mặc định là 50/70/70 A. Cả hai boot mode index 0.

Format cấu hình vẫn 2; EEPROM tag `0x524D02`, địa chỉ và load/store/checksum không đổi. Không lưu PARK hoặc current-profile. Reverse trên RX đã có từ bản gốc; cruise thực thi đã bỏ trước baseline dù gốc còn comment cũ.

Release trap việc đặt `min-speed` trước khi chấp nhận cấu hình reverse (248-261, 542-588): lỗi trả mã 9 thay vì để handler chết trước ACK. Boot không áp dụng được reverse đã lưu thì tắt reverse trong RAM và báo lỗi (365-369), tiếp tục panel/mode. Reverse-step chuyển từ RX thread vào motor loop; RX thread chỉ đọc/debounce/heartbeat.

Release còn chặn đọc helper buffer rỗng và sửa pattern event CAN SID (970-997). Tuy nhiên startup cả hai chỉ thấy enable data-rx/shutdown, không thấy enable event-can-sid; không khẳng định helper SID hoạt động thực tế chỉ từ handler.

## Dữ liệu Lisp gửi P4

| Giao dịch | Gốc | Release |
|---|---|---|
| DASH `0x04 -> 0x84` | 340-353 | 426-439, cùng nội dung hàm |
| Config `0x07/0x08 -> 0x87` | 286-296, format 2 | 377-387, cùng layout/format |
| Legacy status `0x0A -> 0x89` | 301-312 | 392-403, giữ nguyên |
| Sequenced status `0x0D -> 0x8D` | Không handler | Thêm 443-452, 642-644 |
| Safety `0x0B -> 0x8B` | Không handler | Thêm 465-477, 645-647 |
| PARK/ACK `0x0C -> 0x8C` | Không handler | Thêm 479-488, 648-650 |

DASH cả hai: magic `56 50`, opcode `84`, rồi bốn i32: `rv-dir*1000`, `rv-armed*1000`, `current-profile*1000`, `rm-effective-dA*1000`. Slot cuối là **trần dòng hiệu dụng**, không phải dòng đo tức thời. Không có ADC, điện áp ga, nhiệt độ hoặc trường PARK riêng trong DASH.

Phần DASH có nghĩa là 19 byte nhưng cả hai gửi toàn bộ `pbuf` 128 byte. Status mới có buffer riêng 19 byte; safety/ACK riêng 9 byte. Parser P4 yêu cầu 20/10 byte tính thêm command `COMM_CUSTOM_APP_DATA=36`; hai phía khớp nhau.

Status mới thêm sequence, vẫn chứa revision, profile, requested/effective/ESC-max current, direction, reverse button/armed, persist và fault. Safety chứa version 1, sequence, state, profile, result; state: 0=P, 1=forward, 2=reverse ready, 3=reverse active, 4=interlock, 5=fault. PARK dùng token từ safety query, ràng buộc nguồn và tuổi <1 s; token đã dùng chỉ trả kết quả cũ, không thực thi lại.

P4 commit `7c77f2b` poll status `0x0D` và safety, mỗi loại 200 ms. Legacy `0x89` không làm mới live status; gear dùng safety snapshot, thiếu/quá hạn thì hiện `-`. Vì vậy **P4 mới + Lisp gốc vẫn có config format 2 và DASH, nhưng thiếu live gear/safety/PARK**. Không có chuyển format1 sang format2 giữa hai Lisp này.

Tham chiếu: [ride backend](../components/vesc_can/vesc_ride_mode.c) tại commit release, các dòng 449-460, 513-535, 585-600; [parser](../components/vesc_can/vesc_ride_mode_parse.c); [UI updater](../main/vesc_ui_updater.c) tại commit release, 151-162; [gear mapping](../main/ride_gear_state.h).

Mỗi send đồng bộ có thể chờ 60 ms. Lisp gốc bỏ qua hai request mới nên có thể thêm hai lần chờ vào chu kỳ poll chung. Đây là ứng viên làm chậm dữ liệu, chưa chứng minh gây triệu chứng thực tế.

## ADC và M-TEMP là luồng riêng

| Dữ liệu | Command VESC | Đường đọc trên P4 |
|---|---|---|
| ADC decoded/voltage | 32 | [IO decoder](../components/vesc_can/vesc_io_data.c): ADC1 decoded/voltage, ADC2 decoded/voltage, mỗi trường i32/1e6 |
| Nhiệt MOS/motor | 47/51 SETUP | [RT decoder](../components/vesc_can/vesc_rt_data.c): i16/10 theo mask |
| Custom Lisp | 36 | [Lisp panel dispatcher](../components/vesc_can/vesc_lisp_panel.c) |

Diff ba component này từ `e7f0b16` tới `7c77f2b` rỗng. Không tìm thấy thay đổi source trong so sánh này biến ga/ADC thành M-TEMP. Điều đó không loại trừ lỗi traffic, dữ liệu nguồn ESC hoặc phần cứng; cần raw packet để phân định.

Người dùng đã làm rõ trigger là nạp Lisp mới; nhận định cũ “P4 mới + Lisp cũ cũng lỗi” đã bị thay thế. VESC Tool cũng đổi nhiệt và motor không có cảm biến: không quy riêng lỗi cho widget M-TEMP. Cần Sensor Type, đúng source board ESC và kiểu kết nối Tool để xác định nguồn giá trị. Nguồn VESC official dùng giá trị override nếu Disabled; nếu vẫn chọn loại cảm biến, nó vẫn chuyển đổi đầu vào nhiệt. Các lệnh ghi cấu hình trong hai Lisp không trực tiếp đổi Sensor Type hoặc override nhiệt.

## Kiểm chứng và giới hạn

Đã kiểm tra SHA256 bốn bản local, Git blobs sau chuẩn hóa xuống dòng, thành viên ZIP, diff trực tiếp và parse hai file thành 110/147 top-level forms. So sánh cấu trúc xác nhận curve/ramp/current, DASH/config/legacy status và EEPROM giữ nguyên. Ba agent đọc độc lập ga/ADC, PARK/mode và giao thức P4.

Không chạy LispBM, không build/flash, không chạy lại suite motor/transport và không đo CAN/hardware. Chỉ dùng parser từ [host test](../scripts/test_lisp_safety.py) để so sánh cấu trúc, không dùng kết quả host làm bằng chứng vận hành.

Kết luận: phép tính ga không đổi; ADC Control Type, PARK chưa mở hoặc fault đã chốt có thể trực tiếp khóa ga ở release. Fault có thể chặn chọn mode, còn PARK riêng nó không chặn mode trong panel. Thiếu status/safety mới làm mất live gear/PARK khi ghép P4 mới với Lisp gốc. Bảng thu vào khi nhả có đường sự kiện touch phù hợp trong source P4/LVGL; chưa chứng minh điều kiện thực tế hoặc vì sao thay Lisp kích hoạt. Chưa xác định nguyên nhân nhiệt thay đổi theo ga trên ESC.

Nếu thử tiếp, giữ nguyên P4/ESC/config, chỉ thay Lisp theo đúng trigger mới và ghi command 32/36/47/51, nhiệt trong Tool, safety/status, lỗi worker/runtime. Phải xác định motor loop Lisp còn sống, không chỉ dựa vào việc ga làm quay motor. Không sửa implementation hoặc build/flash trong lượt đối chiếu này.
