# Hiệu chuẩn theo xe cho `lisp/main.lisp`

## Kết luận ngắn

`lisp/main.lisp` không thay thế cấu hình Motor/Battery/App trong VESC Tool. Nó
là lớp điều phối lệnh motor với thứ tự ưu tiên:

`master off > phanh > ga > cruise > PAS > thả trôi`

Script đọc ga/phanh đã chuẩn hóa từ ADC, đặt dòng motor hoặc dòng phanh, và đổi
giới hạn tốc độ/dòng theo profile. Vì vậy phải hiệu chuẩn cấu hình VESC nền trước,
sau đó mới chỉnh các hằng số trong Lisp.

## Trạng thái profile hiện tại

| Profile | `max-speed` | `l-current-max-scale` | Ảnh hưởng |
|---|---:|---:|---|
| Slow | 5 km/h | 0,30 | Giới hạn tốc độ 5 km/h và tối đa khoảng 30% giới hạn dòng motor đang hiệu lực |
| Medium | 10 km/h | 0,60 | Giới hạn tốc độ 10 km/h và khoảng 60% dòng motor |
| Fast | 20 km/h | 1,00 | Giới hạn tốc độ 20 km/h và cho phép toàn bộ giới hạn dòng motor |

Theo nhật ký phối hợp, các giá trị này đang phục vụ bench demo với bánh 40 mm,
motor 14 cực và truyền động 1:1; chưa nên coi là cấu hình an toàn cho một xe khác.

Theo thông số người dùng xác nhận ngày 2026-08-16, Motor Current Max hiện tại là
70 A. Vì vậy trần danh nghĩa của ba profile hiện tại lần lượt là 21 A, 42 A và
70 A. VESC vẫn có thể kẹp thấp hơn do giới hạn dòng pin, ERPM, duty, điện áp và
giảm công suất theo nhiệt độ. Đây là dòng motor/pha dùng để tạo mô-men, không phải
70 A lấy trực tiếp từ pin trong mọi điều kiện.

Điện áp pin hiện tại được người dùng xác nhận là 40 V. Không được tính công suất
pin bằng `40 V * 70 A`, vì 70 A ở trên là dòng motor/pha. Công suất điện lấy từ
pin gần đúng bằng `V_battery * I_battery`; do đó phải biết thêm `Battery Current
Max` và giới hạn xả liên tục/đỉnh của BMS. Ví dụ tại 40 V, giới hạn dòng pin 20,
30, 40 hoặc 50 A tương ứng khoảng 0,8, 1,2, 1,6 hoặc 2,0 kW trước tổn hao.

Nếu đây là pack Li-ion 10S thì 40 V là điện áp gần đầy và điện áp đầy thường gần
42 V, nhưng chưa được coi 10S là dữ kiện đã xác nhận. Phải xác nhận số cell và hóa
học trước khi đặt battery cutoff hoặc regen cutoff. Giới hạn regen cũng phải theo
khả năng nhận sạc của cell/BMS, đặc biệt khi pin gần đầy.

Profile 0 được áp dụng mỗi lần script nạp (`apply-profile 0`). `conf-set` chỉ áp
dụng tức thời trong RAM; script không gọi `conf-store`. Profile không scale dòng
phanh tái sinh.

## ESC thực thi giới hạn tốc độ như thế nào

Khi đổi profile, Lisp không chuyển ESC sang speed PID. `conf-set 'max-speed`
nhận m/s rồi firmware VESC đổi thành giới hạn ERPM dương:

```text
speed_factor = (motor_poles / 2) * 60 * gear_ratio
               / (wheel_diameter * pi)
l_max_erpm = max_speed_mps * speed_factor
```

Với bench hiện tại (14 cực, 1:1, bánh 0,04 m), hệ số xấp xỉ
3342 ERPM/(m/s), nên các profile tương ứng khoảng 4642, 9284 và 18568 ERPM.

Trong vòng giới hạn của ESC:

- Dưới `l_max_erpm * l_erpm_start`: vẫn cho toàn bộ dòng kéo đang hiệu lực.
- Từ điểm đó đến `l_max_erpm`: trần dòng kéo được nội suy giảm dần về gần 0.
- Trên `l_max_erpm`: ESC không cấp thêm mô-men kéo đáng kể theo chiều tiến.

Giá trị mặc định của `l_erpm_start` trong các bảng VESC 6.05/6.06/7.00 của repo
là 0,8. Nếu ESC đang dùng đúng 0,8, ba profile bắt đầu giảm lực kéo từ khoảng
4, 8 và 16 km/h rồi đạt trần ở 5, 10 và 20 km/h.

Đây là soft current limit, không phải bộ điều khiển giữ tốc độ. Nó không tự ra
lệnh phanh để kéo xe về đúng tốc độ khi xuống dốc; xe có thể trôi vượt trần.
Phanh cơ hoặc nhánh `set-brake-rel` vẫn là cơ chế giảm tốc. Các overspeed fault
cũng là option riêng của firmware, không được profile này tự bật.

`l-current-max-scale` được đổi cùng lúc và giảm trần mô-men ở toàn bộ dải tốc độ.
Lệnh ga, cruise và PAS cuối cùng đều đi qua giới hạn dòng/ERPM của ESC. Profile
chỉ thay giới hạn ERPM tiến (`l_max_erpm`), không thay giới hạn lùi
(`l_min_erpm`). Với nhiều ESC qua CAN, `conf-set` hiện chỉ sửa ESC cục bộ chạy
Lisp; các ESC khác phải được đồng bộ riêng.

### Giới hạn speed nhưng giữ toàn bộ dòng dưới ngưỡng

Có thể đặt `l-current-max-scale = 1.0` cho mọi profile và chỉ thay `max-speed`.
Khi đó profile không còn giảm mô-men trên toàn dải: ESC có thể dùng toàn bộ
`Motor Current Max` ở tốc độ thấp, rồi cơ chế ERPM mới giảm dòng trong vùng
`l_erpm_start * l_max_erpm` đến `l_max_erpm`.

Không có giới hạn tốc độ điện tử nào vừa giữ nguyên dòng kéo dương khi đã chạm
trần vừa ngăn motor tăng tốc: ESC bắt buộc phải giảm mô-men kéo, hoặc chuyển sang
phanh/regen. Dùng `set-rpm`/speed PID cũng vẫn điều chỉnh dòng để giữ tốc độ và
có thể tạo mô-men âm; nó không phải cách "không giới hạn dòng". Thiết kế hiện
tại chủ ý tránh chuyển đột ngột từ current mode sang speed PID để giảm nguy cơ
giật khi vào giới hạn.

Giữ 100% dòng ở profile 5 km/h có thể tạo lực khởi hành rất lớn dù tốc độ thấp.
Chỉ dùng lựa chọn này khi `l_current_max`, ramp ga và độ bám/cơ khí đã được hiệu
chuẩn cho toàn lực.

### Khuyến nghị kỹ thuật khi chọn cấu hình speed-only

Không nên quyết định `l-current-max-scale = 1.0` dựa trên khả năng tối đa của ESC.
Giá trị 1,0 chỉ an toàn khi `Motor Current Max` nền đã là mức mà xe có thể chịu ở
0 km/h: không trượt bánh, không nhấc đầu, không giật truyền động và không làm nóng
motor/ESC quá mức. Trần dòng pin vẫn phải đặt riêng theo cell, BMS, dây và đầu nối.

Ba phương án khởi đầu:

| Mục đích | Slow 5 km/h | Medium 10 km/h | Fast 20 km/h | Nhận xét |
|---|---:|---:|---:|---|
| Commissioning/bench | 0,30 | 0,60 | 1,00 | Cấu hình hiện tại; phù hợp nhất khi chưa biết trần mô-men an toàn |
| Cân bằng cho xe thật | 0,50 | 0,75 | 1,00 | Giữ thêm lực ở mode thấp nhưng vẫn giảm nguy cơ giật |
| Chỉ giới hạn tốc độ | 1,00 | 1,00 | 1,00 | Chỉ dùng khi toàn lực khởi hành đã được xác nhận an toàn |

Với Motor Current Max hiện tại là 70 A, ba phương án trên tương ứng:

| Phương án | Slow | Medium | Fast |
|---|---:|---:|---:|
| Commissioning/bench | 21 A | 42 A | 70 A |
| Cân bằng cho xe thật | 35 A | 52,5 A | 70 A |
| Chỉ giới hạn tốc độ | 70 A | 70 A | 70 A |

Không nên thử trực tiếp 70 A ở mode Slow. Lộ trình tăng có kiểm soát là 0,50
(35 A), 0,65 (45,5 A), 0,80 (56 A), rồi mới 1,00 (70 A), đồng thời theo dõi
trượt bánh, giật truyền động, dòng pin, nhiệt motor/FET và fault. Dừng tăng ngay
khi mức trước đã đủ lực sử dụng; 1,00 không phải mục tiêu bắt buộc.

Các tỷ lệ 0,50/0,75 chỉ là điểm khởi đầu, không phải giá trị chứng nhận cho mọi
xe. Khi chưa có thông số motor, ESC, pin/BMS, tổng khối lượng, bán kính bánh và
tỷ số truyền thực, khuyến nghị giữ cấu hình commissioning hiện tại. Không lấy
giá trị mặc định 60 A trong bảng schema của firmware làm dòng thực của xe.

Giữ `l_erpm_start = 0.8` làm điểm xuất phát. Với các trần 5/10/20 km/h, ESC bắt
đầu giảm dòng kéo tại khoảng 4/8/16 km/h. Giá trị thấp hơn làm giới hạn mềm hơn
nhưng xe hụt lực sớm hơn; giá trị cao hơn giữ lực sát trần hơn nhưng chuyển tiếp
gắt hơn. Không đặt gần 1,0 trong giai đoạn hiệu chuẩn.

Với code hiện tại, ADC `ramp_time_pos` và `ramp_time_neg` được dùng chung cho cả
ga và phanh. Có thể bắt đầu bằng giá trị mặc định firmware 7.00 là 0,3 s tăng và
0,1 s giảm, nhưng không nên tăng `ramp_time_pos` chỉ để chữa cú ga mạnh vì nó cũng
làm `brake-out` tăng lực phanh chậm. Muốn mode Slow có ga mềm mà phanh vẫn nhanh,
cần tách ramp ga/phanh hoặc thêm curve/ramp theo profile trong Lisp.

`panel-set-profile` và nút TX hiện cho đổi profile mà không kiểm tra ga đã nhả.
Trước khi dùng trên xe thật nên chỉ cho đổi mode khi ga dưới deadband, cruise đã
tắt và xe đang đứng hoặc ở dưới một tốc độ thấp. Ngay cả khi cả ba scale đều 1,0,
đổi từ Slow sang Fast lúc giữ ga vẫn tháo trần ERPM và xe sẽ tăng tốc tiếp.

Kết luận lựa chọn: nếu mục tiêu chính là tải nặng/leo dốc ở tốc độ thấp và người
lái đã quen xe, dùng speed-only 1,00/1,00/1,00 sau khi hạ `l_current_max` nền tới
mức an toàn ở mode Slow. Nếu mode Slow dành cho người mới, thao tác trong nhà hoặc
không gian hẹp, cấu hình cân bằng hay commissioning an toàn hơn.

## Nhóm 1 - bắt buộc hiệu chuẩn trong VESC Tool trước

### Motor và cảm biến vị trí

- Chạy Motor Setup/FOC detection đúng cho motor: điện trở, điện cảm, flux linkage,
  kiểu cảm biến và bảng Hall/encoder nếu có.
- Sai thông số có thể làm motor rung, hụt lực, nóng, mất đồng bộ hoặc báo fault.
  Không dùng profile dòng thấp để che một cấu hình FOC sai.

### Giới hạn dòng motor và pin

- `l_current_max` (Motor Current Max): trần mô-men kéo cơ sở mà profile nhân với
  0,30/0,60/1,00. Quá cao làm nóng motor/ESC, tăng lực giật và tải cơ khí; quá
  thấp làm xe yếu, cruise/PAS không giữ được tốc độ khi tải tăng.
- `l_current_min` (Motor Current Max Brake, giá trị âm): quyết định lực phanh tái
  sinh vì `set-brake-rel` dùng toàn bộ giới hạn phanh, không theo profile. Quá lớn
  về trị tuyệt đối có thể khóa/trượt bánh hoặc quá tải motor/ESC.
- `l_in_current_max` (Battery Current Max): phải không vượt khả năng xả liên tục
  của cell, dây, đầu nối và BMS. Đây là giới hạn công suất lấy từ pin, khác dòng
  pha motor.
- `l_in_current_min` (Battery Current Max Regen, giá trị âm): phải theo khả năng
  nhận sạc của cell/BMS. Sai có thể làm BMS ngắt hoặc VESC quá áp khi phanh.

### Điện áp pin và nhiệt độ

- `l_battery_cut_start` / `l_battery_cut_end`: đặt theo số cell nối tiếp, hóa học
  pin và ngưỡng BMS. Quá thấp gây xả sâu hoặc BMS cắt đột ngột; quá cao làm mất
  công suất và dung lượng sử dụng sớm.
- `l_battery_regen_cut_start` / `l_battery_regen_cut_end`: hạn chế regen khi pin
  gần đầy. Đặt sai có thể gây over-voltage/BMS trip hoặc làm mất phanh regen quá
  sớm.
- Chọn đúng loại cảm biến nhiệt motor rồi đặt `l_temp_motor_start/end` và
  `l_temp_fet_start/end`. Sai loại cảm biến nguy hiểm hơn sai ngưỡng vì VESC có
  thể đọc nhiệt độ không đúng và không derate khi motor thực sự nóng.

### Tốc độ cơ khí

- `si_motor_poles`: đúng số cực motor theo quy ước của VESC Tool.
- `si_gear_ratio`: đúng tỷ số truyền motor/bánh.
- `si_wheel_diameter`: đường kính lăn thực tế của bánh có tải.
- `l_max_erpm`, `l_min_erpm` và duty tối đa: đặt theo giới hạn điện/cơ của motor
  và truyền động.

Ba thông số SI quyết định đổi ERPM sang m/s. Nếu sai, `get-speed`, profile
`max-speed`, hiển thị tốc độ và bước tăng/giảm cruise 1 km/h đều sai theo cùng
một tỷ lệ. `rpm-per-ms` trong script chỉ học lại tỷ lệ từ `get-rpm/get-speed`;
nó không sửa được đường kính bánh, số cực hay tỷ số truyền bị nhập sai.

### ADC ga và phanh

ADC app phải tiếp tục được cấu hình dù script tạm khóa output gốc của app. Script
dùng:

- `get-adc-decoded 0` cho ga.
- `get-adc-decoded 1` cho phanh.

Cần hiệu chuẩn điện áp min/max, chiều tín hiệu, deadband và kiểu điều khiển để
giá trị đã decode gần 0 khi nhả và gần 1 khi tác động hết hành trình.

- Ga nhả không xuống dưới 0,05: điều kiện safe-start không bao giờ arm, xe không
  nhận ga.
- Ga/phanh bị nhiễu vượt 0,05: có thể tự chọn nhánh ga/phanh; phanh luôn có ưu
  tiên và sẽ hủy cruise.
- Endpoint sai: mất hành trình điều khiển hoặc đạt dòng tối đa quá sớm.
- Phải cấu hình output ADC gốc an toàn vì nếu Lisp chết, output gốc được phục hồi
  sau khoảng 1,5 giây.

## Nhóm 2 - thông số cần tune theo khối lượng và cảm giác xe

### Profile tốc độ và mô-men

Các cặp `max-speed` và `l-current-max-scale` nằm trong `apply-profile`. Dòng motor
chủ yếu quyết định mô-men, không trực tiếp là phần trăm tốc độ. Xe nhẹ/lốp ít bám
có thể cần scale nhỏ hơn; xe nặng hoặc leo dốc cần dòng cao hơn nhưng vẫn không
được vượt giới hạn nhiệt, pin/BMS và truyền động.

Khi đổi tốc độ phải đổi đồng thời nhãn `Slow/Medium/Fast` trên panel để UI không
hiển thị sai cấu hình thực tế.

### Ramping ga/phanh

Script đọc trực tiếp từ ADC App:

- `adc-ramp-time-pos`: thời gian tăng output từ 0 đến toàn thang.
- `adc-ramp-time-neg`: thời gian giảm output về 0.

Giá trị được kẹp trong 0,05-5 giây. Thời gian nhỏ cho phản ứng nhanh nhưng dễ
giật/trượt và tăng sốc cơ khí; thời gian lớn êm hơn nhưng ga chậm, còn phanh có
thể vào hoặc nhả quá trễ. Cùng hai giá trị đang được dùng cho cả ga và phanh,
nên phải kiểm tra thực tế cả hai chiều.

### Đường cong ga

- `thr-curve-accel = 0.0`: hiện được chú thích là tuyến tính.
- `thr-curve-mode = 0`: chế độ exponential của hàm VESC; khi hệ số là 0, cảm giác
  hiện tại vẫn gần tuyến tính.

Hệ số làm mềm đầu ga giúp điều khiển tốc độ thấp; tăng độ nhạy đầu ga cho cảm
giác mạnh hơn nhưng dễ giật. Đây là thông số cảm giác lái, không thay thế giới
hạn dòng an toàn.

### Cruise PI

- `cruise-kp = 0.02 A/ERPM`: sai lệch 100 ERPM tạo ngay khoảng 2 A thành phần P.
- `cruise-ki = 0.05 A/(s*ERPM)`: sai lệch 100 ERPM làm tích phân tăng khoảng
  5 A mỗi giây cho đến trần dòng đang hiệu lực.

Xe nặng, tỷ số truyền cao và đường dốc thường cần đáp ứng khác xe nhẹ. Gain quá
thấp làm tụt tốc và bắt lại chậm; quá cao gây săn tốc, giật dòng, dao động hoặc
trượt bánh. Cruise dùng ERPM làm phản hồi và đặt dòng motor, không dùng speed PID
của firmware. Chỉ tune cruise sau khi tốc độ cơ khí, dòng, ga và phanh đã đúng.

`ctl-dt = 0.01 s` là chu kỳ arbiter 100 Hz, không phải thông số hiệu chuẩn xe.
Không nên đổi độc lập vì nó tham gia cả slew và tích phân PI.

## Nhóm 3 - PAS nằm ngoài phần config chính của Lisp

Lisp chỉ nhận một setpoint dòng tuyệt đối `pas-amps` từ P4, từ chối nguồn CAN
xen kẽ và bỏ setpoint cũ sau 0,4 giây. Các thông số PAS thực tế nằm trong
`main/pas.h` và được lưu NVS:

| Thông số | Ảnh hưởng |
|---|---|
| `reverse` | Đảo chiều cadence; sai chiều làm đạp tới không trợ lực hoặc nhận nhầm đạp lùi |
| `level`, `level_count` | Tỷ lệ mức trợ lực hiện tại |
| `max_current_a` | Dòng motor ở mức 100%; phải nằm trong trần motor/pin an toàn |
| `mode` | Switch: dòng theo level; Proportional: thêm tỷ lệ theo cadence |
| `start_delay_ms` | Chống motor vào quá sớm khi vừa nhúc nhích bàn đạp |
| `start_current_pct` | Cú dòng ban đầu; cao quá gây giật, thấp quá khó khởi hành |
| `ramp_up_aps` | Tốc độ thay đổi dòng A/s; cao nhanh/mạnh, thấp êm/chậm |
| `stop_delay_ms` | Thời gian còn trợ lực sau khi ngừng đạp; quá dài làm xe tiếp tục đẩy |
| `min_cadence_rpm` | Ngưỡng xác nhận đang đạp tới |
| `full_cadence_rpm` | Cadence đạt 100% trong chế độ Proportional |

PAS chỉ được arbiter chọn khi không phanh, không có ga đang tác động và cruise
không hoạt động. Dòng PAS cuối cùng vẫn bị firmware VESC kẹp theo các giới hạn
dòng đang hiệu lực.

## Thông số hiện không tác động điều khiển xe

- `tc-on = 0` và `monitor-traction` không được `spawn`; vì vậy `tc-sens = 50`
  hiện không tạo traction control. Không coi khối này là tính năng an toàn.
- `beep-vol` chỉ điều khiển âm báo và được lưu EEPROM.
- `helper-btn-id = 0x123` chỉ cần đổi khi trùng CAN ID hoặc mapping nút của xe.
- `rx-button-state`, `tx-button-state`, `current-profile`, `rpm-per-ms`,
  `out-rel`, `brk-rel`, `armed` là trạng thái runtime, không phải số hiệu chuẩn.

## Thứ tự hiệu chuẩn an toàn đề xuất

1. Motor detection/cảm biến vị trí và chiều quay.
2. Dòng motor, dòng pin/BMS, regen, điện áp và nhiệt độ.
3. Số cực, tỷ số truyền, đường kính bánh; đối chiếu tốc độ với GPS hoặc dụng cụ
   độc lập ở tốc độ thấp.
4. ADC ga/phanh với bánh chủ động nhấc khỏi mặt đất; xác nhận nhả dưới 0,05 và
   toàn hành trình không nhiễu.
5. Giữ profile 30% và tốc độ thấp để thử ramp, ưu tiên phanh và safe-start.
6. Tune PAS với `max_current_a` thấp, `start_current_pct` thấp và stop delay ngắn.
7. Tune cruise cuối cùng trên khu vực kín, bằng phẳng; tăng gain từng bước nhỏ và
   luôn xác nhận bóp phanh hủy cruise.

Không hiệu chuẩn bằng cách nâng giới hạn cho tới khi hết fault. Mỗi trần phải có
nguồn số liệu từ motor, ESC, pin/BMS, dây/đầu nối và giới hạn cơ khí của xe.
