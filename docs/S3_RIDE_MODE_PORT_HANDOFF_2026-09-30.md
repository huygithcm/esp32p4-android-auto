# Handoff ride mode P4 -> ESP32-S3 (2026-09-30)

## Muc dich

Nhanh tham chieu da kiem tra la `fix/gear-circle-park-reverse`. Day la nhanh
P4/JC4880, dung de doc contract va tach tung thay doi can thiet khi xay lai tren
S3. Khong merge nguyen nhanh nay vao S3 vi BSP, display, touch, CAN pin va build
flow cua P4 khac S3.

Checkpoint hardware nguoi dung da xac nhan:

- VESC firmware 6.05;
- Lisp `diag3` da vao duoc Mode R va thoat trang thai phanh sau khi nha phanh;
- Mode 1/2/3, PARK va nut Mode da hoat dong tren bo P4 hien tai;
- cac thay doi ngay 2026-09-30 mo range dong R va tang kich thuoc badge mode;
  can test lai tren hardware sau khi nap goi moi.

## Source of truth dung chung

Lisp chay tren VESC, khong chay tren ESP32-P4/S3. S3 phai giao tiep dung voi
Lisp nay, khong viet lai state machine motor tren display.

| Chuc nang | Source tham chieu |
|---|---|
| PARK, Mode, R, ADC, watchdog, motor arbiter | `lisp/main.lisp` |
| Format 2, range va model snapshot | `components/vesc_can/include/vesc_can/vesc_ride_mode.h` |
| Parse va validate packet | `components/vesc_can/vesc_ride_mode_parse.c` |
| Owner cua request/poll/sequence | `components/vesc_can/vesc_ride_mode.c` |
| Map state thanh `P/1/2/3/R/-` | `main/ride_gear_state.h` |
| Freshness gate truoc khi day len UI | `main/vesc_ui_updater.c` |

Contract hien tai:

- `VESC_RIDE_CONFIG_FORMAT_VERSION = 2`;
- Mode 1/2/3 luu `mode_current_dA`, range `10..9990` dA (`1..999 A`);
- Reverse luu `reverse_current_dA` cung range `10..9990` dA;
- reverse speed van `10..50` dkm/h (`1..5 km/h`);
- requested current co the cao hon gioi han ESC va van duoc luu;
- dong R thuc te tren Lisp luon la:

```text
min(reverse requested A,
    abs(ESC l-current-min),
    ESC l-current-max)
```

Display khong duoc tu sua `l-current-min`, `l-current-max`,
`l-current-max-scale` de ep dat dong requested. ESC va firmware motor van la
lop gioi han cuoi.

## State hien thi mode

S3 chi hien thi snapshot da qua freshness gate:

```text
PARK             -> P
FORWARD profile0 -> 1
FORWARD profile1 -> 2
FORWARD profile2 -> 3
REVERSE_READY    -> R
REVERSE_ACTIVE   -> R
INTERLOCK        -> -
FAULT/stale      -> -
```

Khong suy ra `R` tu mau den ESC, ADC phanh hoac mot bien UI local. Nguon phai la
safety/status cua Lisp. Khi packet stale, hien `-` va khong giu ky tu cu.

## Luong vao R phai giu nguyen

1. Boot phai thay nut RX released it nhat mot lan.
2. Xe dung, ga released, reverse enabled.
3. Giu nut R active-low tren RX.
4. Giu phanh tren nguong 5% it nhat 20 tick motor.
5. Van giu R, nha phanh.
6. Cho `brk-rel <= 0.001`; Lisp phat `set-current 0` de thoat
   `CONTROL_MODE_CURRENT_BRAKE` cua VESC 6.05.
7. Luc do state thanh `REVERSE_ACTIVE`; tang ga moi tao dong am.

Thu tu arbiter khong duoc doi:

```text
input fault > brake > PARK/master off > reverse/interlock
            > forward throttle > PAS > coast
```

Khong doi cuc tinh RX, khong bo brake hold, khong cho phep R kieu toggle va
khong cho phep packet stale tiep tuc hien/ra torque.

## UI S3 can port va phan P4 khong duoc chep nguyen

Logic doc lap board co the port:

- chuoi va mau mode trong `Super_VESC_Display/custom/dashboard_theme.h`;
- `dashboard_gear_circle_style()` trong `Super_VESC_Display/custom/custom.c`;
- update/hide/re-show mode trong `custom.c` va `theme_generic.c`;
- Settings Reverse current dung range `1..999 A`, step `1 A`, hien so nguyen
  giong ba mode tien trong `ride_mode_screen.c`;
- gia tri R cu theo buoc `0.5 A` phai hien dung (vi du `7.5 A`); lan bam `+`
  tiep theo snap len `8 A`, bam `-` snap xuong `7 A`;
- test `P/1/2/3/R/-`, malformed state, stale state va hide/re-show.

Chi tiet P4/JC4880 phai thiet ke lai theo man S3:

- toa do P4 trong khung `800x480`: Cockpit/Classic tam `(220,280)`,
  Lamborghini tam `(183,354)`, Supermoto tam `(718,399)` va an mode card/
  caption cu de tranh chong lap;
- badge 80 px va `lv_font_montserrat_48`;
- layout cua Lamborghini/Supermoto;
- DSI/PPA, BSP, GT911, backlight va LVGL lock;
- CAN JC4880 TX51/RX52.

Neu man S3 cung 800x480, co the bat dau tu badge 80 px/font 48 px nhung phai
kiem tra overlap tren theme S3. Neu do phan giai khac, tinh ti le theo chieu cao
man va test bounds; khong hard-code toa do P4.

## Tinh trang hai nhanh S3 hien co

- `port/esp32s3-vesc-bms` la source seed doc lap tai checkpoint cu, da co
  format 2 nhung van con tran R 14 A va chua phai firmware S3 build/flash duoc.
- `esp32s3-port` la nhanh cu co format 1 (speed/current permille), khong tuong
  thich truc tiep voi Lisp format 2 hien tai.

Vi vay nen tao clone/worktree S3 rieng tu `port/esp32s3-vesc-bms`, sau do port
co chon loc contract va test tu nhanh tham chieu. Khong merge hai lich su.

## Thu tu trien khai S3

1. Chot board, LCD/touch, do phan giai, flash/PSRAM, TWAI pin va transceiver.
2. Hoan thien entry point, BSP, display/touch, LVGL lock, NVS va build S3.
3. Port `vesc_can` format 2, goi `vesc_ride_mode_set_target()` moi khi doi CAN
   target, va giu mot task duy nhat lam owner request/poll.
4. Port freshness gate va gear mapping truoc khi noi Settings/gesture.
5. Port Settings Reverse current `1..999 A`, step `1 A`.
6. Bo tri badge mode theo layout S3 va them self-test bounds/overlap.
7. Chay host tests, sau do bench voi banh chu dong nhac khoi dat.

## Test toi thieu truoc hardware

- C parser: `9990 dA` hop le, `9991 dA` bi reject.
- LispBM VESC 6.05: `rm-values-ok` co cung ket qua va requested 999 A clamp
  xuong gioi han ESC fixture.
- PARK/Mode/R: Mode 2 -> giu R + phanh -> nha phanh -> R; bop phanh lai phai
  uu tien phanh va nha phanh phai thoat brake mode.
- UI: tat ca state `P/1/2/3/R/-`, packet stale/malformed, hide/re-show.
- UI Settings: gia tri migrate `75 dA` hien `7.5 A`, `+` thanh `8 A`, `-`
  thanh `7 A`.
- CAN: sequence, timeout, wrong target, target thay doi va packet ngan.

Host test khong chung minh motor, PWM, LED, CAN transceiver, touch hay timing
FreeRTOS tren hardware. Bench lan dau phai giu mac dinh R 7 A, banh nhac khoi
dat va xac nhan chieu quay truoc khi tang requested current.
