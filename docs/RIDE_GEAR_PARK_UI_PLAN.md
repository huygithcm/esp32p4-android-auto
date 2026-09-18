# Hiển thị số trong vòng tròn và chế độ P

Ngày: 2026-09-17. Nhánh hiện tại: `fix/gear-circle-park-reverse`.
Trạng thái: ghi lại phương án đã trình bày để làm cơ sở triển khai; chưa
triển khai P hoặc giao diện mới. Các lựa chọn đề xuất bên dưới cần được phân
biệt với hành vi đã có trên phần cứng.

Kế hoạch task, phụ thuộc và các cổng kiểm thử phần cứng:
[RIDE_GEAR_PARK_TASKS_TEST_PLAN.md](RIDE_GEAR_PARK_TASKS_TEST_PLAN.md).

## Yêu cầu giao diện

- Thay nhãn MODE hiện tại bằng một vòng tròn ở đúng vị trí dưới tốc độ.
- Giữa vòng tròn chỉ có một ký tự: `1`, `2`, `3`, `R` hoặc `P`; bỏ chữ MODE.
- `1/2/3` vẫn là ba giới hạn dòng độc lập do người dùng cài, clamp theo ESC.
- `R` chỉ xuất hiện khi BE xác nhận sẵn sàng lùi hoặc đang cho phép chạy lùi.
  Không suy ra R chỉ từ nút nhấn hoặc `direction_state != 1` như hiện tại.
- Đề xuất hiển thị `-` khi chờ interlock, thiếu hoặc hết hạn dữ liệu. Không
  hiển thị số tiến hay R như thể xe đã sẵn sàng khi chưa được BE xác nhận.
- P là khóa yêu cầu chạy tạm thời, không phải giữ ga, phanh đỗ hoặc chống trôi.
- Đổi số tiếp tục không phát tiếng bíp/xung dòng bằng motor.

## Tham khảo Dat Bike

Nguồn chính thức đã đối chiếu: [Hướng dẫn Dat Bike ERA, mục 2.1.5 và 2.8](https://dat.bike/huong-dan-su-dung-xe-era/).
ERA khởi động vào P; nhả ga rồi nhấn MODE để thoát P về chế độ lái đã nhớ.
Để vào P: nhả ga, bóp phanh và giữ MODE một giây. Hạ chân chống cũng vào P;
chân chống chưa nâng sẽ chặn thoát P. Đây là tham khảo cho dòng ERA, không
khẳng định mọi dòng Dat Bike dùng cùng thao tác.

## Luồng đề xuất cho dự án

| Trạng thái/thao tác | Kết quả |
| --- | --- |
| Bật nguồn | P; chuẩn bị mức dòng số 1 |
| P, đứng yên, ga nhả, nhấn MODE ngắn | Thoát P; lần đầu sau boot vào số 1 |
| Đang tiến, nhấn MODE ngắn | 1 -> 2 -> 3 -> 1 |
| Đứng yên, nhả ga, bóp phanh và giữ MODE một giây | Vào P |
| Thoát P trong cùng phiên | Khôi phục số tiến trước khi vào P |
| Giữ nút RX lùi riêng, đủ điều kiện BE | R; giữ cơ chế hold-to-run |
| Đang P, giữ RX | Vẫn P; phải thoát P trước |
| Nhả RX | Bỏ trạng thái sẵn sàng lùi; interlock cho đến khi được phép tiến |

Khởi động vào P là thay đổi so với hiện tại, nhưng số tiến chuẩn bị vẫn luôn
là 1. Không dùng thao tác chuyển lùi của ERA thay cho nút RX giữ để lùi.
Chưa bổ sung chân chống nếu chưa xác định tín hiệu phần cứng.
Nhấn giữ MODE không được phát thêm sự kiện nhấn ngắn; tránh đổi số trước
khi vào P. Ngưỡng đứng yên và ga nhả phải dùng chung với BE, xác minh bằng
giá trị tuyệt đối của tốc độ khi xét đứng yên.

## Phân công và ràng buộc triển khai

- FE (Codex): vòng tròn trong code custom/theme, không đặt logic cần giữ vào
  code generated; cập nhật dashboard, overlay Android Auto và simulator.
  FE hiển thị trạng thái BE xác nhận, không tự bật P/R ngay khi gửi yêu cầu.
- Lisp/BE (Claude theo phân công chung): trạng thái P, điều kiện vào/thoát P,
  phân biệt nhấn ngắn/giữ, khóa ga/PAS/lùi và trả trạng thái qua CAN.
- Chốt contract trạng thái có tính mới của dữ liệu, khả năng hỗ trợ P và kết
  quả yêu cầu. Không tự đổi ý nghĩa gói DASH cũ mà không có tương thích phiên
  bản. Mất kết nối không phải bằng chứng ESC đã vào P.
- P phải xóa ramp lực kéo và lệnh PAS cũ để thoát P không tái sử dụng lệnh
  trước đó. Kiểm tra mọi đường toggle throttle/panel/helper để không vượt P.
- Xác định rõ ưu tiên phanh và hành vi khi Lisp chết/native ADC hoạt động lại;
  không chỉ đổi tên `throttle-on=0` thành P rồi coi đó là khóa đầy đủ.
- R cần được xác nhận từ quyền chạy lùi thực tế. Xử lý lỗi RX và chặn arm khi
  xe còn lăn; các lỗi reverse đã ghi trong log trước vẫn cần giải quyết.

## Tiêu chí kiểm thử trước bàn giao

1. Các theme hiển thị 1/2/3/R/P giữa vòng tròn, không còn tiền tố MODE tại ô
   này, không lệch tâm hoặc che tốc độ; simulator có từng trạng thái để xem.
2. Boot vào P với số chuẩn bị 1; ga hoặc PAS không tạo lực kéo trong P.
3. Thoát P bị từ chối khi ga chưa nhả hoặc xe chưa đứng yên; giữ RX trong P
   không cho phép lùi. Yêu cầu bị từ chối không đổi UI thành trạng thái thành công.
4. Giữ MODE vào P không gây đổi số; thoát P khôi phục đúng số trong phiên.
5. Nhấn RX chưa đủ điều kiện không hiện R; đủ điều kiện mới hiện R; nhả RX
   hoặc lỗi RX phải hủy sẵn sàng và hiển thị đúng interlock.
6. Không tái phát PAS/ramp cũ sau P; đổi số không tạo motor tone.
7. Mất CAN, snapshot cũ hoặc Lisp không hỗ trợ P không được báo sẵn sàng giả.
8. Kiểm tra parser/contract, simulator và bench bánh nhấc khỏi đất trước khi
   xác nhận luồng phần cứng. Build thành công không thay thế kiểm thử Lisp.

## Mốc workspace

Trước khi tạo nhánh đã có thay đổi chưa commit trong `lisp/main.lisp`,
`components/vesc_can/vesc_ride_mode.c`,
`Super_VESC_Display/lvgl-simulator/main.c`, `docs/RIDE_MODE_V2_BRINGUP.md`
và `COLLABORATION_LOG.md`. Giữ nguyên chúng khi tạo nhánh từ HEAD hiện tại;
không coi toàn bộ diff là thay đổi của tính năng P. Nhánh dùng chung với Claude
trong cùng working tree, không phải workspace riêng.
