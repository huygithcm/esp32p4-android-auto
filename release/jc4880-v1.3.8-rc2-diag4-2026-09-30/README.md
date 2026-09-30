# JC4880 v1.3.8-rc2-diag4

Gói kiểm thử ngày 2026-09-30 cho ESP32-P4 JC4880 và VESC 6.05.

## Thay đổi chính

- Mode R cho phép nhập dòng yêu cầu `1..999 A`, bước `1 A`, giống Mode 1/2/3.
- Dòng R thực tế vẫn bị giới hạn bởi cấu hình VESC:
  `min(R requested, abs(l-current-min), l-current-max)`.
- Badge mode trên dashboard tăng từ 40 px lên 80 px, dùng font Montserrat 48.
- Cockpit và Classic Max đặt badge tại tâm `(220,280)`; Lamborghini dùng vùng
  trống tại `(183,354)`; Supermoto thay mode card cũ bằng badge tại `(718,399)`.
  Cả bốn theme giữ badge trong khung `800x480` và tránh các nhãn lân cận.
- Giá trị R cũ như `7,5 A` vẫn hiển thị đúng; nút `+` snap lên `8 A`, nút `-`
  snap xuống `7 A`, tránh hiển thị/lưu lệch nhau.
- Giữ nguyên sửa lỗi diag3: nhả phanh khi đã arm R sẽ thoát
  `CONTROL_MODE_CURRENT_BRAKE` trước khi cấp dòng lùi.

Thay đổi này không tự sửa `l-current-min`, `l-current-max` hoặc các giới hạn
motor khác trên ESC.

Diag4 không thay đổi đường dữ liệu M-TEMP. Dashboard tiếp tục hiển thị nhiệt độ
motor do VESC gửi. Với xe không có cảm biến nhiệt motor, nếu M-TEMP trong VESC
Tool cũng thay đổi theo ga thì cần kiểm tra cấu hình/telemetry phía VESC; không
thể kết luận đó là lỗi mapping UI từ kết quả host.

## File nạp

- `esp32p4-jc4880-v1.3.8-rc2-diag4-ota.bin`: firmware OTA cho JC4880.
- `merged.bin`: image nạp đầy đủ tại offset `0x0` bằng cáp USB.
- `main.lisp`: upload riêng lên VESC 6.05 bằng VESC Tool rồi chạy script.

Firmware P4 và Lisp trên ESC là hai phần riêng. Nạp firmware P4 không tự cập
nhật Lisp trên VESC.

## Trình tự bench an toàn

1. Kê bánh chủ động khỏi mặt đất, thả hoàn toàn ga và phanh.
2. Nạp firmware P4 phù hợp, sau đó upload/run `main.lisp` trên đúng VESC 6.05.
3. Xác nhận dashboard hiện `P`, thoát PARK và chọn lần lượt Mode 1/2/3.
4. Giữ nút R và bóp phanh ít nhất 0,3 s khi xe đứng yên, ga bằng 0.
5. Vẫn giữ R, nhả hoàn toàn phanh và chờ ít nhất 1 s. Dashboard phải hiện `R`
   và VESC Tool phải thoát trạng thái phanh.
6. Chỉ tăng ga rất nhẹ sau khi bước 5 đạt; xác nhận chiều quay trước khi tăng
   dòng yêu cầu R.
7. Chạy các ca chi tiết trong `TEST_CASES.md` và ghi lại kết quả hardware.

## Kết quả kiểm tra trong gói

- Lisp source/safety: `17/17` PASS.
- LispBM từ VESC 6.05, heap 2464: `88/88` PASS.
- Ride mode parser: PASS, gồm R `9990 dA` hợp lệ và `9991 dA` bị từ chối.
- Ride safety: `2532/2532` PASS.
- Ride transport: `53/53` PASS.
- Ride gear: `81/81` PASS.
- LVGL drawer: `9/9` PASS.
- Gear simulator: Cockpit, Classic Max, Lamborghini và Supermoto PASS với
  badge 80 px nằm đúng vùng đã kiểm tra; migrate editor `7,5 A` PASS.
- Firmware image: checksum và validation hash hợp lệ; OTA còn 806.464 byte
  trong slot 5.242.880 byte.

Các kiểm tra LispBM dùng ESC API fixture. Simulator/host/build không chứng minh
CAN transceiver, cảm ứng, PWM, LED, mô-men, chiều quay hoặc việc thoát phanh trên
hardware thật. Kết quả cuối cùng cần xác nhận trên đúng JC4880 và VESC 6.05.

`S3_PORT_HANDOFF.md` mô tả contract để port chọn lọc sang ESP32-S3. Không merge
nguyên nhánh P4 sang S3 vì BSP, pin CAN, display và touch khác nhau.
