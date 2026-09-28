# Trạng thái hardware khi tạo checkpoint — 2026-09-28

Nhánh: `fix/gear-circle-park-reverse`.
P4: JC4880 RC2. Lisp đang thử: [RC2-diag2](../release/jc4880-v1.3.8-rc2-diag2-2026-09-28/main.lisp). VESC 6.05.

## Xác nhận mới nhất từ người dùng

Các mode đã chạy, **trừ mode R**. Giữ nguyên trạng thái này làm checkpoint để tiếp tục điều tra.

Mô tả nguyên văn lỗi còn lại:

> Bóp phanh, nhấn giữ nút R, rồi nhả phanh, đèn báo R trên ESC vẫn sáng.

Trước đó người dùng gọi đèn này là đèn báo phanh; phản hồi mới nhất gọi là **đèn báo R trên ESC**. Chưa xác định chính xác LED/chức năng báo hiệu của bo. Không kết luận từ việc đèn còn sáng rằng motor đang nhận lệnh phanh. Chưa xác nhận chạy lùi thực tế hoặc nguyên nhân lỗi R.

## Bằng chứng đã có

- Lỗi startup RX công bố sẵn sàng trước heartbeat đầu tiên đã được tái hiện và sửa trong diag2. Sau nạp, người dùng đọc được PARK, lỗi 0, không có mã chẩn đoán.
- Mẫu ban đầu giữ lùi/phanh: RX chưa được nhận; sau đó RX đã nhận được.
- Sau nhả phanh có mẫu: ADC phanh 0, ramp phanh 0, RX nhấn, chưa armed, direction 0, không lỗi, motor heartbeat mới. Đây là INTERLOCK, không chứng minh kẹt lệnh phanh.
- Mẫu giữ đồng thời RX và phanh sau đó: `(1 1.000000f32 20 1 0 0.000025f32 0.000000f32 0)` theo thứ tự RX, ADC phanh, ticks, armed, direction, speed, throttle, PARK. Mẫu này đạt REVERSE_READY, nhưng chưa có snapshot sau nhả phanh của cùng chuỗi thao tác để xác nhận chuyển sang chiều lùi.

## Kiểm chứng phần mềm

- LispBM VESC 6.05 trên host, heap 2464: **156/156 PASS**, gồm chẩn đoán, startup RX, luồng R và ưu tiên/nhả phanh. API ESC giả lập; không chứng minh motor thật đã chạy lùi.
- [Log cuối](diagnostics/reverse-brake-diag2-2026-09-28/runtime-605-v2.txt).
- [Phân tích phanh/R](REVERSE_BRAKE_DIAG2_2026-09-28.md), [phân tích RX](REVERSE_INPUT_DIAG2_2026-09-28.md).
- Kiểm tra UI/CAN và build RC2 trước đó được lưu trong [báo cáo RC2](RC2_RERUN_2026-09-28.md). Không build lại P4 trong bước commit.

## Tiếp tục từ checkpoint

Xác định model/board ESC và đèn báo đang được mô tả. Lấy trạng thái sau nhả phanh của cùng lần đã REVERSE_READY, khi vẫn giữ nút R và thả ga:

```lisp
(list (get-adc-decoded 1) brk-rel rv-btn rv-armed rv-dir
      (ride-safety-state) (secs-since motor-seen))
```

Bản này lưu tiến độ, **không đánh dấu mode R đã sửa xong**. Giữ nguyên các chốt an toàn; không tự đổi cực tính đầu vào hoặc bỏ ưu tiên phanh.
