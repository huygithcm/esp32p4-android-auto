# Kế hoạch xác minh và cải thiện `lisp/main.lisp`

Ride-mode backend đã viết xong và build sạch, nhưng **chưa một dòng Lisp nào
chạy trên VESC thật**. Tài liệu này ghi lại: nguồn đối chiếu nào đang có, lời
gọi nào đã xác minh được, lời gọi nào chưa, và làm gì tiếp theo.

Viết sau khi rà source phát hiện năm lỗi mà build sạch không thấy — trong đó
hai lỗi an toàn. Giả định "compile được nghĩa là đúng" đã sai đủ nhiều lần
trong dự án này để không dùng lại.

---

## 1. Nguồn đối chiếu hiện có

| Nguồn | Vị trí | Phạm vi |
|---|---|---|
| Reference nội bộ | `flutter-application/lib/agent/lisp_reference.dart` | 380 dòng, curated cho **chính script này** |
| Danh sách keyword | `flutter-application/lib/ui/lisp_syntax.dart` | tô màu cú pháp, không phải đặc tả |
| Linter | `flutter-application/lib/agent/lisp_lint.dart` | quy tắc `@const-start`, buffer, mutable def |
| Golden test | `flutter-application/test/lisp_lint_test.dart` | `lisp/main.lisp` **là** fixture của nó |

**Chưa có**: source LispBM/VESC gốc. `research/_sources/` chỉ có
`esphome-jk-bms`. Đây là lỗ hổng chính.

---

## 2. Trạng thái từng lời gọi

### Đã xác minh trong reference nội bộ

| Lời gọi | Ghi chú từ reference |
|---|---|
| `gpio-configure` / `gpio-read` | `'pin-mode-in-pu` hợp lệ |
| `eeprom-store-i` / `eeprom-read-i` | **"nil if unset"** — xem §3 |
| `conf-set 'max-speed` | m/s |
| `conf-set 'l-current-max-scale` | 0..1 |
| `set-current amps optOffDelay` | dùng cho dòng âm |
| `get-speed` | m/s, có dấu |
| `shutdown-hold` | giữ nguồn khi ghi |

### Chưa xác minh — rủi ro thật

| Lời gọi | Vì sao rủi ro | Hậu quả nếu sai |
|---|---|---|
| `spawn-trap` | Chỉ có trong danh sách keyword, không có trong reference | Script không load → mất cả panel lẫn arbiter |
| `'pin-ppm` | Reference chỉ nêu `pin-rx`/`pin-tx` | `gpio-configure` ném lỗi — **đã được `spawn-trap` che**, nhưng chỉ khi `spawn-trap` đúng |
| `conf-set 'min-speed` | Không có trong danh sách param của reference | Ném lỗi **trong packet handler** → chết thread panel |
| `mod`, `abs` | Dùng trong code sẵn có nhưng không liệt kê | Thấp |

Ba dòng đầu **phụ thuộc lẫn nhau**: `spawn-trap` tồn tại để chặn `pin-ppm` sai.
Nếu `spawn-trap` sai thì lớp bảo vệ biến mất đúng lúc cần nhất.

---

## 3. Lỗi đã sửa nhờ đọc reference

`eeprom-read-i` trả **nil** cho slot chưa từng ghi, và `=` với nil là **type
error, không phải false**. `rm-load` chạy lúc load script, nên trên **mọi board
mới** nó sẽ ném lỗi và **toàn bộ script không load được**.

Đã sửa: kiểm magic bằng `(and magic (= magic rm-ee-tag))`. Nếu magic khớp thì
`rm-store` đã ghi mọi field trước đó, nên các lần đọc sau là an toàn.

Lỗi này không compiler nào bắt được, không test host nào chạm tới, và sẽ biểu
hiện thành *"nạp script xong màn hình không có gì"* — một triệu chứng dễ đổ cho
CAN hoặc cho màn hình.

---

## 4. Việc cần làm, theo thứ tự phụ thuộc

### Bước 1 — Chạy linter chính thức (không cần phần cứng)

```bash
cd flutter-application && flutter test test/lisp_lint_test.dart
```

`lisp/main.lisp` là golden fixture: nó **phải** lint sạch. Máy hiện tại không có
`flutter` trên PATH nên chưa chạy được. Tôi chỉ dựng lại được 3 trong số các
quy tắc của linter.

**Chặn**: mọi bước dưới.

### Bước 2 — Lấy source VESC/LispBM về `research/_sources/`

```bash
mkdir -p research/_sources && cd research/_sources
git clone --depth 1 https://github.com/vedderb/bldc
git clone --depth 1 https://github.com/svenssonjoel/lispBM
```

Rồi kiểm ba thứ:

```bash
grep -rn "spawn-trap"  bldc/lispBM/ lispBM/src/
grep -rn "pin-ppm"     bldc/lispBM/lispif_vesc_extensions.c
grep -rn "min-speed"   bldc/lispBM/lispif_vesc_extensions.c
```

Cả ba đều là câu hỏi **có/không**, trả lời được offline, và mỗi câu loại bỏ
một rủi ro ở §2. Đây là bước có tỉ lệ lợi ích trên công sức cao nhất.

`research/_sources/` đã bị gitignore theo quy ước sẵn có của repo.

### Bước 3 — Bổ sung vào reference nội bộ

Những gì bước 2 xác nhận cần được ghi vào
`flutter-application/lib/agent/lisp_reference.dart`, vì đó là thứ agent đọc
lần sau. Cụ thể cần thêm: `spawn-trap`, danh sách tên `pin-*` đầy đủ, và danh
sách param `conf-set` đầy đủ thay vì ba cái đang có.

Reference thiếu ba mục này chính là lý do §2 tồn tại.

### Bước 4 — Bench, bánh xe nhấc khỏi mặt đất

Theo `docs/RIDE_MODE_REVERSE_BE_CONTRACT.md` §12. **Chạy case 2 trước**:

> Boot với nút R giữ/chập GND: reverse không arm.

Vì đó chính là case vừa hỏng — `rv-btn` khai báo là 0 và vòng lặp kiểm nó
trước khi debounce ra kết quả, nên `rv-seen-release` được set ngay tick đầu.
Một lỗi an toàn mà build sạch, và nó nằm đúng ở case mà bench sẽ thử.

Thứ tự còn lại giữ nguyên như contract.

### Bước 5 — Chân connector nút lùi

**Chưa được đoán.** Cần model ESC hoặc schematic để map tên `PPM`/`GND` sang
chân vật lý. Cho tới lúc đó reverse ở trạng thái disabled và báo
`UNSUPPORTED_HARDWARE`, đúng như contract yêu cầu.

---

## 5. Cải thiện đề xuất, ngoài phạm vi xác minh

Xếp theo giá trị trên rủi ro:

### 5.1 Nhãn panel đang nói dối

`panel-send-ui` vẫn ghi cứng `"Slow 5 km/h"`, `"Medium 10 km/h"`,
`"Fast 20 km/h"`. Giờ tốc độ sửa được, nên các nhãn này chỉ đúng với giá trị
mặc định. Rider đặt mode 1 thành 8 km/h vẫn thấy chữ "Slow 5 km/h".

Sửa: dựng nhãn từ `rm-speed-of`. Chi phí thấp, đây là thứ đầu tiên người dùng
nhìn thấy sai.

### 5.2 `lisp/README.md` chưa cập nhật

Vẫn mô tả ba profile cố định. README này là thứ người khác đọc trước khi sửa
script.

### 5.3 Test cho phần Lisp không chỉ dừng ở lint

Hiện chỉ có lint tĩnh. Không có gì kiểm được `rm-apply-set` từ chối đúng theo
thứ tự, hay checksum EEPROM round-trip đúng. Một bộ test chạy LispBM trên host
(`lispBM/repl`) nạp `main.lisp` với các hàm phần cứng được stub lại sẽ phủ
được §12 "Host/static" một cách thật sự, thay vì phủ bằng mắt.

Đây là việc lớn nhất trong danh sách, và cũng là việc duy nhất thay đổi được
tình trạng "không có gì kiểm Lisp ngoài đọc".

### 5.4 `rm-fault` chưa bao giờ được xoá

Nó giữ mã lỗi của lần từ chối gần nhất mãi mãi. Nên xoá về 0 khi có một SET
thành công, nếu không màn hình sẽ hiện một lỗi cũ bên cạnh một config vừa lưu
thành công.

### 5.5 Message `0x0A` là sai lệch so với contract

Contract liệt kê `0x07`/`0x08`/`0x09` và để ngỏ cơ chế poll status. Tôi thêm
`0x0A REQ_RIDE_STATUS` vì cách hiển nhiên — SELECT lại profile đang cache — là
một lệnh ghi đội lốt lệnh đọc và sẽ kéo mode về mỗi khi rider bấm nút TX.

Cần Codex xác nhận id này trước khi FE bám vào.

---

## 6. Tóm tắt trạng thái

| Hạng mục | Trạng thái |
|---|---|
| Transport C + parser | Xong, 12 nhóm test PASS, mutation check PASS |
| Lisp data model + EEPROM | Xong, chưa chạy |
| Lisp protocol handlers | Xong, chưa chạy |
| Reverse state machine | Xong, mặc định tắt, chưa chạy |
| Linter chính thức | **Chưa chạy** — thiếu Flutter |
| Xác minh API với source gốc | **Chưa làm** — thiếu source |
| Bench | **Chưa làm** — thiếu chân connector |

Ba dòng cuối là toàn bộ khoảng cách giữa "viết xong" và "dùng được".
