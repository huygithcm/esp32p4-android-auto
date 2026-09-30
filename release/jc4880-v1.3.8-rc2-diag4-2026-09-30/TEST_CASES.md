# Test cases v1.3.8-rc2-diag4

## A. Kiểm tra tĩnh và host đã chạy

| ID | Mục tiêu | Kích thích | Kết quả mong đợi | Kết quả gói |
|---|---|---|---|---|
| A01 | Range cấu hình R | Parse `9990 dA` | Hợp lệ | PASS |
| A02 | Chặn quá range R | Parse `9991 dA` | `OUT_OF_RANGE` | PASS |
| A03 | Clamp dòng R | Requested `999 A`, fixture `l-current-min=-30 A`, `l-current-max=70 A` | Limit thực tế `30 A` | PASS |
| A04 | Luồng Mode 2 sang R | Xe đứng, ga 0, R + phanh đủ 20 tick, nhả phanh | `REVERSE_ACTIVE`, dòng motor âm khi có ga | PASS |
| A05 | Ưu tiên phanh trong R | Đang R, bóp lại phanh | Lệnh phanh thay lệnh dòng lùi | PASS |
| A06 | Nhả phanh trong R | Nhả ADC1 và đợi `brk-rel<=0.001` | Phát `set-current 0`, sau đó R điều khiển theo ga mới | PASS |
| A07 | Badge mode | Render `P/1/2/3/R/-` trên 4 theme | Font 48, badge 80 px, không ra ngoài `800x480` | PASS |
| A08 | Drawer mode | Vuốt/mở/đóng, chọn Mode/PARK, đổi screen | Không đóng ngay ngoài ý muốn; `9/9` | PASS |
| A09 | Migrate dòng R cũ | Mở giá trị `75 dA`, sau đó thử `+` và `-` | Hiện `7.5 A`; `+` -> `8 A`; `-` -> `7 A` | PASS |

## B. Test hardware cần người dùng chạy

Mỗi ca bắt đầu với bánh chủ động kê khỏi mặt đất, ga/phanh đã nhả, đúng VESC
6.05, ADC Control Type `NONE`, và firmware/Lisp trong cùng gói này.

| ID | Thao tác | Kết quả đạt |
|---|---|---|
| H01 | Boot, mở VESC Tool song song và thay đổi ga khi bánh đã kê | Dashboard hiện `P`, không hiện `-`; M-TEMP trên P4 phải theo cùng nguồn telemetry motor của VESC Tool. Nếu cả hai cùng đổi theo ga trên xe không có sensor, ghi raw VESC telemetry/config để chẩn đoán phía VESC |
| H02 | Nhấn ngắn nút Mode khi ở PARK | Chuyển Mode 1 -> 2 -> 3; badge 80 px rõ, không che tốc độ hoặc min/max |
| H03 | Vuốt mở drawer rồi nhả tay | Drawer giữ mở; không tự chạy về dashboard cho tới thao tác đóng hợp lệ |
| H04 | Ở Mode 2, ga 0, giữ R và bóp phanh >=0,3 s | Chưa cấp dòng lùi trong lúc còn phanh; trạng thái đi qua reverse ready/interlock đúng thiết kế |
| H05 | Vẫn giữ R, nhả hoàn toàn phanh >=1 s | Dashboard hiện `R`; VESC Tool thoát brake state; đèn/trạng thái phanh không giữ sau nhả |
| H06 | Sau H05, tăng ga rất nhẹ | Motor quay lùi, dòng âm và không vượt giới hạn ESC |
| H07 | Đang R có ga nhẹ, bóp phanh | Phanh ưu tiên ngay; không còn torque lùi |
| H08 | Nhả phanh lần nữa khi vẫn giữ R | Thoát brake state; torque chỉ trở lại theo vị trí ga hiện tại, không dùng giá trị cũ |
| H09 | Settings đặt R `20 A`, lưu và chạy lại H04-H06 | Requested/current giới hạn theo 20 A và giới hạn ESC, không còn trần policy 14 A |
| H10 | Settings thử `999 A` nhưng giữ ESC limit an toàn | Giá trị lưu được; dòng thực tế vẫn clamp theo ESC, không đạt 999 A nếu ESC limit thấp hơn |
| H11 | Nhả nút R | Thoát R về mode tiến hiện tại theo state machine, không giữ torque lùi |
| H12 | Mất/stale packet hoặc fault đầu vào | Badge hiện `-` hoặc `P` theo safety state, propulsion bị chặn |

## C. Dữ liệu cần ghi khi H05 không đạt

Giữ nguyên trạng thái lỗi, không vặn ga, rồi đọc một lần trong VESC Tool:

```lisp
(list (get-adc-decoded 1) brk-rel rv-btn rv-armed rv-dir
      (ride-safety-state) (get-adc-decoded 0) rv-rel
      (get-iq-set) safety-fault (secs-since motor-seen)
      (conf-get 'adc-ramp-time-neg))
```

Khi nhả phanh và R đã active, giá trị mong đợi là ADC1 gần 0,
`brk-rel<=0.001`, `rv-btn=1`, `rv-armed=1`, `rv-dir=-1`, safety state 3,
ADC0/`rv-rel` gần 0 nếu chưa vặn ga, `iq-set` gần 0, fault 0 và motor heartbeat
còn mới.
