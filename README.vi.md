🇻🇳 **Tiếng Việt** | 🇬🇧 [English](README.md) | 🇷🇺 [Русский](README.ru.md)

# VESC Dashboard cho ESP32-P4 🛴⚡

[![ESP-IDF](https://img.shields.io/badge/ESP--IDF-v5.5%2B-blue)](https://github.com/espressif/esp-idf)
[![Target](https://img.shields.io/badge/target-ESP32--P4-red)](https://www.espressif.com/en/products/socs/esp32-p4)
[![License](https://img.shields.io/badge/license-GPL--3.0-green)](LICENSE)
[![Board](https://img.shields.io/badge/board-Waveshare%204.3%22-orange)](https://www.waveshare.com/esp32-p4-wifi6-touch-lcd-4.3.htm)
[![VESC](https://img.shields.io/badge/VESC-CAN-success)]()

**Dashboard cảm ứng 800×480 mã nguồn mở cho xe điện chạy VESC** — ván trượt
điện, xe đạp điện, xe scooter, xe điện DIY. Hiển thị realtime phần trăm pin,
tốc độ, nhiệt độ motor / ESC, điện áp, dòng điện, odometer — lấy trực tiếp từ
bus CAN của VESC. Đồng thời làm cầu BLE cho VESC Tool, nên bạn vẫn tinh chỉnh
được controller trong lúc dashboard đang chạy.

**Bonus**: cùng thiết bị đó còn nói được giao thức Android Auto Wireless gốc —
ghép đôi điện thoại qua Bluetooth là Google Maps / Spotify / Cycleway chiếu
thẳng lên chính màn hình này. Không cần cài app nào trên điện thoại.

![VESC dashboard — pin, tốc độ, điện áp, odometer, nhiệt độ motor / ESC](docs/images/hero.jpg)

> 🎬 Video chạy thử ngắn: [`docs/demo.mp4`](docs/demo.mp4) · [`docs/demo-2.mp4`](docs/demo-2.mp4) · [`docs/demo-3.mp4`](docs/demo-3.mp4)

---

## ✨ Tính năng

### 🛴 Chính — dashboard VESC
- **Telemetry realtime qua CAN**: % pin, tốc độ, điện áp, dòng tiêu thụ, nhiệt
  độ motor và FET, odometer, quãng đường chuyến, ước lượng tầm hoạt động, báo
  cruise control.
- **Theo dõi chuyến đi** — cửa sổ thống kê chuyến ngay trên màn hình (quãng
  đường, Wh, hiệu suất, biểu đồ) + nút **Reset trip**, dữ liệu nằm trong một
  log vòng thô trên partition flash `triplog` riêng nên sống sót qua reboot.
- **Menu cấu hình VESC Tool ngay trên thiết bị** — đọc / ghi cấu hình motor và
  app của controller thẳng từ màn hình dashboard, chạy FOC detection, giữ bản
  backup config / trip — không cần laptop. Bảng tham số được sinh code tự động
  từ file XML của VESC Tool.
- **Cầu BLE NUS** — VESC Tool kết nối qua Bluetooth Low Energy là nói chuyện
  được thẳng với controller thông qua dashboard này, không cần adapter rời.
- **Màn hình Settings** trên thiết bị: tốc độ CAN, ID controller, đơn vị / tuỳ
  chọn được lưu lại, trình xem log ring buffer nằm trong PSRAM.
- **OTA qua HTTP** kèm thanh tiến trình trên màn hình (`scripts/ota_push.sh`).

### 📺 Bonus — Android Auto không dây
- Chiếu **Android Auto Wireless** gốc lên chính panel 800×480 này — không cần
  APK `Wireless Helper`, không cần mẹo developer-mode. Ghép đôi Classic BT một
  lần, sau đó mỗi lần bật nguồn là AA tự khởi động.
- Giải mã video H.264 bằng `esp_h264` (decoder phần mềm) + chuyển đổi
  YUV420 → RGB565 có tăng tốc bằng PPA. Chạy native 800×480 @ ~10–15 fps.
- Cảm ứng được forward về điện thoại (bộ điều khiển điện dung GT911).
- Tiếng bíp / phản hồi chạm của giao diện (kênh System audio — Gearhead bắt buộc).
- Tự kết nối lại với điện thoại đã ghép đôi lần cuối.

![Điều hướng Cycleway của Android Auto chiếu trên cùng màn hình](docs/images/aa-bonus.jpg)

### 📱 App companion (tuỳ chọn)
`aa_bridge` — một app Flutter nhỏ ([`flutter-application/`](flutter-application/))
nói chuyện với head unit qua BLE, hoạt động độc lập với Android Auto:
- **Cầu thông báo & media** — thông báo của điện thoại và metadata / ảnh bìa
  bài đang phát được mirror lên dashboard.
- **Đồng hồ** — đẩy giờ local qua BLE để dashboard hiển thị `HH:MM` mà không
  cần RTC / pin cúc áo trên thiết bị.
- **Cập nhật firmware qua WiFi hoặc BLE** — image của P4 được đóng gói sẵn
  trong APK và nạp vào head unit qua SoftAP của nó (mật khẩu đọc tự động qua
  BLE, hoặc tự nhập / quét), hoặc nạp thẳng qua đường BLE.
- **Quản lý file thiết bị** — duyệt bộ nhớ head unit (`/vescfs` LittleFS nội
  bộ + thẻ microSD) từ điện thoại qua BLE: liệt kê, tải về, tải lên, đổi tên,
  xoá. Đây là cách bạn bỏ file GIF splash lúc boot vào thiết bị.

### 🗂️ Trình duyệt file trên thiết bị + splash lúc boot
- **Trình duyệt file** ngay trên head unit (*Settings → Files*) — điều khiển
  bằng cảm ứng, duyệt `/vescfs` (bộ nhớ trong) và `/sdcard` (thẻ microSD):
  thư mục, dung lượng, xem trước ảnh, đổi tên / di chuyển / xoá.
- **Splash lúc boot** — thả một file `splash.gif` động vào `/vescfs`, nó sẽ
  phát full màn hình trong lúc boot rồi bàn giao cho dashboard. Không có file
  thì boot bình thường.

### 📦 Bên trong
- **Firmware phụ được nhúng sẵn**: ESP32-C6 (WiFi slave `esp-hosted`) và BT
  agent trên D1 Mini đều được đóng gói bên trong binary chính, tự động nạp qua
  SDIO / UART khi phát hiện lệch version.
- Layout **dual-OTA** trong 32 MB flash (cả hai slot nằm dưới mốc 16 MB), cộng
  thêm partition `triplog` ~12 MB riêng ở phía trên cho log chuyến đi thô.

---

## 🔧 Phần cứng

| Linh kiện | Dùng cho | Ghi chú |
|---|---|---|
| **Waveshare ESP32-P4-WIFI6-Touch-LCD-4.3** | luôn cần | [Nơi bán](https://www.waveshare.com/esp32-p4-wifi6-touch-lcd-4.3.htm). Bộ não chính — ESP32-P4 + ESP32-C6 WiFi trên board, màn 800×480 ST7701 MIPI-DSI, cảm ứng GT911. |
| **Controller VESC có ngõ CAN** | dashboard VESC | Bản nào có CAN cũng được. Đây là mục đích chính. |
| **IC thu phát CAN TJA1051** (nên chọn bản `T/3`) | dashboard VESC | Chuyển mức CAN 5V → 3.3V |
| **Mạch hạ áp DC-DC 12V → 5V** (≥1 A) | dashboard VESC | Lấy nguồn từ VESC / pin xe |
| **Module ESP32-WROOM-32** (hoặc dev board bất kỳ có nó) | bonus Android Auto | Hàn thẳng một module WROOM-32 trần lên board P4 là chạy tốt — bật `BT_AGENT_OTA_ENABLED=y` trong `idf.py menuconfig` là P4 tự nạp firmware cho nó qua UART + chân RST/BOOT, không cần mạch USB-to-serial. Dùng D1 Mini ESP32 (hoặc dev board ESP32 USB-C bất kỳ có BT Classic) cũng được nếu bạn thích tự nạp. ESP32-P4 / C6 không có BT Classic. Giá ~$2–3. |
| Dây jumper, cáp USB-C | luôn cần | |

Nếu bạn không quan tâm Android Auto thì bỏ hẳn con WROOM đi cũng được —
dashboard chạy độc lập bình thường. Không nối gì vào UART và để
`BT_AGENT_OTA_ENABLED` ở mặc định `n` thì P4 sẽ lặng lẽ bỏ qua đường BT mỗi
lần boot.

---

## 🔌 Đấu dây

<!-- TODO: ảnh cận cảnh header J3 đã đấu VESC + D1 Mini -->

**Sơ đồ chân phụ thuộc vào board.** Các sơ đồ dưới đây vẽ cho
**Waveshare 4.3"** (board mặc định). Với **Guition JC4880P443C** thì lấy số ở
cột JC4880; nguồn, chiều tín hiệu và cầu phân áp trên CAN RX thì giống nhau ở
cả hai board.

| Tín hiệu | Waveshare 4.3" | JC4880P443C |
|---|---|---|
| CAN TX → TXD của transceiver | GPIO 48 | GPIO 51 |
| CAN RX ← RXD của transceiver | GPIO 47 | GPIO 52 |
| BT agent: P4 TX → WROOM RX | GPIO 22 | GPIO 33 |
| BT agent: P4 RX ← WROOM TX | GPIO 21 | GPIO 31 |
| BT agent RST → WROOM EN | GPIO 24 | GPIO 30 |
| BT agent IO0 → WROOM GPIO 0 | GPIO 25 | GPIO 29 |

### 1. Đường nguồn

```text
 Pin 12 V            ┌────────────┐       ┌──────────────────┐        ┌────────────┐
 (bus VESC)  ───────▶│   DC-DC    │──────▶│  board ESP32-P4  │───────▶│ WROOM 3V3  │
              +12 V  │ 12 V → 5 V │ +5 V  │  USB-C / chân 5V │  3V3   │ (tuỳ chọn) │
                     │   ≥ 1 A    │       │  (LDO trên board)│  (J3)  └────────────┘
                     └────────────┘       └──────────────────┘
```

> ⚠️ Nếu bạn đấu D1 Mini cho phần bonus AA, **hãy cấp nguồn từ chân 3V3 của
> P4, đừng cấp từ 5V**. Con AMS1117 trên D1 Mini sẽ đốt phần dư thành nhiệt
> một cách vô ích — board P4 vốn đã có sẵn 3.3 V sạch trên header J3.

### 2. Bus CAN của VESC (phần chính)

```text
  ESP32-P4                        TJA1051 (nên chọn T/3)      bus CAN
 ┌─────────────────┐            ┌───────────────┐
 │                 │            │               │
 │ GPIO 48 (TX) ───┼───────────▶│ TXD           │           ┌─────────────┐
 │                 │            │               │           │             │
 │ GPIO 47 (RX) ◀──┼─┬─[1.8kΩ]──┤ RXD      CANH ├─────┬─────┤ CANH        │
 │                 │ │          │               │  [120 Ω]  │    VESC     │
 │                 │[3.3kΩ]     │          CANL ├─────┴─────┤ CANL        │
 │                 │ │          │               │           │             │
 │            GND ─┼─┴──────────┤ GND           │           └─────────────┘
 │                 │            │               │
 │             5V ─┼───────────▶│ VCC           │
 └─────────────────┘            └───────────────┘
```

Chiều của chân được ghi theo góc nhìn của MCU (quy ước NXP): trên TJA1051,
**TXD là ngõ vào** (MCU điều khiển nó), **RXD là ngõ ra** (transceiver điều
khiển MCU).

- **Giá trị cầu phân áp trên RXD**: 1.8 kΩ (nối tiếp) + 3.3 kΩ (xuống GND) —
  hoặc 10 kΩ + 18 kΩ. TJA1051 chạy ở 5 V nên RXD của nó dao động tới 5 V —
  cầu phân áp kéo xuống mức 3.3 V an toàn cho P4. TXD thì không cần gì cả:
  3.3 V từ P4 đã đủ điều khiển.
- **Điện trở đầu cuối 120 Ω** giữa CANH ↔ CANL là **bắt buộc** — bus CAN không
  có điện trở đầu cuối sẽ chạy chập chờn hoặc không chạy. Đa số module TJA1051
  đã hàn sẵn (tìm điện trở ghi `121` giữa hai chân CANH và CANL, hoặc đo
  CANH↔CANL khi module chưa cấp nguồn: ~120 Ω = có, hở mạch = không có). Nếu
  module của bạn không có, **hãy hàn một điện trở 120 Ω giữa CANH ↔ CANL** như
  trong sơ đồ.
- **TJA1051 vs TJA1051T/3**: nếu được chọn, hãy lấy bản **T/3** — nó có chân
  `VIO` riêng để nối vào 3.3 V và bạn bỏ được hẳn cầu phân áp trên RXD.
- Tốc độ CAN mặc định là **500 kbps**, ID controller là **2** — cả hai chỉnh
  được trong `idf.py menuconfig` ở mục *VESC CAN* (`VESC_CAN_SPEED_KBPS`,
  `VESC_CAN_CONTROLLER_ID`).

#### Board hai motor (dual-motor / two-head)

ESC đôi (Flipsky Dual, Spintend UBOX, MakerX dual, …) thực chất là **hai node
VESC trên cùng một bus CAN**, mỗi node có ID riêng. Để dùng cả hai đầu:

1. Trong **Settings → Second head**, bật công tắc lên và đặt **Second head ID**
   bằng CAN ID của controller thứ hai (cái chính là *Target VESC ID* như thường lệ).
2. Trên **cả hai** đầu, trong VESC Tool bật **App → General → Send status over CAN**
   với status message **1–5** ở **50 Hz** (tốc độ mặc định khi bật), và đặt cho
   hai đầu **CAN ID khác nhau** (ví dụ 0 và 1).

Tốc độ status 50 Hz là bắt buộc chứ không phải tuỳ chọn: VESC master chỉ gộp
một node peer vào tổng chung khi status CAN của peer đó **mới hơn 100 ms**, và
dashboard cũng đọc nhiệt độ của đầu thứ hai từ chính những khung status đó.
Để tốc độ status thấp thì số Ah/Wh/công suất tổng sẽ nhấp nháy.

Trên board hai motor bạn sẽ có:

- **Nhiệt độ** hiển thị cả hai đầu dạng `h1/h2` (ví dụ `34/37`); tự quay về
  một giá trị nếu không thấy status của đầu thứ hai.
- **Pin / Ah / Wh / công suất** là **tổng gộp** của cả hai motor — chính VESC
  master tự cộng (ta chỉ đọc giá trị *setup* của nó), nên không phát sinh thêm
  lệnh poll CAN nào.
- **Tốc độ, quãng đường và điện áp** lấy từ đầu chính.
- **ESC NOT CONNECTED** hiện lên nếu *một trong hai* đầu im lặng.
- **Menu cấu hình VESC** có thêm bộ chọn **Head 1 / Head 2** để bạn đọc và ghi
  MCCONF/APPCONF của từng đầu độc lập.

### 3. D1 Mini ESP32 ↔ P4 (chỉ cho phần bonus AA)

Board P4 có header `J3` ở cạnh dưới với các chân mở rộng còn trống. Console
debug USB-C (GPIO 37/38) vẫn dùng được bình thường khi đã đấu phần này.

```text
  ESP32-P4 (header J3)             ESP32-WROOM-32 / D1 Mini
 ┌─────────────────────┐          ┌─────────────────────────┐
 │ GPIO 22 (UART1 TX) ─┼─────────▶│ GPIO 3 (RX0)            │
 │ GPIO 21 (UART1 RX) ◀┼──────────┤ GPIO 1 (TX0)            │
 │ GPIO 24 ────────────┼─────────▶│ EN     — reset chip     │
 │ GPIO 25 ────────────┼─────────▶│ GPIO 0 — chọn chế độ boot│
 │ 3V3 (ra) ───────────┼─────────▶│ 3V3    — cấp nguồn      │
 │ GND ────────────────┼──────────┤ GND                     │
 └─────────────────────┘          └─────────────────────────┘
```

`EN` / `GPIO 0` là cặp chân reset + chọn chế độ boot tiêu chuẩn của ESP32 —
đúng bộ chân mà `esp_serial_flasher` dùng trên mọi chip ESP32; trên dev board
D1 Mini thì chúng đã được đưa ra sẵn ở header `EN` và `D3` (= GPIO 0).

Hai đường RST/BOOT cho phép firmware chính trên P4 tự động nạp lại firmware BT
agent qua UART khi version của hai bên lệch nhau — bạn chỉ phải tự nạp cho D1
Mini đúng một lần duy nhất.

---

## 🚀 Build & nạp firmware

### Yêu cầu

- **ESP-IDF v5.5 trở lên** (đã test với v5.5.3).
- Cài cả hai target — `esp32p4` là bắt buộc, `esp32` chỉ cần nếu bạn muốn phần
  bonus Android Auto.

```bash
git clone --recursive https://github.com/espressif/esp-idf.git
cd esp-idf && ./install.sh esp32,esp32p4
. ./export.sh
```

### 1. Firmware chính — ESP32-P4 (dashboard + AA)

```bash
cd esp32p4-android-auto
idf.py set-target esp32p4
idf.py -p /dev/cu.usbmodem* flash monitor
```

Chạy `idf.py` trần là build cho board **Waveshare 4.3"** (mặc định).

> Firmware WiFi của ESP32-C6 (`network_adapter.bin`) được nhúng vào binary
> chính qua `EMBED_FILES` và được đẩy sang C6 qua SDIO lúc boot khi version
> không khớp — bạn không phải nạp riêng cho C6.

#### Nhiều board

Firmware hỗ trợ nhiều hơn một board head unit ESP32-P4. Chọn board bằng
`scripts/build_board.sh <board> <tham số idf.py…>`, script này dùng thư mục
build riêng cho từng board và chồng thêm overlay `sdkconfig.defaults.<board>`
tương ứng:

| Board | Slug | Flash | Ghi chú |
|---|---|---|---|
| Waveshare ESP32-P4-WIFI6-Touch-LCD-4.3 | `waveshare` | 32 MB | mặc định (cũng là cái `idf.py` trần build ra) |
| Guition JC4880P443C_I_W | `jc4880` | 16 MB | ST7701S, bảng partition nhỏ hơn (`partitions_16mb.csv`) |

```bash
scripts/build_board.sh                              # build firmware cho TẤT CẢ board
scripts/build_board.sh waveshare flash monitor
scripts/build_board.sh jc4880 -p /dev/cu.usbmodem* flash monitor
```

Chạy không kèm tên board (hoặc `all`) để build image của mọi board một lượt.

Những thứ khác nhau giữa các board (phần còn lại — WiFi/SDIO→C6, I2C cảm ứng,
SD, phần lớn bus I2S — đều dùng chung): timing panel MIPI-DSI + vendor init,
chân backlight/reset LCD, chân UART của BT agent, chân CAN RX/TX, dung lượng
flash và bảng partition. Board được chọn bằng Kconfig `BOARD_MODEL`
(`CONFIG_BOARD_WAVESHARE_43` / `CONFIG_BOARD_JC4880P443C`); xem các nhánh
`#if CONFIG_BOARD_JC4880P443C` trong BSP và `main/bt_link.h`.

Những chân bạn thực sự phải đấu dây (transceiver CAN và BT agent) nằm ở bảng
trong mục **🔌 Đấu dây** phía trên. Các chân panel nội bộ của JC4880 (backlight
LCD `23`, reset `5`) được set trong BSP — bạn không phải hàn.
Layout 16 MB vừa đủ cho hai slot OTA 5 MB + 1 MB storage + ~4.9 MB trip log —
image ứng dụng (~3.8 MB) còn dư ~1.2 MB (24%) trong mỗi slot, nên hãy để ý
kích thước khi firmware lớn dần.

### 2. (Tuỳ chọn) Firmware BT agent — D1 Mini ESP32

Chỉ cần khi bạn muốn phần bonus Android Auto. Có hai cách:

**A. Để P4 tự nạp (hợp nhất khi hàn WROOM trần).** Bật
`BT_AGENT_OTA_ENABLED=y` trong `idf.py menuconfig` (ở mục *Project → BT Agent
OTA*) rồi build lại. Mỗi lần boot, P4 kiểm tra dòng `BT-VER:` của agent qua
UART và nạp lại firmware từ blob nhúng trong `components/bt_agent_fw/` nếu
version không khớp. Nhờ vậy một con WROOM-32 hàn trần sẽ được nạp đầy đủ ngay
lần cấp nguồn đầu tiên. (Mặc định là `n` — không bật thì đường BT là no-op kể
cả khi đã đấu WROOM.)

**B. Tự nạp bằng tay.** Cắm một dev board có USB vào laptop:

```bash
cd tools/bt_agent
idf.py set-target esp32
idf.py -p /dev/cu.usbserial-* flash monitor
```

Xem [`tools/bt_agent/README.md`](tools/bt_agent/README.md) để có bản giải
thích đầy đủ log boot của agent và hộp thoại ghép đôi SSP trông ra sao.

### 3. Script LISP cho VESC — cruise control + profile tốc độ

[`lisp/main.lisp`](lisp/main.lisp) chạy trên **controller VESC** (không phải
trên P4). Nó thêm cruise control qua chân PPM `RX` và ba preset profile tốc độ
qua chân `TX`, đồng thời expose trạng thái cruise cho kênh LISP poll của
dashboard — đó chính là thứ điều khiển đèn báo cruise trên màn hình.

Mở *VESC Tool → VESC Packages → Lisp Scripting*, nạp `lisp/main.lisp`,
**Upload** → **Activate** → lưu vào flash. Xem [`lisp/README.md`](lisp/README.md)
để biết chi tiết và cách tuỳ chỉnh các preset tốc độ.

Không có script này thì dashboard vẫn hiển thị telemetry realtime bình thường
— chỉ mất đèn báo cruise và tiếng bíp khi đổi profile.

### 4. Cập nhật OTA sau lần nạp đầu tiên

Khi head unit đã lên SoftAP (IP mặc định `192.168.4.1`), đẩy firmware mới qua
HTTP từ bất kỳ laptop nào đang nối vào AP đó:

```bash
scripts/ota_push.sh 192.168.4.1
```

Bạn sẽ thấy thanh tiến trình trên màn hình thiết bị, sau đó nó reboot sang
slot mới.

---

## 📱 Cách sử dụng

### Chế độ dashboard (mặc định)

Cấp nguồn lên. Dashboard hiện ra ngay lập tức với mọi thứ VESC đang báo qua
CAN — pin, tốc độ, nhiệt độ, v.v. Bấm vào Settings để đổi đơn vị, tốc độ CAN,
ID controller, hoặc mở trình xem log.

Trong lúc dashboard đang chạy, **VESC Tool vẫn kết nối được qua Bluetooth LE**
(cầu NUS) y như với bất kỳ adapter VESC nào khác.

### Chuyển qua lại giữa hai chế độ

**Chạm ba ngón** (đặt ba ngón bất kỳ lên màn hình khoảng ~100 ms) để chuyển
giữa **dashboard VESC** và **màn chiếu Android Auto**. Cử chỉ này hoạt động
theo cả hai chiều, ở cả hai chế độ, và mỗi lần chạm chỉ kích hoạt một lần —
phải nhấc tay lên mới kích hoạt lại được, nên nó không bị lọt sang điện thoại
thành một cú chạm lạ.

### Bonus Android Auto (sau khi đã nạp firmware BT agent)

1. Màn hình sẽ hiện **"Waiting for phone"** ở một góc kèm SSID / IP của SoftAP.
2. Trên điện thoại: *Cài đặt → Bluetooth → Ghép thiết bị mới → **ESP32-P4 AA***.
3. Chấp nhận hộp thoại ghép đôi SSP.
4. Điện thoại tự vào WiFi của head unit và khởi động Android Auto. Màn hình
   lật sang chế độ chiếu AA; dashboard VESC vẫn chạy tiếp dưới dạng overlay
   (% pin, tốc độ) đè lên video AA.

Sau lần ghép đôi đầu tiên, head unit nhớ điện thoại, và mọi lần bật nguồn sau
đó đều tự kết nối lại mà không hỏi gì.

### App companion (thông báo, media, đồng hồ, nạp firmware qua WiFi)

App Android tuỳ chọn nằm trong [`flutter-application/`](flutter-application/).
Build và cài:

```bash
cd flutter-application
flutter build apk --release
adb install -r build/app/outputs/flutter-apk/app-release.apk
```

- **Ghép đôi**: mở app → nó quét tìm head unit qua BLE → chạm để kết nối. Sau
  đó tự kết nối lại.
- **Thông báo / media**: cấp quyền truy cập thông báo khi được hỏi; thông báo
  của điện thoại + thông tin bài đang phát sẽ hiện trên dashboard. Đồng hồ
  trên dashboard bắt đầu hiện `HH:MM` ngay khi app kết nối.
- **Cập nhật firmware qua WiFi**: *Home → Update head unit firmware*. App đọc
  thông tin đăng nhập SoftAP qua BLE (hoặc bạn tự quét / nhập), vào WiFi đó và
  tải firmware đóng gói sẵn trong APK lên. Head unit kiểm tra rồi reboot sang
  version mới — nhớ giữ nguồn trong suốt quá trình. Image đóng gói được lấy từ
  `build/` bởi [`scripts/stage_firmware_asset.sh`](scripts/stage_firmware_asset.sh).

### File trên thiết bị & GIF splash lúc boot

Có ba cách quản lý file trên head unit:

- **Trên thiết bị**: *Settings → Files* — trình duyệt file cảm ứng cho
  `/vescfs` (bộ nhớ trong) và `/sdcard` (microSD nếu có cắm). Duyệt, xem trước
  ảnh, đổi tên / di chuyển / xoá.
- **Từ điện thoại**: *Home → Device files* (hiện ra khi kết nối với firmware
  có hỗ trợ) — liệt kê thư mục, tải về máy, tải từ máy lên, đổi tên, xoá, tạo
  thư mục.
- **Từ trình duyệt web**: `http://android-auto.local/files` (hoặc IP gateway
  của SoftAP) — cũng duyệt / xem trước / tải lên / đổi tên / di chuyển / xoá
  trên cả hai ổ.

### Trình soạn thảo LISP trên web

`http://android-auto.local/lisp` — một editor LispBM đầy đủ cho script đang
chạy trên VESC, mở từ bất kỳ trình duyệt nào cùng mạng (điện thoại thì vốn đã
vào SoftAP của head unit rồi):

- tô màu cú pháp, số dòng, ngoặc nhiều màu + highlight ngoặc khớp, tìm/thay
  thế, thụt lề khối, tự đóng ngoặc;
- một linter cho đúng những lỗi tốn thời gian nhất ở đây: cân bằng ngoặc,
  chuỗi chưa đóng, cặp `@const-start` / `@const-end`, defun bị bỏ quên ngoài
  khối const, và thread được start trước khi hàm nó chạy được định nghĩa;
- thư viện script trên thiết bị (`/vescfs/lisp` và microSD) với mở, lưu, đổi
  tên / di chuyển, xoá, tạo thư mục và tải lên;
- **Read VESC** / **Upload** / **Upload + Run** / **Start** / **Stop** qua CAN
  kèm thanh tiến trình (việc truyền chạy bất đồng bộ nên phần còn lại của
  server vẫn phản hồi bình thường);
- console realtime hiển thị output `(print ...)` của script, cộng thêm một
  dòng REPL để chạy thử một biểu thức mà không phải nạp lại script;
- thống kê LISP (CPU / heap / bộ nhớ / stack và các biến được export), chỉ
  poll khi tab đó đang mở.

Tắt nó bằng `CONFIG_LISP_HTTP_ENABLED=n` (tốn khoảng ~15 KiB flash).
`scripts/lisp_web_mock.py` phục vụ đúng trang đó với một thiết bị giả lập, để
làm việc với editor mà không cần board thật.

**Đặt GIF khởi động**: tải một file GIF động lên `/vescfs` qua *Device files*
và đặt tên là **`splash.gif`** (tải lên với tên sẵn như vậy, hoặc đổi tên sau
khi tải). Lần boot kế tiếp nó sẽ phát full màn hình, sau đó dashboard tiếp
quản. Nhớ để dung lượng nhỏ — `/vescfs` khoảng ~4 MB trên board Waveshare,
~1 MB trên jc4880.

---

## 🗺️ Roadmap / Trạng thái

| Hạng mục | Trạng thái | Ghi chú |
|---|---|---|
| Dữ liệu realtime VESC qua CAN | ✅ | % pin, tốc độ, điện áp, dòng, nhiệt độ, odometer |
| Cửa sổ thống kê chuyến + log chuyến lưu lâu dài | ✅ | Số liệu / biểu đồ từng chuyến + Reset trip; log vòng thô trên partition `triplog` |
| Menu cấu hình VESC Tool trên thiết bị + FOC detection | ✅ | Đọc / ghi config controller ngay trên màn hình; bảng tham số sinh từ XML của VESC Tool |
| VESC LISP poll | ✅ | Đèn báo cruise + thống kê tuỳ chỉnh (cần [`lisp/main.lisp`](lisp/main.lisp) trên controller) |
| Cầu BLE NUS (VESC Tool qua BLE) | ✅ | Chạy song song được với AA |
| App companion — cầu thông báo / media / đồng hồ | ✅ | App Flutter `aa_bridge` qua BLE |
| App companion — cập nhật firmware qua WiFi / BLE | ✅ | Image đóng gói được POST tới endpoint OTA của SoftAP, hoặc stream qua đường BLE |
| Trình duyệt file trên thiết bị (`/vescfs` + microSD) | ✅ | Giao diện cảm ứng trong *Settings → Files*; xem trước ảnh, đổi tên / di chuyển / xoá |
| App companion — quản lý file qua BLE | ✅ | Duyệt / tải về / tải lên / đổi tên / xoá bộ nhớ head unit từ điện thoại |
| GIF splash lúc boot | ✅ | `/vescfs/splash.gif` động phát trong lúc boot (tải lên qua trình quản lý file của app) |
| Màn Settings + trình xem log trong PSRAM | ✅ | Log sống sót qua reset, xem được ngay trên thiết bị |
| OTA qua HTTP + tiến trình trên màn hình | ✅ | `scripts/ota_push.sh` |
| (Bonus) Video AA Wireless (H.264) | ✅ | Native 800×480 @ ~10–15 fps; nút thắt là decode phần mềm + chuyển đổi RGB |
| (Bonus) Forward cảm ứng | ✅ | GT911 → protobuf `TouchEvent` của AA |
| (Bonus) Kênh System audio | ✅ | Tiếng bíp giao diện; Gearhead bắt buộc có mới chịu chiếu |
| (Bonus) Tự kết nối lại điện thoại gần nhất | ✅ | Danh sách bonded nằm trong NVS của BT agent |
| Kênh audio Media / Speech | 🟡 | Cố ý bỏ — không có ngõ ra âm thanh |
| BT Classic thuần trên P4 (không cần D1 Mini) | ❌ | Không làm được — ESP32-P4 không có radio BT Classic |

---

## 🖨️ Vỏ hộp in 3D

File STL / STEP của vỏ nằm trong [`3d-model/`](3d-model/). Thông số in, vật
liệu khuyến nghị và ghi chú lắp ráp sẽ được bổ sung khi thiết kế ổn định hơn.

<!-- TODO: ảnh render hoặc ảnh chụp vỏ đã in -->

---

## 📁 Bố cục repo

```
.
├── main/                       # Firmware ESP32-P4 — VESC, UI, stack AA, OTA, BLE
├── components/
│   ├── esp32_p4_wifi6_touch_lcd_4_3/  # BSP Waveshare (LVGL, ST7701 DSI, cảm ứng GT911)
│   ├── vesc_can/               # Driver CAN cho VESC (dữ liệu realtime + LISP poll)
│   ├── vesc_ui/                # UI dashboard — wrapper mỏng; nguồn UI thật lấy từ Super_VESC_Display/{generated,custom}/
│   ├── vesc_config/            # Menu cấu hình VESC Tool trên thiết bị (bảng tham số từ XML, FOC detection)
│   ├── trip_log/               # Log chuyến vòng thô trên partition triplog riêng
│   ├── dev_settings/           # Màn Settings + tuỳ chọn được lưu lại
│   ├── log_capture/            # Logger ring buffer trong PSRAM
│   ├── bt_agent_fw/            # Blob firmware bt_agent để nhúng (bonus AA)
│   ├── c6_ota_partition/       # Firmware ESP32-C6 nhúng sẵn (network_adapter.bin)
│   └── qr_info/                # Mã QR chứa thông tin WiFi cho điện thoại
├── Super_VESC_Display/         # Project NXP GUI Guider — nguồn UI dashboard (generated/ + custom/), biên dịch vào firmware qua vesc_ui
├── tools/
│   ├── bt_agent/               # Firmware D1 Mini ESP32 (Classic BT + SPP)
│   ├── c6_slave_fw/            # Nguồn của firmware C6 đóng gói sẵn (gitignored, xem CLAUDE.md)
│   └── c6_ota_flasher/         # Bộ nạp C6 độc lập dùng khi dự phòng
├── scripts/                    # capture.sh (Wireshark), ota_push.sh, extract_yuv.py, release.sh
├── lisp/                       # Script LISP cho VESC (cruise + profile tốc độ) — chạy trên VESC, không phải P4
├── 3d-model/                   # File STL / STEP của vỏ in 3D
├── release/                    # Artifact release theo version (.bin của P4 + .apk companion)
├── docs/images/                # Ảnh chụp / screenshot dùng trong README này
├── research/                   # Nguồn tham khảo upstream (gitignored)
├── partitions.csv              # Layout dual-OTA (cả hai slot dưới mốc 16 MB) + partition triplog
├── CLAUDE.md                   # Ghi chú kiến trúc / lịch sử thiết kế
└── README.md
```

---

## 🙏 Ghi công & tham khảo

- **[Dự án VESC](https://vesc-project.com/)** của *Benjamin Vedder* — bộ điều
  khiển motor mã nguồn mở mà dashboard này xây quanh nó.
- [**aasdk**](https://github.com/f1xpl/aasdk) và [**openauto**](https://github.com/f1xpl/openauto) của *f1xpl* — tài liệu tham chiếu giao thức AA Wireless cho chế độ bonus.
- [**headunit-revived**](https://github.com/andreknieriem/headunit-revived) của *andreknieriem* — tham chiếu cho chế độ không dây.
- [**esp-h264**](https://github.com/espressif/esp-h264) và [**esp-hosted**](https://github.com/espressif/esp-hosted) của *Espressif*.
- [**Wiki Waveshare ESP32-P4-WIFI6-Touch-LCD-4.3**](https://github.com/waveshareteam/ESP32-P4-WIFI6-Touch-LCD-4.3) — BSP và code ví dụ của board.

---

## 📜 Giấy phép

Phát hành theo **GNU General Public License v3.0** — xem [`LICENSE`](LICENSE).
