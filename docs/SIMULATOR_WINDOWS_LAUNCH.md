# Simulator Windows: cửa sổ chạy nhưng người dùng không thấy

## Ghi nhận ngày 2026-09-18

- Lần chạy trong sandbox: tiến trình `simulator.exe` tồn tại, có title
  `Simulator (C/C++)`, window handle khác 0 và `Responding=True`, nhưng người
  dùng không thấy cửa sổ trên desktop.
- Mở lại ngoài sandbox với `-WindowStyle Normal`: người dùng xác nhận UI đã hiện.
- Đây là bằng chứng về vấn đề môi trường khởi chạy/hiển thị, chưa phải xác minh
  cơ chế Windows desktop isolation cụ thể. Không kết luận LVGL bị lỗi render.

## Quy tắc cho các lần chạy sau

1. Khi người dùng yêu cầu mở simulation để thao tác, **không launch trong
   sandbox**. Dùng cơ chế `require_escalated` của công cụ và tuân thủ phê duyệt;
   nếu không được phép, báo lại thay vì tự tìm cách vượt quyền.
2. Đặt `SDL_VIDEODRIVER=windows`, không để kế thừa `dummy` từ headless test.
   Dùng `-WindowStyle Normal`, không dùng `Hidden`.
3. Dùng đúng executable và working directory dưới shared repository.
4. Kiểm tra cửa sổ, và xác nhận bằng quan sát UI hoặc phản hồi người dùng.
   Chỉ có PID, window handle hoặc `Responding=True` chưa chứng minh UI đã hiện.
5. Nếu cần dừng bản cũ, xác minh PID và executable path trước; không kill tất cả
   tiến trình cùng tên. Không báo đã dừng nếu thao tác dừng trả lỗi.

Lệnh PowerShell dưới đây chạy từ repository root, **ngoài sandbox**:

```powershell
$simDir = (Resolve-Path 'Super_VESC_Display/lvgl-simulator').Path
$simExe = Join-Path $simDir 'build/bin/simulator.exe'
$env:SDL_VIDEODRIVER = 'windows'
Start-Process -FilePath $simExe -WorkingDirectory $simDir `
    -ArgumentList '--gear-preview' -WindowStyle Normal -PassThru
```

`--gear-preview` mở dashboard với các nút giả lập P/1/2/3/R/Wait/Stale.
Bỏ argument để chạy mặc định; dùng `--bms-preview` để xem BMS.
Executable cần các DLL đi kèm; nếu thiếu runtime, dùng môi trường MinGW32
đã thiết lập của dự án. Không sửa firmware chỉ để xử lý cửa sổ desktop bị ẩn.

Build và headless `--gear-self-test` vẫn có thể chạy trong sandbox với
`SDL_VIDEODRIVER=dummy`; kết quả đó không xác nhận cửa sổ tương tác hiển thị.
