# Test tái hiện và kiểm tra hardware — v1.3.8-rc1

## 1. Test tự động đã chạy

Chạy từ root repo, cần Python và GCC host. LispBM runtime hiện dùng MinGW **32-bit**.
Lần đầu runner runtime tải source official bất biến rồi kiểm tra Git blob SHA;
không tải/nạp code vào ESC. `<host-gcc>` là compiler trên máy; tại máy build dùng
MinGW32 GCC. Fixture và log được lưu để tái hiện độc lập.

```text
python scripts/test_lisp_panel_ui.py --cc <host-gcc> --baseline-ref 7c77f2b
python scripts/test_lisp_panel_ui.py --cc <host-gcc>
python scripts/test_lisp_safety.py --source release/jc4880-v1.3.7-2026-09-19-7c77f2b/main.lisp
python scripts/test_lisp_safety.py
python scripts/test_lisp_runtime.py --cc <mingw32-gcc> --source release/jc4880-v1.3.7-2026-09-19-7c77f2b/main.lisp
python scripts/test_lisp_runtime.py --cc <mingw32-gcc>
python scripts/test_vesc_telemetry.py --cc <host-gcc> --baseline-ref 7c77f2b
python scripts/test_vesc_telemetry.py --cc <host-gcc>
python scripts/test_target_polling.py --cc <host-gcc>
```

Các lệnh baseline trả exit 1 theo mong đợi; bản hiện tại trả exit 0.
Chi tiết fixture [UI](../../scripts/tests/lisp_panel_ui/README.md),
[telemetry](../../scripts/tests/vesc_telemetry/README.md),
[LispBM cases](../../scripts/tests/lisp_runtime/cases.lisp),
[Lisp branch](../../scripts/test_lisp_safety.py).

| ID | Điều kiện/bước tái hiện | Kết quả bắt buộc | Cũ → mới |
|---|---|---|---|
| UI-01 | Bắt đầu chạm dashboard, yêu cầu mở async khi vẫn giữ; nhả ngoài drawer | Menu còn mở, polling hoạt động, không gửi action | FAIL → PASS |
| UI-02 | Cùng lượt mở, nhả ở vùng mode bên trong | Menu còn mở, không vô tình chọn mode | FAIL → PASS |
| UI-03 | Kết thúc lượt mở rồi chạm ngoài bằng lượt mới | Đóng đúng một lần, hết overlay sau animation | FAIL → PASS |
| UI-04 | Nhấn Mode 2 → 3 → 1 → Park qua pointer | Gửi ID 11/12/10/6, đúng value; state refresh không tự gửi action | PASS → PASS |
| UI-05/06 | Chưa có descriptor, hoặc descriptor đến muộn sau nhả | Chờ Loading rồi dựng đúng controls, không tự đóng | FAIL → PASS |
| UI-07/08 | Mở khi ở Settings; hoặc rời dashboard khi menu mở | Không mở ngoài dashboard; rời screen thì dọn menu | PASS → PASS |
| UI-09 | Đóng rồi yêu cầu mở lại giữa animation | Không overlay mồ côi, callback cũ không xóa drawer mới | FAIL → PASS |
| L-01 | Fixture native ADC Control Type NONE, ga idle, full script startup | Motor/TX/RX worker sống, trap/event thread chạy, không input fault | PASS → PASS |
| L-02 | Trong PARK, event chọn Mode 2/3; sau đó ga >0 | Mode đổi, STATE reply đúng; PARK vẫn khóa dòng motor | PASS → PASS |
| L-03 | Thoát PARK ở idle rồi nhấn/nhả TX có debounce | Chỉ chuyển mode một lần khi nhả | PASS → PASS |
| L-04 | Fixture báo ADC range không hợp lệ | Chốt fault 12, vào PARK, dòng 0, từ chối mode; phục hồi ADC không tự xóa fault | PASS → PASS |
| L-05 | Sau L-04, poll safety/legacy/sequenced status | State FAULT kèm result 12, không result OK=0 | FAIL → PASS |
| L-06 | Native control không hỗ trợ/bật sai, hoặc worker mất/quá hạn | Poll báo nguyên nhân 9 hoặc 12; giữ chốt an toàn | FAIL → PASS (branch) |
| T-01 | Nhiệt −99.9/2/1/−17, thay riêng ADC 0/50/100% | Decoder nhận đúng nhiệt; ADC không ghi nhiệt | PASS → PASS |
| T-02 | Selective mask chứa nhiệt nhưng payload thiếu | Không đổi giá trị hoặc timestamp, stale không thành fresh | FAIL → PASS |
| T-03 | Selective đủ nhiệt nhưng thiếu trường cuối | Không commit một phần | FAIL → PASS |
| T-04 | Thiếu 2 byte current trước duty | Không đọc current còn dư thành duty | FAIL → PASS |
| T-05 | Cắt packet 20 byte tại mọi độ dài 0…19, gửi lại packet đủ sau mỗi lần | Mỗi gói cụt bị loại nguyên tử; gói hợp lệ tiếp theo phục hồi | FAIL → PASS |
| T-06 | Gói 51 mọi bit đã biết, đổi nhiệt/current/time; gói 47 và future tail | Nhận thật giá trị/thời gian mới, vẫn tương thích gói legacy/tail | PASS → PASS |

Tổng: UI 9/9; branch Lisp 17/17; runtime LispBM 18/18; telemetry 14/14.
Kiểm tra bổ sung: wire safety 2.532, transport 53, gear 81 checks PASS;
target polling PASS cả active/paused. Các case healthy mode đã PASS ở release
cũ: không dùng kết quả đó để tuyên bố đã tìm ra fault ESC thực tế.

## 2. Test trên hardware — đang chờ người dùng

Ghi trước khi test: P4/version, ESC model/firmware, Sensor Type, application và
ADC Control Type, CAN target ID, chân núm, Tool nối trực tiếp hay qua P4.
Backup cấu hình và script hiện tại. Dùng bàn thử không cho xe tạo lực kéo ngoài
ý muốn; chuẩn bị ngắt nguồn, bắt đầu với ga nhả và phanh/reverse nhả.
Không cố tạo lỗi bằng cách rút dây ga đang cấp nguồn hoặc thay cấu hình motor
chỉ để làm test; lỗi ADC/native được tiêm tự động ở fixture host.

| ID | Thao tác | Kỳ vọng/điều cần ghi | Kết quả |
|---|---|---|---|
| HW-00 | Nạp đúng P4 RC và Lisp trong cùng folder, khởi động lại | Version 1.3.8-rc1; Lisp chạy không runtime error; ghi gear/safety ban đầu | PENDING |
| HW-01 | Vuốt mở giống video, nhả ngoài drawer; lặp 10 lần | Drawer giữ mở ít nhất 2 s sau nhả, không phát action ngoài ý muốn | PENDING |
| HW-02 | Vuốt và nhả trên vùng mode; lặp 10 lần | Giữ menu, lượt mở không tự chọn mode | PENDING |
| HW-03 | Sau mở, chạm vùng ngoài bằng lượt mới; mở/đóng nhanh | Đóng bình thường, không lớp mờ còn sót/khóa touch | PENDING |
| HW-04 | Trong PARK, ở ga 0 và trạng thái khỏe: chọn 1→2→3→1 | Toggle mode được ESC xác nhận; vẫn PARK, chọn mode không tự mở ga. Gear có thể tiếp tục P trong PARK | PENDING |
| HW-05 | Nút TX vật lý: đã nhả sau boot; bấm ngắn ở ga 0 | Trong PARK: thoát PARK; sau đó bấm ngắn mới chuyển mode khi nhả. Mỗi lần đúng một bước | PENDING |
| HW-06 | Đứng yên/ga 0, giữ phanh hợp lệ (ADC phanh >0,05), giữ TX ≥1,2 s rồi nhả | Vào PARK; nhả không chuyển thêm mode. Không có phanh: từ chối với BRAKE_REQUIRED=11, không kỳ vọng vào PARK | PENDING |
| HW-07 | PARK: thao tác ga trên bàn thử kiểm soát | Không tạo lực kéo; nhả ga trước thao tác thoát PARK | PENDING |
| HW-08 | Nếu mode vẫn bị từ chối | Ghi `safety-fault`, `rm-fault`, `motor-live`, `rv-dir`, lỗi Lisp runtime; poll phải có result tương ứng. Không ép xóa fault | PENDING |
| HW-09 | Mở Settings, quay dashboard, mở lại drawer | Settings thao tác bình thường; menu không tồn tại trên screen khác | PENDING |
| HW-10 | Ở PARK, ga nghỉ → vị trí giữa → nhả, ghi đồng thời P4 và VESC Tool | Ghi ADC decoded/voltage, Motor Temp, C-TEMP và timestamp; không giả định nhiệt phải 0 chỉ vì không có cảm biến | PENDING |

HW-05/06 chỉ áp dụng nếu nút người dùng nói là nút TX đã nối đúng pin theo
custom ESC. Không suy pin ESC từ pinout JC4880. Nếu chỉ có nút trên màn hình,
ghi rõ loại nút; nút Park trong drawer là lệnh **vào PARK**, không phải toggle
mở ga hay chọn mode. Lệnh vào PARK từ nút màn hình cũng cần đứng yên, ga nhả
và phanh hợp lệ; đang PARK thì lệnh lặp lại không thay trạng thái. Control Type NONE là điều kiện hiện tại của Lisp; không
bypass interlock chỉ để test pass.

## 3. Cô lập M-TEMP sau khi nhận RC

Giữ P4 RC, ESC, wiring và cấu hình cố định; so ADC/nhiệt theo cùng các vị trí
núm với Lisp gốc đã chạy được và Lisp RC. **Lisp gốc không có PARK và khởi động
với `throttle-on=1`: motor có thể chạy ngay khi lên ga sau khi nạp bản gốc.**
Không áp dụng giả định bảo vệ PARK của HW-10 cho bản gốc. Chỉ quét ADC sau khi
đã xác nhận bàn thử ngăn được lực kéo ngoài ý muốn; nếu dùng chức năng Throttle
OFF có sẵn ở bản gốc thì phải kiểm tra nó đã có hiệu lực. Không coi trạng thái
cũ trên P4 là bằng chứng ga đang khóa. Ghi file/hash và restart từng lượt.
Lisp gốc thiếu safety/status/PARK mới nên gear hoặc PARK không phải tiêu chí
tương đương; so riêng ADC/nhiệt. Không đổi cả P4 và Lisp rồi kết luận do Lisp.

Đọc Sensor Type trước; không tự đổi Disabled trong quá trình A/B. Với firmware
có binding chính thức, lấy thêm điện áp TEMP_MOTOR bằng `get-adc` index 3, và
EXT/EXT2 bằng index 0/1 để so xu hướng với núm. Dùng phép đọc có sẵn trong VESC
Tool/terminal, không ghi override nhiệt hay sinh script điều khiển mới.

- Nếu Tool nối trực tiếp cũng đổi nhiệt, tập trung chuyển đổi/cấu hình/mapping
  ADC của ESC; cần đúng source board để xác định chân dùng chung/nhiễu.
- Nếu chỉ P4 đổi, cần log raw command 47/51 và CAN để so với Tool trực tiếp.
- Nếu nhiệt raw không đổi nhưng widget đổi, cần trace UI value/unit.
- Nếu chỉ gói cụt gây cập nhật sai, bộ decoder RC đã có test cho tình huống đó;
  vẫn cần packet capture để chứng minh nó xảy ra ngoài thực tế.

Không coi −99°C hay −17°C là nhiệt motor thật; không kết luận lỗi đã hết bằng
cách tắt/ẩn M-TEMP. Mục tiêu là xác định nguồn số đo và quan hệ với ga.

## 4. Mẫu feedback

```text
P4 board/version:
ESC model/firmware:
Lisp filename + SHA256:
Sensor Type / application / ADC Control Type:
Núm nối chân nào; nút mode là TX hay UI:
VESC Tool nối trực tiếp ESC hay qua P4:
HW-00…HW-10: PASS / FAIL / chưa thử
M-TEMP nghỉ / khi vặn / sau nhả (P4 và Tool):
ADC1 / ADC2 / TEMP_MOTOR voltage nếu đọc được:
Lisp runtime error / safety-fault / rm-fault / motor-live / rv-dir:
Video timestamp hoặc log của lượt lỗi:
```
