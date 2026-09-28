# RC2-diag2 — sửa khởi tạo heartbeat nút lùi RX

Dùng với P4 JC4880 RC2 hiện tại và VESC 6.05. Đây là gói Lisp sửa lỗi khởi tạo, giữ chẩn đoán để xác nhận trên hardware. Không cần nạp lại P4; không có binary P4 mới trong gói.

## Bằng chứng và thay đổi

Kết quả hardware của diag1:

```text
(5 12 5 18288522u32 0.000800f32 1828.852173f32 1828.852417f32)
```

Mã 5 = watchdog RX; lỗi 12 = input fault; safety 5 = FAULT. TX mới cập nhật 0,8 ms, nhưng RX và motor có tuổi gần bằng uptime của ESC (khoảng 1.828,85 giây). Điều này phù hợp với lỗi khởi tạo: `rv-hw-ok` đã lên 1 trong khi `rv-seen` vẫn bằng 0, trước lần motor hoàn tất đầu tiên. Không phải bằng chứng RX đã treo suốt 30 phút.

Bản trước báo RX sẵn sàng ngay sau `gpio-configure`, trước `gpio-read` và trước cập nhật heartbeat. Motor có thể chạy xen vào, lấy tuổi của timestamp 0, rồi khóa lỗi. Sau đó các luồng chạy bình thường nhưng lỗi vẫn giữ, khiến P4 hiện `-` và từ chối chuyển mode.

Bản này chỉ công bố `rv-hw-ok=1` cùng heartbeat trong một khối `atomic` nhỏ **sau lần đọc RX thành công**. Lỗi đọc đầu tiên giữ RX chưa sẵn sàng, không cho lùi. Khi lùi được bật, lỗi/trễ sau khi đã có mẫu vẫn chịu watchdog 100 ms. Giữ kiểm tra ga, phanh, nhả nút, PARK và chốt lỗi; không tự xóa lỗi. Xem [diff](RX_STARTUP_FIX.diff).

## Thử trên hardware

1. Xe đứng yên, kê bánh chủ động an toàn, thả ga. Dùng VESC Tool mở [main.lisp](main.lisp), Upload/Run vào đúng ESC. Giữ cấu hình đang dùng, gồm lựa chọn cho phép lùi, để kiểm tra đúng lỗi cũ.
2. Chờ khởi động. Kỳ vọng dashboard `P`; chưa bấm nút thoát PARK. Ghi nhận nếu vẫn có dấu `-` trước khi mở REPL.
3. Đọc và gửi lại kết quả:

```lisp
(list diag-first safety-fault (ride-safety-state)
      diag-at diag-tx-age diag-rx-age diag-motor-age)
```

Trong PARK bình thường, ba giá trị đầu dự kiến `(0 0 0 ...)`. `diag-first=0` nghĩa là chưa ghi nhánh lỗi; các tuổi chẩn đoán bằng 0 là bình thường vì chưa chụp lỗi. Nếu có lỗi, không Run/restart trước khi đọc vì sẽ mất bản ghi.

Mã chẩn đoán: 1 = native ADC lúc nạp; 2 = native ADC lúc chạy; 3 = ADC ngoài dải; 4 = TX quá hạn; 5 = RX quá hạn khi lùi bật; 6 = motor quá hạn. Tuổi là số đo tại lúc chụp sau dừng dòng, không phải tuổi hiện tại. REPL có thể tạm dừng evaluator nên cần ghi nhận lỗi xuất hiện trước hay sau lệnh đọc.

Kết quả host được ghi tại [báo cáo kiểm chứng](test-results/VERIFICATION.md). Feedback hardware sau khi nạp diag2: `(0 0 0 0 0.000000f32 0.000000f32 0.000000f32)` — PARK, không lỗi và chưa ghi nhánh lỗi trong lần chạy này. Đây là xác nhận khởi động lần thử đó; thao tác chọn mode, thoát PARK, lùi và độ ổn định qua nhiều lần khởi động còn cần kiểm tra. Phạm vi thay đổi này không phải bản sửa M-TEMP hay UI P4.

