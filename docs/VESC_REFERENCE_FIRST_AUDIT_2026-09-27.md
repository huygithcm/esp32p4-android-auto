# Căn cứ VESC trước khi sửa P4 và Lisp

Ngày: 2026-09-27. Yêu cầu: đọc implementation VESC/custom có chức năng tương tự và trích phần cần thiết trước khi phát triển; không tự dựng implementation mới. Phần audit ban đầu dưới đây chỉ đọc mã, tải upstream và ghi tài liệu.

**Cập nhật sau yêu cầu test/fix/build:** reference LVGL đã dùng để sửa lượt nhả mở drawer; lỗi status Lisp và kiểm tra độ dài telemetry đã được tái hiện rồi sửa. Xem [kết quả video/regression](./M_TEMP_VIDEO_AND_REGRESSION_2026-09-27.md) và [gói RC](../release/jc4880-v1.3.8-rc1-2026-09-27/README.md). Những câu “chưa implementation/build” bên dưới là trạng thái tại thời điểm audit, không phải trạng thái bàn giao mới. Không ghi cấu hình ESC/flash; nguyên nhân M-TEMP hardware vẫn chưa xác định.

## Triệu chứng và ma trận đang có hiệu lực

Xác nhận mới nhất của người dùng: **lỗi xuất hiện khi cập nhật Lisp mới lên ESC** trong tổ hợp P4/release mới. Các chi tiết đã được làm rõ:

- Bảng chọn mode **mở được khi vuốt nhưng tự thu vào ngay khi nhả tay**; không phải hoàn toàn không nhận vuốt.
- Nút chuyển mode không chuyển; chưa rõ nút GPIO hay nút trên màn hình.
- M-TEMP thay đổi theo ga và **Motor Temperature trong VESC Tool cũng thay đổi**.
- Motor **không gắn cảm biến nhiệt**. Chưa biết cấu hình Sensor Type thực tế.

Xác nhận này thay thế ghi nhận cũ “P4 mới + Lisp cũ cũng lỗi”. Khi cô lập regression, giữ nguyên P4, ESC, cấu hình và thay riêng Lisp; không tự suy ra kết quả của tổ hợp chưa thử. VESC Tool cùng hiển thị giá trị đổi theo ga loại bỏ lời giải thích chỉ do widget P4, nhưng chưa loại trừ đường truyền chung nếu Tool nối qua P4. Kiểu kết nối, phiên bản/board ESC và cấu hình Sensor Type đã hỏi, chưa có câu trả lời. Chưa có packet capture hay lỗi Lisp runtime.

Cặp source/artifact đối chiếu vẫn là baseline `e7f0b16` và release `7c77f2b`, JC4880 v1.3.7 build 2026-09-19; Lisp trong release đã xác minh bằng SHA256 ở [báo cáo trước](./ROOT_LISP_VS_RELEASE_2026-09-27.md).

## Nguồn chính thức đã tải và đọc

Các phiên bản dưới đây là snapshot commit của nhánh upstream, không phải suy đoán firmware đang cài trên ESC. File tải nguyên bản, giữ header; dùng SHA256 ghi trong cache provenance để kiểm tra. Không chép vào source build.

| Nguồn | Commit đã ghim | Phần dùng làm căn cứ |
|---|---|---|
| [VESC firmware release_6_05](https://github.com/vedderb/bldc/tree/a0d40e2c5a42c810888d8c379307e6b0a118a125) | `a0d40e2c5a42c810888d8c379307e6b0a118a125` | ADC, Lisp bindings, CAN, command encoding |
| [VESC firmware release_6_06](https://github.com/vedderb/bldc/tree/94b305ec124b67b0161f2d3d85c4d5b35e48edc5) | `94b305ec124b67b0161f2d3d85c4d5b35e48edc5` | ADC/SETUP reference chính trong trích đoạn bên dưới |
| [VESC firmware release_7_00](https://github.com/vedderb/bldc/tree/20cbb362687291242ab90b99f25fbfe8835540fc) | `20cbb362687291242ab90b99f25fbfe8835540fc` | Native ADC, output gating, send-data, timeout |
| [VESC Tool](https://github.com/vedderb/vesc_tool/tree/dc53c658cbb89a947246034f7a00149cf79abdfc) | `dc53c658cbb89a947246034f7a00149cf79abdfc` | Decoder ADC/SETUP và custom app data độc lập |
| [VESC packages](https://github.com/vedderb/vesc_pkg/tree/c3965e47fb2c5787e45d75ec4dbc4868acf10e3a) | `c3965e47fb2c5787e45d75ec4dbc4868acf10e3a` | Ví dụ custom_data_comm và xe vl_bike_39p |

Header firmware và custom_data_comm ghi Benjamin Vedder, GPL-3.0-or-later; VESC Tool/package có LICENSE GPL-3.0. Giữ attribution/license khi trích hoặc phát triển tiếp. Snapshot package này mới hơn release P4; nó là reference, không phải bằng chứng cho runtime đã phát hành.

## 1. Trích đường ADC và nhiệt độ chuẩn

Nguồn: [commands.c 6.06, case ADC](https://github.com/vedderb/bldc/blob/94b305ec124b67b0161f2d3d85c4d5b35e48edc5/comm/commands.c#L712). Trích nguyên văn:

```c
case COMM_GET_DECODED_ADC: {
	int32_t ind = 0;
	uint8_t send_buffer[50];
	send_buffer[ind++] = COMM_GET_DECODED_ADC;
	buffer_append_int32(send_buffer, (int32_t)(app_adc_get_decoded_level() * 1000000.0), &ind);
	buffer_append_int32(send_buffer, (int32_t)(app_adc_get_voltage() * 1000000.0), &ind);
	buffer_append_int32(send_buffer, (int32_t)(app_adc_get_decoded_level2() * 1000000.0), &ind);
	buffer_append_int32(send_buffer, (int32_t)(app_adc_get_voltage2() * 1000000.0), &ind);
	reply_func(send_buffer, ind);
} break;
```

Nguồn: [SETUP/SELECTIVE, mask và hai trường nhiệt](https://github.com/vedderb/bldc/blob/94b305ec124b67b0161f2d3d85c4d5b35e48edc5/comm/commands.c#L805). Trích nguyên văn:

```c
uint32_t mask = 0xFFFFFFFF;
if (packet_id == COMM_GET_VALUES_SETUP_SELECTIVE) {
	int32_t ind2 = 0;
	mask = buffer_get_uint32(data, &ind2);
	buffer_append_uint32(send_buffer, mask, &ind);
}

if (mask & ((uint32_t)1 << 0)) {
	buffer_append_float16(send_buffer, mc_interface_temp_fet_filtered(), 1e1, &ind);
}
if (mask & ((uint32_t)1 << 1)) {
	buffer_append_float16(send_buffer, mc_interface_temp_motor_filtered(), 1e1, &ind);
}
```

Đầu nhận chuẩn: [VESC Tool commands.cpp](https://github.com/vedderb/vesc_tool/blob/dc53c658cbb89a947246034f7a00149cf79abdfc/commands.cpp#L457) đọc đúng mask và signed i16/10 cho hai nhiệt độ; [ADC decoder](https://github.com/vedderb/vesc_tool/blob/dc53c658cbb89a947246034f7a00149cf79abdfc/commands.cpp#L411) đọc bốn i32/1e6.

Đã đối chiếu trực tiếp với [RT decoder P4](../components/vesc_can/vesc_rt_data.c), [IO decoder](../components/vesc_can/vesc_io_data.c), [buffer](../components/vesc_can/buffer.c): command 32/47/51, mask bit0..21, thứ tự, big-endian và scale khớp. Với SELECTIVE có cả hai nhiệt, motor temperature nằm tại byte 7-8 gồm command, không nằm ở trường ADC.

Hai case ADC và SETUP trích xuất giống nội dung trên cả ba snapshot firmware 6.05/6.06/7.00; SHA256 của hai khối lần lượt:

- ADC: `da9d0f885668350742e4a362ad5065855a8efad32a198eaed796ae1a876c596f`.
- SETUP: `9e1bb18149b54b478f757988d5ee9fa847c9dc570855afa6e5db1b08a18421d1`.

Đây là căn cứ giữ nguyên cách đọc chuẩn khi phát triển tiếp. Không có căn cứ đổi offset/scale M-TEMP hoặc thay bằng giá trị tính từ ga. Tuy nhiên, khớp schema không chứng minh các byte P4 nhận ngoài thực tế là đúng; cần kiểm cả nguồn ESC và quá trình ghép gói.

### Không gắn cảm biến: giá trị nhiệt được tạo ở đâu

Nguồn [mc_interface.c 7.00](https://github.com/vedderb/bldc/blob/20cbb362687291242ab90b99f25fbfe8835540fc/motor/mc_interface.c#L2271) chọn công thức theo Sensor Type đang lưu. Nhánh NTC đọc đầu vào nhiệt của board. Nhánh Disabled dưới đây là trích nguyên văn, không phải mã đề xuất:

```c
	case TEMP_SENSOR_DISABLED:
		temp_motor = motor->m_temp_override;
		break;
	}
```

[Kiểm tra hợp lệ và lọc](https://github.com/vedderb/bldc/blob/20cbb362687291242ab90b99f25fbfe8835540fc/motor/mc_interface.c#L2325), trích nguyên văn:

```c
	if (UTILS_IS_NAN(temp_motor) || UTILS_IS_INF(temp_motor) || temp_motor > 600.0 || temp_motor < -200.0) {
		temp_motor = -100.0;
	}

	UTILS_LP_FAST(motor->m_temp_motor, temp_motor, MOTOR_TEMP_LPF);
```

[Khởi tạo state](https://github.com/vedderb/bldc/blob/20cbb362687291242ab90b99f25fbfe8835540fc/motor/mc_interface.c#L169) xóa về 0, bao gồm giá trị override. [Setter override](https://github.com/vedderb/bldc/blob/20cbb362687291242ab90b99f25fbfe8835540fc/motor/mc_interface.c#L1805) thay giá trị đó khi được gọi; [getter nhiệt](https://github.com/vedderb/bldc/blob/20cbb362687291242ab90b99f25fbfe8835540fc/motor/mc_interface.c#L1550) trả giá trị sau lọc cho telemetry. Không có lời gọi override nhiệt trong [Lisp gốc](../main.lisp) hoặc [Lisp release](../release/jc4880-v1.3.7-2026-09-19-7c77f2b/main.lisp).

| Trạng thái cấu hình thực tế | Kết luận từ nguồn chuẩn |
|---|---|
| Vẫn chọn NTC/PTC dù không gắn cảm biến | ESC vẫn chạy nhánh chuyển đổi đầu vào nhiệt; số nhận được không phải phép đo nhiệt motor hợp lệ. Cần đúng sơ đồ ADC của board để giải thích vì sao liên hệ với ga. |
| Chọn Disabled | Lấy giá trị override, ban đầu 0 sau khởi tạo nếu không có nguồn ghi khác; không trực tiếp đọc ADC nhiệt trong nhánh này. Nếu vẫn đổi theo ga, phải truy firmware custom, nguồn override, motor context và đường dữ liệu. |
| Kết quả tính NaN/Inf hoặc ngoài khoảng -200…600 | Thay bằng -100 trước khi lọc; nhiệt âm có thể được tạo ngay ở ESC. Đây không phải bằng chứng đã quan sát nhánh này trên thiết bị. |

Khối từ khởi tạo biến nhiệt tới phép lọc giống nhau trên 6.05/6.06/7.00 (2.900 byte LF, SHA256 `63658c15f6853bf1e6dcaa8e88020cc6e84890586d987e78adeac2b3b3e83266`). Không gắn cảm biến **không đồng nghĩa** đã chọn Disabled. Chưa kết luận có nhiễu, dùng chung chân ADC hay lỗi dây.

Đã truy tiếp các tác dụng phụ của Lisp: cả hai chỉ ghi các khóa dòng tối đa theo tỉ lệ và tốc độ lùi, dùng cùng GPIO TX/RX input pull-up; không đổi Sensor Type, không override nhiệt hay remap ADC. Trong [binding cấu hình chính thức](https://github.com/vedderb/bldc/blob/20cbb362687291242ab90b99f25fbfe8835540fc/lispBM/lispif_vesc_extensions.c#L3823), hai khóa này đi nhánh sửa trực tiếp `changed_mc=1`; [full setter](https://github.com/vedderb/bldc/blob/20cbb362687291242ab90b99f25fbfe8835540fc/lispBM/lispif_vesc_extensions.c#L4251) chỉ chạy khi bằng 2. Đã đối chiếu cả ba phiên bản: không có việc nạp lại toàn bộ cấu hình motor từ hai khóa này. [Helper giới hạn phần cứng](https://github.com/vedderb/bldc/blob/20cbb362687291242ab90b99f25fbfe8835540fc/comm/commands.c#L1917) có thể kẹp các giới hạn khác nhưng không ghi Sensor Type/override nhiệt hay remap ADC. Kết quả này không phủ nhận trigger nạp Lisp mới; nó loại bớt một đường tác động trực tiếp chưa có căn cứ.

## 2. Trích chức năng ADC/output chính thức

[app_adc.c 7.00](https://github.com/vedderb/bldc/blob/20cbb362687291242ab90b99f25fbfe8835540fc/applications/app_adc.c#L189) đọc điện áp, tính range_ok tại dòng 207, ghi decoded_level tại 248 và decoded_level2 tại 285, rồi mới tới đoạn sau:

```c
// All pins and buttons are still decoded for debugging, even
// when output is disabled.
if (app_is_output_disabled()) {
	continue;
}
```

Vì vậy khóa output không tự dừng decode ADC. Control Type NONE vẫn đi qua phần decode; **application ADC phải thực sự được bật/chạy**, khác với đặt Control Type NONE. [app.c](https://github.com/vedderb/bldc/blob/20cbb362687291242ab90b99f25fbfe8835540fc/applications/app.c#L101) mới quyết định start ADC application. Không tự chuyển app type trong quá trình kiểm tra này.

[Lisp binding](https://github.com/vedderb/bldc/blob/20cbb362687291242ab90b99f25fbfe8835540fc/lispBM/lispif_vesc_extensions.c#L1836):

```c
static lbm_value ext_app_adc_range_ok(lbm_value *args, lbm_uint argn) {
	(void)args; (void)argn;
	return app_adc_range_ok() ? ENC_SYM_TRUE : ENC_SYM_NIL;
}
```

Tất cả ba snapshot chính thức đều đăng ký adc-ctrl-type, get-adc-decoded, app-adc-range-ok, app-disable-output, set-current và set-current-rel. Do đó **không dùng giả thuyết thiếu API này cho các snapshot official đã kiểm tra**. Firmware custom/version thực trên ESC vẫn cần xác định.

Range check chính thức là read_voltage trong voltage_min..voltage_max, không phải tự đặt một ngưỡng mới. range_ok khởi tạo true; app ADC không chạy có thể làm decoded input cũ/zero nhưng không được suy ra rằng range check chắc chắn false.

[app_disable_output](https://github.com/vedderb/bldc/blob/94b305ec124b67b0161f2d3d85c4d5b35e48edc5/applications/app.c#L175) xác nhận -1 khóa vô thời hạn, số dương khóa theo timer, 0 bật output lại. [set-current binding](https://github.com/vedderb/bldc/blob/20cbb362687291242ab90b99f25fbfe8835540fc/lispBM/lispif_vesc_extensions.c#L1893) gọi timeout_reset; đối số thứ hai là current_off_delay, không phải watchdog timeout. Khi phát triển tiếp phải bám đúng các nghĩa này.

## 3. Trích custom data và ví dụ xe có sẵn

[send_app_data của VESC](https://github.com/vedderb/bldc/blob/20cbb362687291242ab90b99f25fbfe8835540fc/lispBM/lispif_vesc_extensions.c#L1371) thêm COMM_CUSTOM_APP_DATA, interface 2 gửi CAN tới can_id. [ext_send_data](https://github.com/vedderb/bldc/blob/20cbb362687291242ab90b99f25fbfe8835540fc/lispBM/lispif_vesc_extensions.c#L1411) gửi toàn bộ array->size, không biết biến pi của Lisp dự án.

Ví dụ official [custom_data_comm/code.c](https://github.com/vedderb/vesc_pkg/blob/c3965e47fb2c5787e45d75ec4dbc4868acf10e3a/c_libs/examples/custom_data_comm/code.c#L38) đóng gói và gửi chiều dài thực:

```c
int32_t ind = 0;
uint8_t buffer[10];
buffer_append_int32(buffer, d->msg_cnt, &ind);
buffer_append_float32_auto(buffer, d->msg_val, &ind);
VESC_IF->send_app_data(buffer, ind);
```

[qml.qml](https://github.com/vedderb/vesc_pkg/blob/c3965e47fb2c5787e45d75ec4dbc4868acf10e3a/c_libs/examples/custom_data_comm/qml.qml#L48) là đầu đối ứng sendCustomAppData/onCustomAppDataReceived. Đây là reference để thiết kế cặp encode/decode thống nhất. Ví dụ này dùng float32_auto/IEEE float, trong khi protocol VP dự án dùng fixed-point; **không chép riêng một đầu rồi giữ nguyên đầu còn lại**. Callback ví dụ tối giản cũng không phải một validator production đầy đủ.

Ví dụ xe [vl_bike_39p/code_stm.lbm](https://github.com/vedderb/vesc_pkg/blob/c3965e47fb2c5787e45d75ec4dbc4868acf10e3a/vl_bike_39p/code_stm.lbm#L452) đăng ký và bật event rõ ràng:

```lisp
(event-register-handler (spawn event-handler))
(event-enable 'event-can-sid)
(event-enable 'event-data-rx)
```

Ví dụ này đọc get-adc 0/1, chuẩn hóa theo calibration tại dòng 487, chạy vòng 10 ms và xuất qua set-remote-state tại 567. Nó dùng native remote control, khác motor-current arbiter của dự án. Chỉ lấy cấu trúc input/event và cách ghép hai đầu giao thức làm reference, không ghép nguyên hai controller. Cả hai Lisp dự án có handler CAN SID nhưng startup chưa thấy enable event-can-sid; điểm này liên quan helper CAN SID, không được tự coi là lỗi nút GPIO TX hoặc regression mới.

## 4. Reference UI/mode đã có trong dự án

| Luồng | Reference đã đọc | Kết luận source |
|---|---|---|
| Gesture | [touch_input.c](../main/touch_input.c), dòng 35 và 216 | Bắt đầu x<40 ở mép trái, kéo sang phải >=100 px; không kiểm tra PARK/Lisp |
| Mở drawer | [lisp_panel.c](../Super_VESC_Display/custom/lisp_panel.c), dòng 379 và 444 | Callback async, chỉ guard đã mở/không ở dashboard; tạo drawer trước khi nhận descriptor |
| Dựng/refresh panel | Cùng file, dòng 245 và 352 | BUTTON/TOGGLE của descriptor mới đã được hỗ trợ; count6->4 không tự chứng minh parser lỗi |
| Nhấn mode | Cùng file dòng 76; [transport](../components/vesc_can/vesc_lisp_panel.c) dòng 130 | Queue action id10/11/12; không gửi CAN đồng bộ từ callback LVGL |
| Chấp nhận mode | [Lisp release](../release/jc4880-v1.3.7-2026-09-19-7c77f2b/main.lisp), dòng 596 | Yêu cầu safety-fault=0 và rv-dir=1; không yêu cầu park-on=0 |

Hai file gesture/drawer không đổi giữa e7f0b16 và 7c77f2b. Người dùng đã làm rõ bảng mở được rồi thu vào khi nhả tay. Không cần tiếp tục giả thuyết “không nhận vuốt” hoặc quy lỗi do vuốt sai hướng.

PARK riêng nó không giải thích việc radio mode không đổi. Fault chốt có thể làm mode bị từ chối, nhưng không trực tiếp đóng drawer. Phải tách việc bảng tự đóng khỏi việc ESC chấp nhận lệnh mode.

Một điểm thiếu thông tin lỗi trong source mới: ride-input-fault đặt safety-fault=12, còn safety-query và status gửi rm-fault. Hai biến không luôn được đồng bộ. Do đó có thể có safety state FAULT nhưng result vẫn 0/lỗi cũ. Cần đọc cả state và hai biến khi chẩn đoán; không dùng riêng result=0 để kết luận Lisp khỏe. Chưa đổi giao thức hoặc thêm mã lỗi.

### Đường nhả tay làm bảng tự đóng

[Touch detector](../main/touch_input.c), dòng 207-210 và 233-236, vẫn chuyển tọa độ/pressed cho LVGL khi gọi mở bảng. Cờ đã nhận gesture chỉ ngăn mở lặp, không kết thúc lượt chạm. [Drawer](../Super_VESC_Display/custom/lisp_panel.c), dòng 386-400, tạo lớp nền bắt nhấn toàn màn hình trước bảng đang trượt từ x=-300 vào trong 220 ms. Lớp nền đăng ký sự kiện CLICKED. Callback dòng 346 được trích nguyên văn:

```c
static void scrim_cb(lv_event_t *e)
{
    (void)e;
    lisp_panel_close();   /* tap outside the drawer */
}
```

Đường phù hợp với triệu chứng, còn cần xác nhận trên thiết bị:

1. Ngón tay bắt đầu trên vùng dashboard không khóa lượt nhấn và không đang cuộn.
2. Bảng mở khi tay vẫn chạm; một lần hit-test chọn lớp nền trước khi bảng trượt tới vị trí ngón tay.
3. Lớp nền có PRESS_LOCK nên giữ lượt chạm, kể cả khi bảng đã trượt tới.
4. Khi nhả và không có cuộn, LVGL phát CLICKED cho lớp nền; callback đóng bảng. Không cần cú nhấn thứ hai.

Điều kiện này có căn cứ trong [LVGL input](../managed_components/lvgl__lvgl/src/core/lv_indev.c), dòng 827-844 (chọn lại target), 856-887 (chuyển target), 950-984 (release/CLICKED), và [cờ mặc định](../managed_components/lvgl__lvgl/src/core/lv_obj.c), dòng 436-438. Nếu target cũ có PRESS_LOCK, đang cuộn hoặc drawer chỉ tạo sau khi release đã được xử lý thì đường này không xảy ra. [Dashboard cockpit](../Super_VESC_Display/custom/custom.c), dòng 498-501, đã bỏ SCROLLABLE trên screen root; chưa có trace target thực tế tại mép chạm.

Reference thư viện để xử lý lượt chạm mở UI đã có sẵn: [hàm chờ nhả](../managed_components/lvgl__lvgl/src/core/lv_indev.c), dòng 278; trích nguyên văn:

```c
void lv_indev_wait_release(lv_indev_t * indev)
{
    if(indev == NULL)return;
    indev->proc.wait_until_release = 1;
}
```

Khi cờ này có hiệu lực, nhánh press bỏ qua xử lý; nhánh release gửi PRESS_LOST rồi xóa target thay vì tạo click. [Colorwheel có sẵn](../managed_components/lvgl__lvgl/src/extra/widgets/colorwheel/lv_colorwheel.c), dòng 490-495, dùng API đó sau một tương tác đổi mode. Đây là reference cần đối chiếu, chưa phải patch: callback mở bảng đang chạy async nên phải xác định đúng input device, không giả định input đang active.

Điểm phiên bản: [lockfile](../dependencies.lock), dòng 436-442, của release `7c77f2b` và workspace cùng khóa LVGL **8.3.11**, khớp [header đang dùng](../managed_components/lvgl__lvgl/lvgl.h) và compile database JC4880 hiện có. Bản kèm simulator là 8.3.10. Ghi chú “8.4” trong hướng dẫn cục bộ không khớp dependency thực tế; trích đoạn trên lấy từ 8.3.11.

Thiếu descriptor chỉ làm timer chờ tiếp, không đóng bảng. Nhánh đóng còn lại là active screen không còn dashboard tại dòng 360. Cần xác định callback nào thực sự đóng bảng. Vì source gesture/drawer không đổi, đường lỗi này là ứng viên có điều kiện, **chưa chứng minh vì sao thay riêng Lisp kích hoạt nó**; không tự gán cho số control, PARK hoặc tải CAN.

## 5. Tương tác CAN của cặp mới cần kiểm tra

[CAN VESC chuẩn](https://github.com/vedderb/bldc/blob/94b305ec124b67b0161f2d3d85c4d5b35e48edc5/comm/comm_can.c#L443) dùng gói ngắn nếu payload<=6 byte, còn lại FILL/PROCESS với CRC. Release Lisp mới trả safety 9 byte và status 19 byte; thêm command thành 10/20 byte, đều qua reassembly. Các request mới 7 byte cũng qua đường fragment, khác request status cũ 5 byte.

Không nên kết luận “thêm safety nên bus chắc chắn nặng hơn”: status cũ gửi cả pbuf128, tức 129 byte gồm command. Chỉ tính các lượt status/safety thành công, theo cơ chế chunk7 hiện có: cũ 1+20=21 CAN frame mỗi lượt; mới status 2+4 và safety 2+3, tổng 11 frame cho hai lượt. Đây là phép đếm từ source, không phải đo bus, chưa tính các luồng khác/retry/timing.

[Reassembler P4](../components/vesc_can/comm_can.c) dòng 508/555 key slot bằng eid&0xFF (địa chỉ đích), không phải sender. Callback chỉ nhận data/len, không giữ last_id; semaphore tại 590 được signal kể cả CRC fail. [VESC official](https://github.com/vedderb/bldc/blob/94b305ec124b67b0161f2d3d85c4d5b35e48edc5/comm/comm_can.c#L1622) theo dõi offset/length trước CRC/routing. Đây là khác biệt/rủi ro nguồn có sẵn cần đối chiếu khi giao dịch mới hoạt động; chưa phải bằng chứng lỗi ghép gói trên xe.

CRC fail thường làm mất gói, không tự chứng minh ADC được biến thành nhiệt độ. Trình tự wait/reply và source identity cần được ghi thực tế trước khi chọn sửa reassembly. Không áp dụng package CAN S3 hoặc thay decoder theo giả thuyết.

## Việc còn thiếu trước implementation

1. Drawer đã rõ triệu chứng; cần trace callback đóng, target/cờ touch tại lúc mở/nhả, active screen và xác định nút mode là GPIO hay trên màn hình.
2. Đọc firmware/board ESC, Motor Temperature Sensor Type, application đang chọn, ADC Control Type và điện áp/min/max; lấy lỗi runtime và safety state, safety-fault/rm-fault, motor-live, rv-dir. Chưa có các giá trị này.
3. VESC Tool đã được xác nhận cùng đổi nhiệt. Cần kiểu kết nối trực tiếp/qua P4 và đúng ESC/motor đang chọn để phân biệt nguồn ESC với đường truyền chung. Sau khi có board identity mới truy mapping đầu vào nhiệt/ga, không suy pinout từ P4.
4. Nếu làm A/B tiếp, giữ P4/ESC/config cố định và chỉ đổi Lisp; ghi nhiệt, ADC, trạng thái Lisp và packet cùng một điều kiện. Không ép nhiệt về 0 hoặc ẩn số để che regression.
5. Dùng các đoạn official bên trên làm chuẩn cho phần sửa đã xác định; giữ đúng version/API/scale/endianness và cặp encode/decode. Không đổi protocol hoặc điều khiển motor chỉ vì một giả thuyết.

Kiểm tra lượt này: 41 file nguyên bản có SHA256 trong cache ignored (ba request COPYING trả 404 được ghi riêng trong provenance); so khối ADC/SETUP/nhiệt motor ba phiên bản, đọc decoder hai phía, binding cấu hình, đường input LVGL và diff P4 baseline/release. Worker gặp lỗi executor nên root đọc nguồn và chuyển trích đoạn cho worker rà độc lập; không coi đây là ba lần chạy kiểm chứng độc lập. Không sinh implementation, không thực thi Lisp trên ESC, không build/flash và chưa xác định nguyên nhân trên thiết bị.
