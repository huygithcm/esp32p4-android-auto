# Kế hoạch task và kiểm thử P / R / hiển thị số

Ngày: 2026-09-17. Nhánh: `fix/gear-circle-park-reverse`.
Đặc tả: [RIDE_GEAR_PARK_UI_PLAN.md](RIDE_GEAR_PARK_UI_PLAN.md).
Trạng thái: kế hoạch; các test bên dưới chưa được thực hiện và chưa có kết
luận phần cứng an toàn. Chưa triển khai tính năng trong lần lập kế hoạch này.

## 1. Các điểm chặn được xác nhận từ source hiện tại

| ID | Hiện trạng | Vì sao phải xử lý |
| --- | --- | --- |
| B1 | `motor-control-loop` gọi `app-disable-output 1500` định kỳ | Khi loop ngừng, khóa ADC hết hạn; P chỉ nằm trong Lisp không đủ bảo đảm khóa lực kéo khi Lisp lỗi. Phải xác minh cấu hình/firmware ESC và đường inhibit độc lập. |
| B2 | `reverse-step` dùng `sp > 0.083` khi xét chuyển trạng thái | Có thể arm từ trạng thái đang lăn lùi. Phải tách điều kiện vào R khỏi điều kiện duy trì R; không thay mọi điều kiện bằng abs vì có thể cắt lùi hợp lệ khi đã chạy. |
| B3 | RX monitor đặt `rv-hw-ok=1` rồi có thể chết ở lần đọc GPIO sau | Trạng thái nút/quyền lùi cũ có thể tiếp tục được motor loop sử dụng. Cần giám sát tính mới và hủy quyền khi lỗi. |
| B4 | `get_dash` chỉ kiểm tra `valid`, không kiểm tuổi bản tin | Màn hình có thể giữ R/số cũ sau khi mất phản hồi. Không được suy ra P từ mất dữ liệu. |
| B5 | Callback đổi Target VESC ID chưa retarget Ride Mode; snapshot chưa được reset đầy đủ | Cấu hình và trạng thái có thể thuộc hai ESC khác nhau. |
| B6 | Nhánh `throttle-on=0` đứng trước phanh, chưa xóa `rv-rel`/PAS | Không thể chỉ đổi tên nhánh này thành P; cần định nghĩa lại ưu tiên phanh và xóa yêu cầu lực kéo cũ. |
| B7 | Nút Beep riêng còn gọi `foc-play-tone` | P khóa ga/PAS nhưng có thể còn kích motor qua lệnh âm thanh. Phải chặn motor tone trong P và luồng vận hành. |

Các kết luận này là review source, chưa phải kết quả đo thực tế. Native ADC,
timeout và GPIO phụ thuộc firmware/model ESC; cần ghi nhận đúng thiết bị.

## 2. Thứ tự task và điều kiện hoàn thành

Phân công dự kiến theo workspace chung: Codex phụ trách FE, Claude phụ trách
Lisp/BE; kiểm thử tích hợp cần đối chiếu cả hai. Danh sách này là handoff,
không có nghĩa Claude đã nhận hoặc hoàn thành task.

| Task | Phụ trách | Nội dung / đầu ra | Phụ thuộc |
| --- | --- | --- | --- |
| T0 | Chung + người thử | Chụp baseline Git/diff, backup script và config VESC; ghi model ESC, firmware, CAN ID, sơ đồ TX/RX/phanh/ga, nguồn và phương án ngắt lực kéo độc lập | Trước mọi nạp phần cứng |
| T1 | BE + FE | Chốt bảng chuyển trạng thái, ưu tiên phanh, ngưỡng thời gian/tốc độ/ga; contract có version, trạng thái, số chuẩn bị, lý do từ chối, sequence và tính mới | T0 |
| T2 | BE | Giải quyết B1: trạng thái lỗi không tự mở lại ga ADC. Xác minh native output, reboot, script stop, motor timeout, quyền kiểm soát motor; ghi cơ chế bảo vệ thực tế | T1; chặn thử có lực kéo |
| T3 | BE | Implement P và một hàm chuyển trạng thái chung cho mọi nguồn lệnh; boot P/số 1, khóa ga/PAS/lùi, xóa ramp/setpoint, chặn tone, giữ hành vi phanh đã chốt | T1, T2 |
| T4 | BE | MODE nhấn ngắn/giữ, debounce và yêu cầu đã nhả sau boot; giữ >=1 s chỉ vào P khi hợp lệ, không phát thêm nhấn ngắn | T3 |
| T5 | BE | Sửa B2/B3: tách requested/ready/active/interlock, xác nhận phanh, RX freshness, nhả RX/hỏng RX; không tự trao lại lực kéo tiến khi đang lăn lùi | T1, T2 |
| T6 | BE | Version/parser/ACK/timeout; sửa B4/B5, loại snapshot cũ khi đổi target/reconnect và hủy yêu cầu cũ; không toggle lại khi retry | T1 |
| T7 | FE | Vòng tròn 1/2/3/R/P tại vị trí MODE, trạng thái `-` khi không xác nhận; cập nhật custom themes, overlay, màn chi tiết và pending/error | T1; mock có thể làm song song T3–T6 |
| T8 | FE | Simulator có tình huống chủ động cho từng trạng thái, giữ/nhả nút, từ chối, lỗi và mất dữ liệu; tách rõ mock và dữ liệu thật | T7 |
| T9 | Chung | Host tests điều khiển + parser, test UI, review diff và hồi quy mode ampere/BMS | T3–T8 |
| T10 | Chung | Build đúng board, tạo merged mới và bộ Lisp cùng phiên bản; lưu hash/config/kết quả test và đường khôi phục | T9 |
| T11 | Người thử + chung | Thử phần cứng theo từng cổng ở mục 5, ghi kết quả từng case và dừng khi fail | T10 |

## 3. Những quyết định phải viết thành contract trước khi code

- Boot vào P, số chuẩn bị 1. Thoát P cùng phiên trở lại số tiến đã chọn;
  restart không khôi phục quyền chạy hoặc quyền lùi.
- Điều kiện vào/thoát P được kiểm tra ở ESC, không tin UI. Giữ MODE thất bại
  không biến thành nhấn ngắn lúc nhả. Nhấn khi ga mở không được xếp hàng để
  tự thoát P khi ga đóng; phải có một thao tác mới hợp lệ.
- Bảng phanh: P chặn lực kéo, không tự phát lực giữ bánh. Phanh cơ vẫn là
  phương tiện dừng; quyết định có giữ phanh điện theo yêu cầu trong P phải
  được chốt và test. Không để P vô tình vô hiệu hóa phanh đang yêu cầu.
- R chỉ hiển thị khi quyền lùi đã được xác nhận hoặc đang chạy lùi hợp lệ.
  Arm ở đứng yên dùng trị tuyệt đối tốc độ; duy trì R dùng giới hạn tốc độ
  lùi riêng. Nhả RX đưa về interlock, không cấp tiến tới khi dừng và nhả ga.
- P xóa PAS cũ và bỏ qua frame trợ lực trong P. Chốt handshake tái cho phép
  PAS sau thoát P, ví dụ phải thấy zero/idle mới trước yêu cầu trợ lực mới;
  một stream dương liên tục không được gây bật lực kéo ngay khi thoát P.
- Các entry point TX, panel, SELECT, helper, throttle toggle và Save đều
  đi qua một bộ kiểm điều kiện. Save không được thay quyền P/R. Retry dùng
  lệnh đích có sequence, tránh một lần bấm thành hai lần đảo trạng thái.
- Đề xuất poll trạng thái 200 ms, FE hết hạn sau 1000 ms kể từ bản tin hợp lệ
  mới nhất của đúng target/session; đây là mục tiêu cần đo, không phải cam kết
  hiện tại. Lỗi dữ liệu không tự hiển thị P và không tự gửi lệnh phanh/đảo chiều.
- Watchdog nút/motor và thời gian cắt lực kéo phải có giá trị đo được ghi trong
  contract trước test. Kiểm tra tick jitter, ADC sample age và lỗi task; UI
  timeout không được dùng thay cho bảo vệ motor.
- P4 mất nguồn/CAN mất liên lạc: ESC không được sinh lệnh thoát P hoặc đảo
  chiều; PAS hết hạn theo timeout đã xác minh. Chốt rõ khả năng tiếp tục điều
  khiển bằng ga/nút tại ESC trong trạng thái tiến hiện hành.
- Backend cũ không hỗ trợ P: báo không hỗ trợ, không cho UI tuyên bố khóa P.
  Không mở rộng âm thầm ý nghĩa gói DASH cũ; chọn version/capability hoặc gói mới.

## 4. Ma trận test host và simulator

Host harness cần chạy logic điều khiển Lisp thực với I/O/time được giả lập,
hoặc kiểm thử trực tiếp logic dùng chung có đối chiếu Lisp. Một mô hình độc
lập viết lại trên host không chứng minh script đang nạp là đúng. Linter và
test parser hiện có chỉ là một phần; chưa kiểm được hành vi motor/task.

| ID | Kích thích | Kết quả bắt buộc |
| --- | --- | --- |
| H01 | Boot ga nhả / ga mở / TX hoặc RX bị giữ | Luôn P; không có yêu cầu lực kéo, không tự thoát do nút đang giữ |
| H02 | P + ga/PAS dương/nút lùi/Beep/helper toggle | Không vượt khóa P; không tone và không lệnh lực kéo |
| H03 | Thoát P hợp lệ và từng điều kiện sai | Chỉ thao tác hợp lệ vào số đúng; từ chối có lý do, không tự thực hiện sau |
| H04 | MODE bounce, bấm nhanh, 0,99/1,00/1,01 s, giữ lâu | Một sự kiện đúng loại; giữ vào P không làm đổi số hoặc thoát lại |
| H05 | Vào P khi ramp/PAS/R còn trạng thái cũ; thoát P | Xóa trạng thái lực kéo, không bật lại setpoint cũ |
| H06 | R: đứng yên, lăn tiến, lăn lùi, biên ngưỡng và nhiễu | Chỉ arm khi đạt điều kiện; không cắt R hợp lệ chỉ vì đã chạy lùi |
| H07 | Ga + phanh, thiếu phanh xác nhận, nhả RX đang lùi | Không arm sai; phanh thắng yêu cầu kéo; không cấp lực tiến sớm |
| H08 | RX read lỗi sau init; monitor ngừng cập nhật | Hủy quyền lùi trong thời hạn contract; lỗi rõ ràng, không giữ R cũ |
| H09 | Motor loop/panel task chết riêng; Lisp stop/restart | Không mở lại ga ngoài kiểm soát; không giữ trạng thái sẵn sàng giả |
| H10 | Packet ngắn/sai version/sai target, seq cũ/lặp/ACK muộn | Không mutate trạng thái; không gửi lặp gây đổi quyền hai lần |
| H11 | CAN mất, P4 restart, đổi target, thời gian wrap | Snapshot cũ bị loại; FE về `-` đúng hạn; không tự thoát P/đảo chiều |
| H12 | 50/70/100 A với ESC 70 A; thứ tự 70/30/50; ESC max đổi live | Clamp đúng, không ép thứ tự, không ghi tăng ESC max, không motor tone |
| H13 | Save bị từ chối/EEPROM lỗi, reboot | Không báo Saved giả; boot P/số 1; config hợp lệ được khôi phục |
| U01 | Từng trạng thái và từng theme/dashboard/overlay | Một ký tự giữa vòng tròn, không MODE, không đè tốc độ |
| U02 | RX mới nhấn nhưng chưa ready; interlock; backend cũ | Không hiện R/P hoặc số tiến gây hiểu nhầm |
| U03 | Yêu cầu đang chờ, bị từ chối, ACK trễ, dữ liệu quá hạn | FE hiển thị theo xác nhận còn mới, có thông báo thích hợp |
| U04 | Chuyển BMS/settings/AA rồi về dashboard nhiều lần | Không snapshot cũ, timer trùng, crash hoặc UI đứng |

Thử giá trị đúng ngưỡng và hai phía của mọi ngưỡng; thứ tự sự kiện khác nhau
trong cùng tick cũng phải được thử. Ghi số lượng case và kết quả thực tế,
không đánh dấu PASS từ review source.

## 5. Các cổng kiểm thử phần cứng

### G0 — Chưa cho phép quay motor

- Hoàn tất T0–T9 và xử lý B1–B7; test điều khiển quan trọng không được bỏ qua.
- Ghi model ESC/firmware/Lisp hash, số ESC truyền động, nguồn cấp, giới hạn
  motor/battery/regen, ADC min/max/deadband và chiều tốc độ đã hiệu chuẩn.
- Thông tin đã biết chỉ là pin khoảng 40 V và Motor Current Max 70 A: chưa
  đủ chọn dòng thử an toàn. Không dùng 50/70/100 A làm mức thử ban đầu.
  Người phụ trách phần cứng phải chốt mức thấp phù hợp motor, ESC, nguồn,
  gá thử và phanh; nguồn phải xử lý được năng lượng hồi khi thử regen.
- Xác nhận đường ngắt lực kéo độc lập với FE/CAN/Lisp. Không dùng nút P trên
  màn hình làm nút dừng khẩn cấp duy nhất. Không tháo dây pha khi đang cấp điện.
- Backup config motor/app và Lisp đang nạp; chuẩn bị cách khôi phục qua kết
  nối trực tiếp khi P4 không hoạt động. Không nạp/thử đồng thời nhiều ESC chưa
  có thiết kế đồng bộ P/R được kiểm chứng.

### G1 — Nạp và kiểm tra logic khi lực kéo bị vô hiệu hóa độc lập

- Build đúng board P4, tạo lại merged từ chính build đó; nạp merged ở offset
  đã xác minh của dự án. Nạp Lisp riêng vào ESC. Ghi SHA-256 của cả hai file.
- Kiểm tra boot/P/số chuẩn bị, version protocol, CAN ID, console và các task.
- Test TX/RX/phanh/ga bằng telemetry; xác nhận nhấn ngắn/giữ, từ chối và
  tuổi dữ liệu mà chưa cho phép motor chạy. Không dùng nhãn trên UI làm bằng
  chứng duy nhất về lệnh motor.
- Kiểm tra Stop Lisp và lỗi task trong điều kiện lực kéo vẫn bị inhibit;
  quan sát native ADC sau hơn 1,5 s và qua restart. Chỉ chuyển G2 khi cơ chế
  khóa thật sự đáp ứng T2; không suy luận từ việc bánh chưa quay.

### G2 — Bench bánh nhấc khỏi đất, giới hạn thấp

Gá xe chắc chắn, bánh truyền động không chạm đất và vùng quay không có người
hoặc vật cản; có người thao tác ngắt lực kéo. Giữ nguồn/config đã chốt ở G0.
Không dùng giữ bánh bằng tay hoặc khóa rotor để thử trần dòng.

| ID | Thao tác theo thứ tự | Tiêu chí đạt |
| --- | --- | --- |
| B01 | Boot P; ga nhẹ, nút RX, PAS nếu có | Không lực kéo; boot với nút giữ cũng không tự thoát P |
| B02 | Ga nhả, đứng yên, MODE ngắn | Vào 1; chưa vặn ga thì không xung dòng/tone |
| B03 | Đổi 1/2/3 bằng TX và UI; Save, ga nhả/PAS off | Nhãn đúng, chỉ đổi trần dòng, không phát lực kéo |
| B04 | Ga nhẹ rồi nhả, vào P đúng thao tác, thoát P | Ramp đúng; không tự chạy lại; số trong phiên đúng |
| B05 | Phanh + MODE giữ, nhả nút; thử thao tác không hợp lệ | Một lần vào P; không đổi số trước hoặc thoát P khi nhả |
| B06 | Thử R chưa đủ điều kiện, sau đó đủ điều kiện | R chỉ hiện khi ready; ga nhẹ quay đúng chiều, trong giới hạn thấp |
| B07 | Nhả RX khi lùi, giữ ga; sau đó nhả ga/dừng | Không tự cấp lực tiến; interlock và UI đúng |
| B08 | Phanh trong tiến/R, bật/tắt P theo điều kiện | Phanh theo contract, không tái dùng ramp lực kéo cũ |
| B09 | Ngắt kênh dữ liệu có kiểm soát / restart riêng P4 | UI hết hạn đúng, không thay quyền chạy/chiều ngoài yêu cầu; PAS hết hạn |
| B10 | Fault injection task/RX/Lisp | Thực hiện trước với inhibit; chỉ thử có lực kéo khi T2/G1 đã chứng minh cơ chế cắt độc lập và có quy trình riêng |
| B11 | Reboot sau Save và khi số trước đó là 3/R | Về P, số chuẩn bị 1; không giữ quyền lùi |

Đo dòng motor và battery riêng; log lấy mẫu chậm có thể bỏ lỡ xung. Xác định
trước tốc độ ghi, độ nhiễu, ngưỡng phát hiện xung và thời gian phản ứng chấp
nhận. Không khẳng định dòng điện thực bằng tuyệt đối 0 chỉ từ số UI làm tròn.
Bench không tải không chứng minh giới hạn dòng dưới tải; cần test tải kiểm soát
riêng nếu muốn xác nhận trần dòng thực tế.

### G3 — Chạy chậm ở khu vực kiểm soát

Chỉ bắt đầu sau toàn bộ case bắt buộc G2 đạt, không lỗi an toàn còn mở và
người phụ trách phần cứng duyệt biên bản. Thử trên nền phẳng, khu vực riêng
không có người qua lại, mức dòng/tốc độ thấp đã chốt; tăng từng bước sau khi
review log. Thử phanh và dừng trước, sau đó P, cuối cùng R. Không thử trôi dốc,
tải lớn hoặc fault injection khi đang có người ngồi chạy trong giai đoạn này.
P không giữ xe trên dốc. Chưa mặc định khôi phục mức 70 A sau test.

## 6. Tiêu chí dừng và bằng chứng bàn giao

Dừng ngay, ngắt lực kéo bằng phương tiện đã xác minh và không tiếp tục case
kế tiếp nếu: bánh quay ngoài yêu cầu, lực sai chiều, ga còn tác dụng trong P,
phanh sai hành vi, R báo ready sai, dữ liệu cũ vẫn báo sẵn sàng, Lisp/task lỗi,
nguồn quá áp/nhiệt vượt giới hạn thiết bị, hoặc có dòng ngoài ngưỡng thử.
Lưu log rồi tái hiện trong môi trường không có lực kéo để sửa và chạy lại các
case liên quan. Không cố bù lỗi bằng thay đổi giới hạn tùy ý trên xe đang chạy.

Mỗi lần thử ghi:

```text
Test ID / ngày / người thử:
Board P4 / ESC model / firmware / CAN ID:
Git commit + patch chưa commit / hash merged.bin / hash main.lisp:
Config motor/app / nguồn / mức giới hạn thử:
Trạng thái đầu / thao tác / trạng thái mong đợi:
Trạng thái thực / dòng motor / dòng pin / speed / thời gian phản ứng:
Log hoặc video / PASS-FAIL-NOT RUN / lỗi liên quan:
Người duyệt cổng tiếp theo:
```

Đầu ra bàn giao: contract, source + test report, merged đúng board, Lisp,
hash và config thử, biên bản từng cổng, danh sách lỗi còn mở, hướng dẫn nạp
và khôi phục. Không đánh dấu sẵn sàng chạy thực chỉ vì build/simulator đạt.
