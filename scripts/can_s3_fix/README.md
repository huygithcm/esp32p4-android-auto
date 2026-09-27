# Script sửa giao tiếp CAN ESP32-S3 — chờ xác nhận trước khi build

Đích: bản clone `vesc-display-s3`, nhánh `esp32s3-port` tại `d311601`,
bao gồm các sửa đổi BSP/main đã có khi kiểm tra. Script dùng SHA256 để từ chối
ghi đè nếu cộng tác viên đã sửa tiếp. Không dùng reset, checkout hoặc stash.

## Phạm vi bản sửa

- CAN: từ chối frame thiếu byte/RTR, giữ các mảnh cùng một lần gửi không xen
  kẽ, dừng khi gửi lỗi, sao chép frame trước khi trả slot RX về producer.
- Định tuyến: cập nhật Ride Mode khi đổi ESC ID; hủy lệnh đang chờ của target
  cũ, xóa cache và kiểm tra sequence của phản hồi config. Phản hồi Ride Mode
  từ ESC khác không cập nhật backend; cầu BLE vẫn nhận các phản hồi đó.
- Tạm dừng cả polling Ride Mode trong luồng upload/read Lisp hiện có.
- BSP: giữ USB trong lúc khởi tạo LCD/touch; chỉ chọn CAN khi khởi động TWAI.
- Giữ nguyên UI, cấu trúc format1 và script Lisp đang đi kèm S3. Không đưa
  format2/PARK của P4 vào UI S3 cũ.

`changes.patch` là diff để review. `payload/` là nội dung sau sửa;
`manifest.json` ghi hash trước/sau và 64 file UI/Lisp được bảo vệ.
Chỉ 7 file firmware nằm trong danh sách sửa; các file còn lại là test host.

## Kiểm tra, chưa áp dụng

Chạy từ repo P4, thay `<S3-clone>` bằng đường dẫn clone thực tế:

```powershell
python scripts/can_s3_fix/apply_can_fix.py --root "<S3-clone>"
```

Mặc định chỉ đọc và kiểm tra. Không chạy test, build hoặc flash.

## Áp dụng nguồn, không build

```powershell
python scripts/can_s3_fix/apply_can_fix.py --root "<S3-clone>" --apply
```

Script sao lưu trước khi ghi vào `build/can_fix_backups/<timestamp>/` trong
clone S3, kiểm tra từng file ngay trước khi ghi và cập nhật collaboration log.
Nếu có thay đổi ngoài snapshot, script dừng để review lại, không tự ghi đè.
Đóng các thao tác ghi đồng thời vào các file này khi áp dụng.

**Theo yêu cầu người dùng: chỉ build sau khi người dùng xác nhận.** Script áp
dụng không có lệnh build/flash hoặc tự chạy test. Hiện chưa áp dụng vào clone.

## Kiểm chứng đã thực hiện trước yêu cầu tạm dừng build

- Transport CAN bản staged: 14.911 checks đạt với TWAI/FreeRTOS mock.
- Backend đổi target: 49 checks đạt trên bản staged đầu tiên. Sau đó đã giới
  hạn số lệnh mỗi vòng polling và kiểm tra pause giữa các lần gửi; chỉnh sửa
  cuối này mới review nguồn, chưa compile/test lại.
- Test seam main/BSP: tái hiện lỗi latch USB/CAN trên mã cũ, đạt trên bản staged
  cho mux, sáu callback target, loại phản hồi target cũ và giữ BLE forwarding.
- Chưa build firmware S3, flash hoặc đo giao tiếp CAN thật trong lượt này.
- Test đã đóng gói cần chạy lại sau khi áp dụng và được xác nhận compile:
  `python scripts/test_can_host.py --cc <host-gcc>` từ clone S3. Đường dẫn test
  đã được chuyển từ staging sang repo; chưa chạy lại bản đóng gói.

## Sau khi được xác nhận build

1. Áp dụng patch và chạy test host, sau đó build với ESP-IDF 5.5.3 cho `esp32s3`.
2. Xác nhận hash UI/Lisp giữ nguyên, firmware nằm đúng partition 3 MB.
3. Đối chứng CAN thật: đúng LCD7B, TX20/RX19, mux EXIO5, bitrate/ID theo cấu hình
   thực tế/NVS, emulator OFF; đọc telemetry/config từ cùng ESC dùng đối chứng P4.
4. Thử đổi target, đọc/upload Lisp, BLE NUS đồng thời và mất/kết nối lại CAN;
   đo timeout, CRC lỗi và bus-off/recovery. Không suy ra hoạt động motor từ test host.

## Giới hạn còn lại

- Backend S3 vẫn format1; không tương thích mặc định với Lisp format2 mới của P4.
- Khóa TX mới bảo vệ các mảnh khi enqueue, chưa độc quyền cả chu kỳ request/reply
  của mọi client. Ghép phản hồi đồng thời từ nhiều nguồn vẫn cần test tải thực tế.
- Tín hiệu hoàn tất CAN chưa khớp command/target của từng request; pause của các
  luồng upload đồng thời chưa phải cơ chế nhiều chủ sở hữu.
- Cập nhật latch expander chưa có mutex chung cho mọi phép read/modify/write;
  cần kiểm chứng chuyển mux đồng thời với thao tác SD/backlight trên board.
- Không thay đổi cơ chế an toàn motor/Lisp hay tuyên bố tương đương P/R P4.
