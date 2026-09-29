# RC2-diag3 — thoát trạng thái phanh khi chuyển sang R

Gói này chỉ thay Lisp trên VESC 6.05. Giữ nguyên firmware P4 JC4880 RC2 đang
chạy; không cần OTA hoặc flash lại P4.

## Lỗi hardware cần kiểm tra

Sau khi xe đã đạt `REVERSE_READY`, người dùng nhả tay phanh nhưng VESC Tool vẫn
nhận diện trạng thái phanh. Phanh hoạt động và nhả bình thường ở chiều tiến và
PARK, nên thay đổi chỉ tập trung vào chuyển tiếp phanh → R.

## Thay đổi

- Chỉ chuyển `rv-dir` sang `-1` và báo `REVERSE_ACTIVE` sau khi cả ADC phanh đã
  nhả và ramp `brk-rel` đã giảm xuống `<= 0.001`.
- Khi ramp phanh vừa kết thúc, phát `set-current 0` ngay trong cùng vòng điều
  khiển. VESC 6.05 xử lý `set-brake-rel 0` vẫn là
  `CONTROL_MODE_CURRENT_BRAKE`; lệnh current-zero thay thế trạng thái đó trước
  khi vòng Lisp có thể bị tạm dừng.
- Khi R đang giữ nhưng dòng lùi yêu cầu bằng 0, dùng `set-current 0` không có
  `current_off_delay`. Chỉ dùng delay `0.2` khi dòng lùi thực sự khác 0, tránh
  làm mới delay mỗi 10 ms và giữ modulation/PWM hoạt động vô hạn.
- Không đổi cực tính ADC/RX, ngưỡng phanh `0.05`, điều kiện giữ nút R, giới hạn
  dòng lùi, PARK, watchdog hoặc cấu hình ESC.

Đối chiếu implementation chính thức VESC 6.05 tại commit
`a0d40e2c5a42c810888d8c379307e6b0a118a125`: `set-brake-rel` gọi brake-current
mode; `set-current` chuyển sang current mode; đối số thứ hai của `set-current`
là modulation-off delay, không phải motor-command timeout.

## Kiểm chứng host

- LispBM VESC 6.05, heap 2464: 156/156 PASS, gồm startup RX, fault/watchdog,
  Mode 2 → R, ưu tiên phanh, nhả phanh, ramp 0.1–5 s và dòng lùi âm.
- Safety source/branch: 17/17 PASS.
- UI drawer/LVGL host: 9/9 PASS.

ESC API trong LispBM là fixture; host không mô phỏng driver FOC, PWM, LED hoặc
mô-men motor thật. Gói này cần xác nhận trên đúng ESC 6.05.

## Thử trên hardware

1. Kê bánh chủ động an toàn, thả ga và dùng đúng ESC đang nối tay ga/phanh.
2. Trong VESC Tool, upload/run `main.lisp` của thư mục này. Không nạp file bằng
   cách dán toàn bộ vào REPL.
3. Thoát PARK, chọn Mode 2, giữ R và bóp phanh ít nhất 0,3 s.
4. Vẫn giữ R, nhả hoàn toàn phanh và không vặn ga trong ít nhất 1 s.
5. Kiểm tra VESC Tool đã thoát trạng thái phanh. Sau đó mới tăng ga rất nhẹ để
   kiểm tra mô-men lùi.
6. Nếu vẫn báo phanh, khi vẫn giữ nguyên trạng thái chạy lệnh đọc sau một lần:

```lisp
(list (get-adc-decoded 1) brk-rel rv-btn rv-armed rv-dir
      (ride-safety-state) (get-adc-decoded 0) rv-rel
      (get-iq-set) safety-fault (secs-since motor-seen)
      (conf-get 'adc-ramp-time-neg))
```

Kết quả bình thường khi nhả phanh, chưa vặn ga: ADC1 gần 0, `brk-rel<=0.001`,
`rv-btn=1`, `rv-armed=1`, `rv-dir=-1`, safety state 3, ADC0 và `rv-rel` gần 0,
`iq-set` gần 0, fault 0 và motor heartbeat mới.

Không chạy thử có tải hoặc để bánh chạm đất cho đến khi xác nhận phanh đã nhả và
dòng lùi tăng đúng chiều.
