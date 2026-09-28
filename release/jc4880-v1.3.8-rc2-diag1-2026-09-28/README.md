# JC4880 v1.3.8-rc2-diag1 — chẩn đoán Lisp / VESC 6.05

Bản này ghi nhánh kích hoạt khóa lỗi khiến P4 hiện dấu `-`. Đây là **bản chẩn đoán**, chưa phải bản sửa nguyên nhân trên hardware. Dùng cùng firmware P4 RC2 đang có; gói không có binary P4 mới.

## Cách lấy kết quả

1. Thử khi xe đứng yên, bánh chủ động được kê an toàn và ga thả. Không thử khi đang chạy xe.
2. Trong VESC Tool kết nối đúng ESC, mở [main.lisp](main.lisp) của gói này và Upload/Run như RC2. Không dán toàn bộ chương trình vào REPL. Giữ cấu hình đang dùng để thu đúng lỗi.
3. Chờ dấu `-` xuất hiện trên P4. Ghi nhận lỗi đã xuất hiện trước khi đọc REPL hay chưa. Không restart/Run lại Lisp sau khi lỗi xuất hiện vì sẽ mất bản ghi.
4. Chạy một lần biểu thức chỉ đọc sau và gửi toàn bộ kết quả:

```lisp
(list diag-first safety-fault (ride-safety-state)
      diag-at diag-tx-age diag-rx-age diag-motor-age)
```

Thứ tự: **mã chẩn đoán, lỗi Lisp, trạng thái an toàn, thời điểm ghi (tick), tuổi TX (giây), tuổi RX (giây), tuổi motor (giây)**.

| Mã | Nhánh được ghi |
|---|---|
| 0 | Chưa ghi nhánh lỗi nào trong lần chạy này |
| 1 | ADC Control Type khác NONE hoặc đọc cấu hình thất bại lúc nạp Lisp |
| 2 | Kiểm tra ADC Control Type thất bại trong vòng motor |
| 3 | `app-adc-range-ok` trả về false |
| 4 | TX/Mode đã từng chạy nhưng heartbeat quá 100 ms |
| 5 | Lùi bật, RX đã cấu hình thành công nhưng heartbeat quá 100 ms |
| 6 | Supervisor thấy vòng motor đã từng chạy nhưng heartbeat quá 100 ms |

Bản ghi giữ nguyên sau khi đầu vào/luồng phục hồi. Mã 4/5/6 chỉ xác định watchdog nào kích hoạt; chưa phân biệt chết luồng, nghẽn lịch chạy hay tác động REPL. VESC Tool 6.05 tạm dừng evaluator khi nạp biểu thức REPL, nên cần ghi nhận lỗi xuất hiện trước hay sau lệnh đọc. Không dùng REPL để xóa lỗi hoặc ghi biến điều khiển.

Đây là **nguyên nhân được ghi đầu tiên**, không bảo đảm thứ tự tuyệt đối của hai lỗi đồng thời. Tuổi heartbeat được chụp sau lệnh dừng dòng, có thể khác số đo tại điều kiện phát hiện. Với mã 1, luồng chưa khởi động nên tuổi heartbeat chưa có ý nghĩa chẩn đoán.

## Phạm vi thay đổi

Dựa trên RC2 có SHA256 `bab5a803c38ca6b61b0b44d41d80a9b3df0c51ad870aa70c7f07e118d55acc6d`.
Xem [diff](RC2_DIAGNOSTIC.diff). Thêm 5 biến RAM và hàm ghi một lần trong `atomic`; không ghi EEPROM/CAN/log trong hàm chụp. Giữ thứ tự điều kiện, ngưỡng 100 ms, khóa ga/PAS, từ chối thoát PARK và giữ lỗi. Đường lỗi motor thêm lệnh dòng 0 trước chụp; arbiter vẫn ra dòng 0 như RC2. Không tự xóa lỗi hay mở khóa để che dấu `-`.

Đã đối chiếu API với VESC chính thức 6.05, bldc `a0d40e2c5a42c810888d8c379307e6b0a118a125`. `atomic` chỉ bọc kiểm tra/ghi biến số và đọc thời gian, không bọc trap, sleep hay I/O motor.

## Kiểm chứng

Xem [kết quả chạy](test-results/lisp-runtime-605.txt). Chạy tại gốc repository:

```powershell
python -B scripts/test_lisp_runtime.py --vesc-version 6.05 --heap-cells 2464 --source release/jc4880-v1.3.8-rc2-diag1-2026-09-28/main.lisp --fault-diagnostics --cc C:/msys64/mingw32/bin/gcc.exe
```

Interpreter LispBM 6.05 thật trên host 32-bit, heap 2464 cells; ESC ADC/GPIO/current/CAN là fixture. Kết quả host không chứng minh ADC, thời gian chạy hoặc motor thật trên ESC. Không thay đổi P4/UI trong bản này; không tuyên bố đã chạy lại simulation UI hay kiểm thử hardware.
