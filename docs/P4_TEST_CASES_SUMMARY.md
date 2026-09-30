# Tổng hợp test case đã thực hiện cho ESP32-P4

Cập nhật: **2026-09-30**. Nhánh: `fix/gear-circle-park-reverse`.
Mốc Git khi tổng hợp: `858f853` (`docs: record diag4 publication`).
Gói mới nhất có kết quả build trong phạm vi báo cáo: **JC4880 v1.3.8-rc2-diag4**,
ngày 2026-09-30, triển khai/package tại commit `1c3bc04`.

Đây là bản tổng hợp kết quả đã lưu, **không phải một lần chạy lại test**.
PASS chỉ có hiệu lực cho source/gói và môi trường ghi tại từng hàng. Không
suy ra toàn bộ working tree hiện tại đã đạt từ một log của phiên bản trước.
Các thay đổi UI/font/simulator chưa commit không nằm trong image diag4.

## 1. Cách đọc trạng thái

- **PASS host:** kiểm thử C/Python hoặc VM chạy trên máy tính; không chạy trên P4 thật.
- **PASS simulator:** kiểm thử LVGL/giao diện Windows; không xác nhận touch GT911 thật.
- **PASS build/package:** biên dịch hoặc kiểm tra image/hash; không xác nhận vận hành.
- **Quan sát hardware:** phản hồi/snapshot từ người dùng, chỉ xác nhận điều đã ghi.
- **Chưa nghiệm thu:** chưa có kết quả phần cứng đủ để đánh dấu đạt.

VM LispBM sử dụng mã VESC thật nhưng API ESC là fixture. Không dùng kết quả VM
để kết luận PWM, mô-men, chiều motor, LED, CAN transceiver hoặc nhả phanh thực tế.
Số case/assertion giữa các đợt khác nhau không cộng thành một tổng duy nhất.

## 2. CAN, telemetry và nhiệt độ — RC2, 2026-09-28

| ID | Nội dung đã kiểm | Kết quả đã lưu | Bằng chứng |
| --- | --- | --- | --- |
| P4-CAN-01 | Ghép gói, thiếu/đảo thứ tự/nhân đôi fragment, overflow và phục hồi; DLC; sender và motor identity | **22/22 PASS host** | [CAN transport](diagnostics/rc2-rerun-2026-09-28/can-transport.txt) |
| P4-RT-01 | Gói full/selective, mọi prefix bị cắt, không refresh/mutate snapshot từ gói thiếu; không lệch field; motor identity | **16/16 PASS host** | [Telemetry](diagnostics/rc2-rerun-2026-09-28/telemetry.txt) |
| P4-TEMP-01 | Nhiệt độ độc lập ADC/current/gear; screen gate; demo exit; ESC thứ hai; thiếu nhiệt không refresh; wrap/reset và realtime viewer | **9/9 PASS host**, lấy hàm production | [Thermal UI](diagnostics/rc2-rerun-2026-09-28/thermal-ui.txt) |
| P4-TEMP-02 | Pipeline chuyển đổi sensor/ADC của VESC 6.05, giá trị lỗi, fallback và hiển thị Celsius/Fahrenheit | **603 checks, 0 failures**; đầu vào ADC giả định | [Conversion 6.05](diagnostics/rc2-rerun-2026-09-28/conversion-6.05.txt) |
| P4-TARGET-01 | Đổi target giữ đúng trạng thái active/paused của polling RT, Lisp và IO | **PASS host**, exit 0; không tự gán số lượng case | [Target polling](diagnostics/rc2-rerun-2026-09-28/target-polling.txt) |
| P4-LISP-01 | Nhánh safety, fault bị latch, native input không hỗ trợ và worker hết hạn | **17/17 PASS host** | [Lisp branch](diagnostics/rc2-rerun-2026-09-28/lisp-branch.txt) |
| P4-LISP-02 | Chạy Lisp trong VM VESC 6.05, heap 2464 | **27/27 PASS VM**, ESC API fixture | [Runtime 6.05](diagnostics/rc2-rerun-2026-09-28/lisp-runtime-6.05.txt) |
| P4-LISP-03 | Chạy bộ RC2 trên VM VESC 7.00 | **27/27 PASS VM**, ESC API fixture | [Runtime 7.00](diagnostics/rc2-rerun-2026-09-28/lisp-runtime-7.00.txt) |
| P4-UI-01 | Drawer: nhả tay sau mở ở trong/ngoài, lần tap ngoài tiếp theo, descriptor thiếu/trễ, mở lại lúc đang đóng | **9/9 PASS với LVGL thật trên host** | [Drawer](diagnostics/rc2-rerun-2026-09-28/drawer-ui.txt) |

[Báo cáo RC2](RC2_RERUN_2026-09-28.md) đối chiếu **34 case từng FAIL đã PASS**:
CAN 12, telemetry 7, thermal UI 4, drawer 6, Lisp branch 3, LispBM 2.
Đây là 34 test case, không phải 34 lỗi độc lập. [Danh mục từng case](diagnostics/rc2-rerun-2026-09-28/failed-case-coverage.json)
lưu tên case và đường dẫn baseline/rerun; baseline cũ không được chạy lại trong lượt đó.
Lệnh, thời gian và exit code nằm trong [fast-results](diagnostics/rc2-rerun-2026-09-28/fast-results.json)
và các log cùng thư mục.

### Điều tra M-TEMP/Settings trước RC2

[Báo cáo ngày 2026-09-25](P4_TEMP_SETTINGS_DIAGNOSTIC.md) ghi harness parser/freshness
old/current đều chạy **309 checks**. Sweep dòng 0..100 không đổi nhiệt giữ ở -99;
gói ADC không thay thế trường nhiệt; đổi nhiệt trong payload thay đổi snapshot.
Audit đồng thời tái hiện lỗi malformed-packet làm số cũ được đánh dấu fresh và
nhận snapshot thiếu đuôi. Vì vậy 309 checks không đồng nghĩa parser cũ không có lỗi.
Các lỗi hồi quy liên quan được theo dõi trong bảng RC2 ở trên.

Test tiêm touch tại `(678,18)`, so sánh GT911 với touch injection và các giả thuyết
nhiễu điện vẫn là đề xuất chẩn đoán trong báo cáo đó, **không phải case đã chạy**.
Chưa chứng minh một nguyên nhân end-to-end cho cả M-TEMP đổi theo ga và không vào Settings.

## 3. Mode/PARK/reverse — diag2, diag3 và diag4

| ID | Phiên bản / kích thích đã kiểm | Kết quả và giới hạn | Bằng chứng |
| --- | --- | --- | --- |
| P4-R-01 | Diag2: luồng RX/arm R, Mode 2 + chưa nhận RX, interlock khi chưa đủ phanh, arm hợp lệ rồi ga | **142/142 PASS VM 6.05**, heap 2464; lệnh dòng âm trong fixture | [Điều tra RX](REVERSE_INPUT_DIAG2_2026-09-28.md), [log](diagnostics/reverse-diag2-2026-09-28/runtime-605.txt) |
| P4-R-02 | Diag2: ưu tiên phanh/nhả phanh và last output API, ramp tối đa | **156/156 PASS VM** ở lần cuối; ba assertion ban đầu đòi đúng zero đã sửa về ngưỡng `<=0.001` và kiểm API thực gọi | [Phân tích](REVERSE_BRAKE_DIAG2_2026-09-28.md), [log cuối](diagnostics/reverse-brake-diag2-2026-09-28/runtime-605-v2.txt) |
| P4-R-03 | Diag3: đợi brake ramp về ngưỡng trước R, terminal brake-zero dùng `set-current 0`, không lặp off-delay khi dòng lùi zero | Gói diag3 **156/156 PASS VM**; canonical source **85/85** theo nhật ký; bộ tùy chọn khác nhau, không so sánh trực tiếp tổng case | [Gói diag3](../release/jc4880-v1.3.8-rc2-diag3-2026-09-29/), [nhật ký](../COLLABORATION_LOG.md) |
| P4-R-04 | Diag4: parse R `9990 dA`; từ chối `9991 dA`; range của từng field, packet thiếu, version/enum, config bị từ chối vẫn trả giá trị thật | **Parser PASS, 0 failures** | [C host tests](../release/jc4880-v1.3.8-rc2-diag4-2026-09-30/test-results/c-host-tests.txt) |
| P4-R-05 | Diag4: safety state/protocol | **2532 checks, 0 failures** | [C host tests](../release/jc4880-v1.3.8-rc2-diag4-2026-09-30/test-results/c-host-tests.txt) |
| P4-R-06 | Diag4: transport ride-mode, không gửi trong khóa | **53 checks, 0 failures; sends while locked = 0** | [C host tests](../release/jc4880-v1.3.8-rc2-diag4-2026-09-30/test-results/c-host-tests.txt) |
| P4-R-07 | Diag4: ánh xạ/độ mới của số P/1/2/3/R/- | **81 checks PASS** | [C host tests](../release/jc4880-v1.3.8-rc2-diag4-2026-09-30/test-results/c-host-tests.txt) |
| P4-R-08 | Diag4: source/safety Lisp | **17/17 PASS** | [Lisp safety](../release/jc4880-v1.3.8-rc2-diag4-2026-09-30/test-results/lisp-safety.txt) |
| P4-R-09 | Diag4: requested R `999 A`, fixture ESC min `-30 A` / max `70 A`; Mode 2 → R, phanh ưu tiên và nhả phanh | **88/88 PASS VM 6.05**, heap 2464; quan sát clamp `-30 A` trong fixture, không phải dòng đo trên xe | [Runtime](../release/jc4880-v1.3.8-rc2-diag4-2026-09-30/test-results/runtime-605.txt), [manifest](../release/jc4880-v1.3.8-rc2-diag4-2026-09-30/manifest.json) |

Các bộ 27, 85, 88, 142 và 156 checks dùng source/option/phạm vi khác nhau.
Không lấy việc số case tăng hoặc giảm làm kết luận hồi quy. Ramp 5 giây cấu hình
có thể kéo dài khoảng 8 giây trên host vì tăng theo iteration; chưa phải đo thời gian ESC thật.

Diag3 là bản cập nhật Lisp, giữ firmware P4 RC2; không có một lần build P4 diag3 mới.

## 4. UI, simulator, build và package

| ID | Nội dung | Kết quả đã ghi | Nguồn |
| --- | --- | --- | --- |
| P4-UI-02 | Diag4: vòng mode font 48, badge 80 px, 9 tình huống gear/theme, hide/re-show trên Cockpit, Classic Max, Lamborghini và Supermoto | **4/4 theme PASS simulator** | [Gear simulator](../release/jc4880-v1.3.8-rc2-diag4-2026-09-30/test-results/gear-simulator.txt) |
| P4-UI-03 | Editor migrate `75 dA`: hiển thị `7.5 A`, tăng thành `8 A`, giảm thành `7 A` | **PASS simulator** | [Gear simulator](../release/jc4880-v1.3.8-rc2-diag4-2026-09-30/test-results/gear-simulator.txt) |
| P4-UI-04 | Drawer trong gói diag4 | **9/9 PASS host/LVGL** | [Drawer](../release/jc4880-v1.3.8-rc2-diag4-2026-09-30/test-results/ui-drawer.txt) |
| P4-BUILD-01 | Build JC4880 diag4 bằng Ninja | **PASS build**; OTA 4.436.416 byte, slot 5.242.880 byte, còn 806.464 byte | [Build](../release/jc4880-v1.3.8-rc2-diag4-2026-09-30/test-results/build.txt), [manifest](../release/jc4880-v1.3.8-rc2-diag4-2026-09-30/manifest.json) |
| P4-PKG-01 | Diag4 OTA/merged, checksum và validation hash | **Hợp lệ**; merged 4.567.488 byte | [Image info](../release/jc4880-v1.3.8-rc2-diag4-2026-09-30/test-results/image-info.txt), [SHA256](../release/jc4880-v1.3.8-rc2-diag4-2026-09-30/SHA256SUMS.txt) |
| P4-PKG-02 | RC2 rerun: đối chiếu source và release trước/sau test | **59 source files và 27 release checksums khớp**; image OTA 4.479.264 byte | [RC2 rerun](RC2_RERUN_2026-09-28.md) |

Nhật ký local ngày 2026-09-30 còn ghi simulator build/gear self-test PASS sau
khi chuyển tâm badge Cockpit/Classic Max xuống `(400,320)`. Thay đổi này **chưa
commit, chưa rebuild firmware và còn chờ người dùng duyệt hình ảnh** ở thời điểm
tổng hợp; không gộp nó vào kết quả image diag4 hoặc đánh dấu đã phát hành.

## 5. Các kiểm thử lịch sử khác của dự án P4

Nguồn của bảng này là [COLLABORATION_LOG.md](../COLLABORATION_LOG.md), các entry
BMS/ride-mode trong tháng 8. Đây là kết quả đã ghi trong lịch sử, không phải
bộ hồi quy vừa chạy trên diag4. Không suy ra BMS hoặc serdes mới nhất đã được retest.

| ID | Kiểm thử đã ghi | Kết quả / giới hạn |
| --- | --- | --- |
| P4-BMS-01 | JK parser: frame split, checksum, unknown layout, resync, command framing; sau đó thêm 24/32S, mask và alarm | Các đợt mở rộng 8, 9 và 12 nhóm **PASS host, 0 failures**; không phải BLE/BMS thật |
| P4-BMS-02 | BMS UI gauge, dấu dòng sạc/xả, build simulator | **Build simulator PASS**, JK parser 12 nhóm PASS; dấu dòng trên charger/regen/load thật còn chờ xác nhận |
| P4-SERDES-01 | VESC config serialization/deserialization | Nhật ký checkpoint ghi **ALL PASS host**; không thay cho ghi cấu hình ESC thật |
| P4-PARSER-OLD | Ride-mode parser 12 nhóm và mutation cố tình làm yếu validation | **12 nhóm PASS**; mutation tạo 24 failures như mong đợi, chứng minh test phát hiện biến thể lỗi |
| P4-LINT-OLD | Lisp cân bằng ngoặc, const block, định nghĩa hàm, message ID và kiểm chiều dài | Kiểm tĩnh PASS; `lisp_lint_test.dart` tại checkpoint đó **NOT RUN**, Flutter không có trên PATH |

Không có bằng chứng trong tập nguồn đã đối chiếu để đánh dấu một bộ nghiệm thu
Android Auto, Wi-Fi/BLE OTA hoặc C6/BT-agent OTA mới đã PASS cùng gói diag4.

## 6. Những gì đã quan sát trên phần cứng

Nguồn: [checkpoint hardware 2026-09-28](HARDWARE_STATUS_2026-09-28.md).
Thiết bị ghi trong nguồn: **P4 JC4880 RC2 + Lisp diag2 + VESC 6.05**.

| ID | Quan sát/phản hồi | Kết luận được phép |
| --- | --- | --- |
| P4-HW-OBS-01 | Người dùng xác nhận các mode hoạt động, trừ R | Xác nhận định tính; không thay thế biên bản từng case 1/2/3 |
| P4-HW-OBS-02 | Sau nạp diag2 đọc được PARK, fault 0, không có mã chẩn đoán | Startup quan sát được; chưa nghiệm thu mọi lần boot/fault |
| P4-HW-OBS-03 | Sau nhả phanh: ADC phanh 0, ramp 0, RX=1, armed=0, dir=0, heartbeat mới | INTERLOCK chưa arm; không chứng minh phanh vật lý bị giữ |
| P4-HW-OBS-04 | Giữ RX và phanh đồng thời: RX=1, brake≈1, ticks=20, armed=1, dir=0, tốc độ gần 0, ga 0, PARK=0 | Đạt mẫu REVERSE_READY; còn thiếu chuỗi tiếp theo chứng minh nhả phanh/chạy lùi |

Lỗi còn mở theo mô tả người dùng: **bóp phanh, giữ R rồi nhả phanh, đèn báo R
trên ESC vẫn sáng**. Trước đó đèn được gọi là đèn phanh; chưa xác định đúng chức
năng LED. Không lấy LED làm bằng chứng duy nhất về command hoặc lực phanh.
Diag3/diag4 có sửa và PASS trên host, nhưng chưa có kết quả hardware mới đủ để đóng lỗi.

## 7. Các case còn phải nghiệm thu trên JC4880 + VESC

Thực hiện theo [TEST_CASES diag4](../release/jc4880-v1.3.8-rc2-diag4-2026-09-30/TEST_CASES.md)
và các cổng phần cứng của [kế hoạch P/R](RIDE_GEAR_PARK_TASKS_TEST_PLAN.md).
Không đánh dấu các hàng dưới là PASS chỉ vì đã viết checklist hoặc gửi gói cho người dùng.

| Case trong gói | Phần còn thiếu | Trạng thái |
| --- | --- | --- |
| H01–H03 | Boot với đúng cặp firmware/Lisp; so M-TEMP với Tool; mode và drawer/touch thật | Chưa nghiệm thu diag4 |
| H04–H06 | Arm khi đứng yên/ga 0; giữ R rồi nhả phanh; Tool thoát brake state; ga nhẹ quay lùi | Chưa nghiệm thu; lỗi R còn mở |
| H07–H08 | Phanh ưu tiên khi lùi và nhả phanh không dùng ramp lực kéo cũ | Chưa nghiệm thu phần cứng |
| H09–H10 | Lưu dòng R mới và clamp theo ESC | Chưa nghiệm thu; `999 A` chỉ là requested, không phải mức dòng thử thực tế |
| H11 | Nhả R: interlock/điều kiện quay về tiến, không cấp lực tiến sớm khi còn lăn lùi hoặc giữ ga | Chưa nghiệm thu phần cứng |
| H12 | Mất gói/fault, tuổi dữ liệu và quyền điều khiển thực | Chưa nghiệm thu; mất dữ liệu đơn thuần không được suy ra đã PARK |
| Bổ sung | Reboot/stop Lisp, lỗi worker, mất CAN/P4, lưu cấu hình, sensor/GT911 và các cổng inhibit | Theo kế hoạch; chưa có biên bản đạt đầy đủ |

Mỗi lần nghiệm thu cần ghi mã case, model/firmware ESC, board P4, hash firmware
và Lisp, CAN ID, bước tái hiện, kết quả mong đợi/thực tế, log/ảnh và người xác nhận.
Firmware P4 và Lisp VESC là hai phần nạp riêng; không giả định nạp P4 đã cập nhật Lisp.

## 8. Kiểm tra của lần cập nhật tài liệu này

- Đối chiếu báo cáo, manifest và log có sẵn; kiểm đường dẫn bằng chứng và diff Markdown.
- Không chạy lại build, test host, simulator hoặc test phần cứng; không flash/ghi ESC.
- Commit chỉ chứa báo cáo này và entry nhật ký của lần tổng hợp; giữ nguyên các
  sửa đổi source/font/simulator và entry nhật ký đã tồn tại trước đó ở local.
