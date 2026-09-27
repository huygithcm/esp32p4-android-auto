# JC4880 — v1.3.7 — 2026-09-19

Nguồn firmware: commit `7c77f2b`, ESP32-P4 JC4880, flash 16 MB.

- `merged.bin`: nạp vào màn hình ESP32-P4 tại offset `0x0`.
- `main.lisp`: nạp riêng vào ESC VESC qua VESC Tool/Lisp editor.

Không dùng merged.bin cho ESP32-S3 hoặc board Waveshare.
Sao lưu setting trước khi flash merged (có vùng NVS/padding và OTA data).
Build và host test đã đạt; chưa xác nhận an toàn trên phần cứng thực tế.
Không nạp Lisp/flash khi xe có thể tạo lực kéo ngoài ý muốn.

Hai file được sao chép và đối chiếu SHA256 với bản gốc, không thay đổi nội dung.
