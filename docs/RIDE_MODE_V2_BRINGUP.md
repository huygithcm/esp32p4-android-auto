# Ride modes v2 — đưa lên phần cứng thật

Chưa một dòng Lisp v2 nào chạy trên VESC. Tài liệu này là thứ tự thao tác, dấu
hiệu đúng/sai ở mỗi bước, và chỗ dừng lại nếu sai.

Bản đồ nút **đã đổi**:

| Nút | Trước (v1) | Bây giờ (v2) |
|---|---|---|
| `TX` | đổi mode / giảm cruise | **đổi Mode**, chỉ vậy |
| `RX` | bật/tắt cruise | **giữ để lùi** |

Cruise đã bị xoá hoàn toàn.

---

## Bước 0 — Trước khi đụng vào xe

**Kê bánh sau lên giá, bánh không chạm đất.** Mọi bước từ 4 trở đi đều phát
mô-men. Không có bước nào trong tài liệu này được làm khi xe đang trên mặt đất.

Ghi lại `Motor Current Max` hiện tại trong VESC Tool (Motor Settings → General).
Toàn bộ v2 xoay quanh con số này; nếu nó là 70 A thì mode đặt 100 A sẽ hiện
`70 A (capped from 100 A)`, và đó là **đúng**, không phải lỗi.

---

## Bước 1 — Nạp firmware P4

```
build_jc4880/esp32p4_android_auto-jc4880-bms-merged.bin   →  offset 0x0
```

```bash
esptool.py --chip esp32p4 -p COM<x> write_flash 0x0 \
    esp32p4_android_auto-jc4880-bms-merged.bin
```

**Đạt:** boot lên dashboard, không panic loop, log có `sdio` chứ không phải
`spi: Resetting slave`.

Bước này độc lập với Lisp. Firmware mới nói protocol v2; script cũ trên VESC
chưa biết `0x07..0x0A` nên tab Ride Modes sẽ hiện *backend unavailable*. Bình
thường cho tới bước 2.

---

## Bước 2 — Nạp `lisp/main.lisp` lên VESC

VESC Tool → **Lisp** → mở `lisp/main.lisp` → **Upload** (không phải Run).

**Đạt:** console Lisp in ra

```
ride config: no eeprom block, using defaults
Mode 1: 50.0 A req, 50.0 A effective
```

Dòng đầu là bình thường trên board chưa từng lưu config v2 — kể cả board đã
từng chạy v1, vì magic EEPROM đã đổi và block cũ **phải** đọc ra là không có.

**Hỏng — script không load, console im hoặc báo lỗi:** đây là rủi ro đã biết.
Ba lời gọi chưa xác minh được từ máy dev: `spawn-trap`, `'pin-rx` trong
`gpio-configure`, và `conf-set 'min-speed`. Chép nguyên văn dòng lỗi và dừng
lại — đừng sửa mò, xem `docs/LISP_VERIFICATION_PLAN.md` bước 2.

---

## Bước 3 — **Reboot VESC. Bắt buộc.**

Cắt nguồn VESC rồi bật lại.

Script v1 từng gọi `conf-set 'max-speed` để giới hạn tốc độ theo mode. v2 không
bao giờ đụng `max-speed` nữa — nghĩa là **nó cũng không dọn giá trị cũ đi**.
`conf-set` chỉ ghi RAM, nên một lần power-cycle là xong; bỏ qua bước này thì xe
vẫn bị giới hạn ở tốc độ mà v1 đặt lần cuối, và không có gì trên màn hình giải
thích tại sao.

**Đạt:** sau khi boot lại, `Motor Settings → Max Speed` trong VESC Tool đúng
bằng giá trị bạn đã lưu, không phải giá trị v1 áp lúc chạy.

---

## Bước 4 — Mode hoạt động, không cần nút

Mở tab **Ride Modes** trên màn hình.

**Đạt:**
- ba tab Mode hiện `50 / 70 / 100 A`;
- dòng **Effective now** của Mode 3 hiện `70 A (ESC cap)` nếu ESC là 70 A;
- dòng trạng thái góc phải: `Active M1 - 50 A`.

Đổi sang Mode 3 bằng nút TX. **Đạt:** dòng trạng thái đổi thành
`Active M3 - 70 A (capped from 100 A)`, và có tiếng bíp theo mode.

**Kiểm tra `sync-current-scale` — đây là thứ chỉ v2 mới có:** để nguyên Mode 3,
vào VESC Tool hạ `Motor Current Max` từ 70 xuống 60 A rồi Apply.

**Đạt:** dòng trạng thái tự đổi sang `60 A (capped from 100 A)` trong vòng một
giây, **không cần** đổi mode hay nạp lại gì. Nhớ trả lại 70 A sau khi thử.

---

## Bước 5 — Sửa và lưu

Đặt Mode 1 = `30 A`, bấm **Save**.

**Đạt:** `Saved` màu xanh, và console Lisp in `Mode 1: 30.0 A req, 30.0 A
effective`.

Thử phần bị từ chối: vặn nhẹ tay ga rồi bấm Save.

**Đạt:** bị từ chối với `Throttle not released`. Config trên màn hình **quay về
đúng giá trị xe đang giữ**, không nằm lại ở số vừa gõ.

Power-cycle VESC. **Đạt:** Mode 1 vẫn là 30 A, và luôn boot vào **Mode 1** dù
trước đó đang ở mode nào.

---

## Bước 6 — Đấu nút lùi vào RX

**Chỉ làm khi bước 1–5 đã đạt.**

```
ESC RX  ──── nút thường-mở ──── ESC GND
```

Không cấp điện áp ngoài vào chân RX. Nút phải là dry contact. Đây chính là dây
nút cruise cũ — nếu bạn đã đấu cruise thì **không phải đấu lại gì**.

Bật `Reverse enabled` trong tab Reverse, đặt `3.0 km/h` và `7.0 A`, bấm Save.

**Đạt:** lưu thành công.
**Hỏng — `Unsupported hardware`:** `monitor-reverse` không configure được RX.
Reverse ở trạng thái tắt, xe vẫn chạy tiến bình thường. Dừng và báo lại.

---

## Bước 7 — Mười ca bench, bánh vẫn treo

Chạy **đúng thứ tự**. Ca 2 chạy trước tiên trong nhóm lùi vì nó chính là ca đã
từng hỏng.

| # | Làm | Đạt |
|---|---|---|
| 1 | Boot với RX **nhả** | Mode 1 được áp, `R button: released` |
| 2 | Boot với RX **giữ / chập GND** | **Không arm.** `R button: PRESSED` nhưng `safe`. Nhả ra rồi mới arm được |
| 3 | Quay bánh tiến bằng ga, rồi nhấn RX | **Không có dòng âm.** Mô-men bị cắt, xe trôi tự do |
| 4 | Dừng, nhả ga, giữ RX, bóp phanh ≥ 200 ms | `ARMED`, **và pill mode trên dashboard đổi thành `MODE R`** |
| 4b | Giữ **cả ga lẫn phanh** 200 ms | **Không arm.** Đây là lỗ hổng đã sửa |
| 5 | Giữ RX, nhả phanh, tăng ga từ từ | Bánh quay **lùi**, dòng ≤ giá trị đã đặt |
| 6 | Đang lùi thì **bóp phanh** | Dòng lùi **ramp về 0**, vẫn giữ arm. Nhả phanh thì lùi tiếp |
| 7 | Đang lùi thì **nhả RX** | Dòng về 0. Ga tiến bị khoá tới khi xe dừng **và** ga đã nhả |
| 8 | Trong lúc lùi, bấm TX / đổi mode trên màn hình | **Bị bỏ qua**, mode không đổi |
| 9 | Lùi chạm 3 km/h | ESC taper dòng theo `min-speed` |
| 10 | Power-cycle sau khi Save | Config còn, mode về 1 |

**Chỉ hạ xe xuống đất sau khi cả mười ca đạt.** Ra đường bắt đầu ở 3 km/h và
7 A.

---

---

## Chỉ báo lùi trên màn hình chính

Không phải chỉ tab Reverse mới thấy. Pill mode trên dashboard chính đổi thành
**`MODE R`** ngay khi reverse được arm **hoặc** khi hướng khác "tiến" — hai
trạng thái đó với người lái là một: cú vặn ga tiếp theo sẽ đi lùi.

Overlay trên nền video Android Auto cũng dùng đúng tín hiệu đó, nên chỉ báo
không biến mất khi đang chiếu điện thoại.

Tab Reverse vẫn có ba dòng chi tiết hơn cho lúc bench: `R button`,
`Interlock`, `Direction`.

**Nếu pill không đổi thành `MODE R` khi đã `ARMED`:** gói DASH không tới nơi.
Kiểm tra script Lisp còn sống không — `panel-send-dash` chạy trên thread panel,
và một lỗi ở đó giết thread mà dashboard vẫn hiện số cũ.

---

## Khi báo lỗi

Gửi bốn thứ: `Motor Current Max` của ESC, dòng `Mode N: ... req ... effective`
trong console Lisp, dòng trạng thái trên màn hình, và số ca bench bị hỏng. Bốn
cái đó đủ định vị gần hết mọi trường hợp trên.
