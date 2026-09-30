# Plan doi Ride Modes sang gioi han dong tuyet doi

Status: **da trien khai format 2; cap nhat dong reverse ngay 2026-09-30**.

Cap nhat 2026-08-30 theo yeu cau nut cung:

- bo toan bo chuc nang cruise control;
- giu nut `TX` de doi Mode 1 -> 2 -> 3 -> 1;
- doi nut `RX` cu cua cruise thanh nut lui kieu **nhan-giu**;
- khong dung them `pin-ppm`, khong can them nut cung thu ba.

## 1. Cach hieu yeu cau

Moi mode chi luu gioi han **Motor Current (dong pha motor)** theo ampere, khong
con luu toc do va khong thay doi `max-speed`:

| Mode | Gia tri nguoi dung dat | Motor Current Max cua ESC | Gioi han thuc te |
|---|---:|---:|---:|
| 1 | 50 A | 70 A | 50 A |
| 2 | 70 A | 70 A | 70 A |
| 3 | 100 A | 70 A | 70 A |

Cong thuc bat buoc:

```text
effective_current_A = min(requested_mode_current_A, ESC l-current-max)
```

- Van luu `100 A` cho Mode 3; neu sau nay ESC duoc cau hinh hop le len 120 A,
  Mode 3 se cho toi 100 A.
- Tuyet doi **khong tu dong nang** `Motor Current Max` cua ESC khi mode dat cao
  hon. ESC van la gioi han tong/master.
- Khong dung `Battery Current Max` cho mode; hai dai luong nay khac nhau.
- Ba mode doc lap, vi du `90 / 40 / 70 A` van hop le. Khong co quy tac tang dan.
- Toc do do Motor Settings/VESC Tool quan ly; ride mode khong con goi
  `conf-set 'max-speed`.
- De xuat gia tri mac dinh moi la `50 / 70 / 100 A`. Can nguoi dung xac nhan
  day la default hay chi la vi du.

Mapping nut sau khi doi:

| Nut vat ly | GPIO Lisp | Hanh vi moi |
|---|---|---|
| Mode | `pin-tx` | Moi lan nhan (canh xuong, da debounce) doi mot mode |
| Reverse hold | `pin-rx` | Giu lien tuc de cho phep lui; tha ra thi cat/ramp dong lui ve 0 |

Nut RX chi la dieu kien cho phep chieu lui, **khong tu tao torque**. Nguoi lai
van phai hoan tat interlock an toan va dung tay ga de dieu khien dong lui.

## 2. Data model va protocol v2

Dung fixed-point `dA = A x 10` de 50 A duoc truyen/luy thanh `500`:

```text
mode_current_dA[3]       uint16, gia tri requested
esc_current_max_dA       uint16, read-only snapshot tu Lisp
reverse_enabled          bool
reverse_speed_dkmh       uint16
reverse_current_dA       uint16
config_revision          uint16
persist_pending          bool
```

Status active can them:

```text
current_profile
requested_current_dA
effective_current_dA
esc_current_max_dA
direction_state / reverse_button / reverse_armed / fault_reason
```

Ke hoach wire:

- tang `VESC_RIDE_CONFIG_FORMAT_VERSION` tu 1 len 2;
- giu message ID `0x07/0x08/0x09/0x0A/0x87/0x89`, thay payload va length;
- bo ba field speed va `current_permille`, thay bang ba `mode_current_dA`;
- response luon mang `esc_current_max_dA` de FE hien requested/effective;
- giu result code `ORDER_INVALID=4` o dang reserved de khong doi so cac result
  phia sau, nhung v2 khong bao gio tra result nay;
- parser v1 khong duoc doc payload nhu v2.

Range nhap cua ca Mode 1/2/3 va Reverse la `1 .. 999 A`, step `1 A`; gia tri
thuc te van clamp theo ESC. Reverse khong co tran policy 14 A rieng.

## 3. EEPROM va khoi dong

- Tang EEPROM magic/version tu `RM v1` sang `RM v2` vi semantics da doi hoan
  toan; khong duoc dien giai `%/permille` cu thanh ampere.
- Layout v2 chi luu ba current requested va ba field reverse.
- De xuat bo qua block v1 va dung full default v2, thay vi migration am tham.
- Khong luu active mode vao EEPROM.
- Luc moi khoi dong: gan ro `current-profile = 0`, load config, roi apply Mode 1.
- P4 chi dung `0x0A REQ_STATUS` de doc; khong re-select cache cu khi boot.

Nhu vay moi lan khoi dong luon ve Mode 1, ke ca lan truoc xe tat o Mode 3.

## 4. BE Lisp: ap dung gioi han A

`apply-profile` moi khong cham vao speed. No tinh:

```text
requested = mode_current_dA[current_profile] / 10
esc_max   = max(0, conf-get 'l-current-max)
effective = min(requested, esc_max)
scale     = effective / esc_max (0 neu esc_max <= 0)
conf-set 'l-current-max-scale scale
```

Can co `sync-current-scale` trong motor loop:

- doc lai `l-current-max` live;
- chi `conf-set` khi desired scale thay doi;
- neu VESC Tool doi 70 -> 60 A, Mode 3/100 A lap tuc thanh 60 A;
- neu doi 70 -> 120 A, Mode 3 lap tuc thanh 100 A;
- scale duoc giu trong motor config runtime, nen throttle, PAS va native ADC
  fallback deu khong vuot effective cap;
- firmware van giu thermal, absolute-current va hardware protection ben duoi.

SET config van chi chap nhan khi xe dung, ga nha va khong o reverse. Khi thanh
cong phai clear `rm-fault = 0`.

Quick panel doi label speed cu thanh label on dinh `Mode 1/2/3` hoac dong A
dong, tranh tiep tuc hien `Slow 5 km/h`.

### Bo cruise trong Lisp va motor arbiter

Xoa khoi `lisp/main.lisp`:

- state/gain `cruise-active`, `cruise-rpm`, `cruise-i`, `cruise-kp`,
  `cruise-ki`, `rpm-per-ms`, `rx-button-state`;
- cac ham activate/deactivate/increase/decrease, `cruise-out` va thread
  `update-rpm-per-ms`;
- nhanh cruise trong `motor-control-loop` va moi loi goi huy cruise;
- `monitor-rx-button` cu. RX se do `monitor-reverse` doc lien tuc.

Thu tu arbiter sau khi rut gon:

```text
master off > brake > reverse/interlock > forward throttle > PAS > coast
```

`pin-tx` luon la nut Mode va can debounce stable-count 40 ms truoc khi bat canh
nhan; sampling 50 ms + edge detection hien tai chua du chong bounce. Guard
`rv-dir == 1` phai nam trong mot ham select chung, khong chi trong monitor TX,
vi mode con doi tu quick panel (`10/11/12`), ride SELECT `0x09` va BLE helper
`cmd=2`. Nhu vay moi nguon doi mode deu bi bo qua khi reverse/interlock.

## 5. FE

- Xoa dong `Speed limit` khoi ca ba tab.
- Doi `Motor current %` thanh `Requested current`, don vi A.
- Moi tab hien them:
  - `ESC Motor Max: 70.0 A`;
  - `Effective: 50.0/70.0 A`;
  - canh bao mau vang neu requested > ESC max.
- Header active hien vi du `M3: set 100 A / effective 70 A`.
- Xoa local ordering validation va thong bao `Mode speeds must increase`.
- Save van la mot transaction nguyen khoi, Reload van lay source-of-truth tu
  VESC.
- Simulator fixture: ESC max 70 A, mode `50/70/100`, de test truc tiep clamp.

### Don dep cruise tren man hinh va protocol noi bo

- dashboard, cac theme va Android Auto overlay khong hien icon/label/toc do
  cruise;
- xoa simulator cruise animation va cac callback
  `update_cruise_control_status`/`update_cruise_speed` khi khong con consumer;
- `vesc_ui_updater` doc active mode tu ride status `0x89`, khong doc profile tu
  DASH packet cu;
- sau khi migrate consumer mode, xoa han poll/parse/cache/API
  `VLP_MSG_REQ_DASH 0x04/0x84` trong `vesc_lisp_panel.[ch]` va loop lien quan
  trong `vesc_rt_data.c`; khong giu packet profile-only/stub cruise;
- xoa cruise khoi source-of-truth/generator
  `Super_VESC_Display.guiguider`, `tools/build_classic_max.py`,
  `build_cockpit_guiguider.py`, `build_amber_dashboard.py` va
  `build_concept_dashboards.py`, sau do regenerate thay vi hand-edit generated;
- Classic/Classic Max khong con icon/target speed; Lamborghini/Supermoto khong
  con LED/label `CRZ`; chi xoa asset `_cruise_control_alpha_38x38.c` sau khi
  khong con reference;
- metadata cau hinh VESC goc co chu "Enable Cruise Control" khong thuoc nut
  cruise cua san pham nay, khong sua bang config sinh tu firmware neu khong co
  yeu cau rieng.

## 6. Ra soat mode lui hien tai

### Dau vao nut cung moi

`pin-rx` hien dang la nut cruise active-low va da duoc cau hinh
`pin-mode-in-pu`. Trien khai moi tai su dung dung day/nut nay:

```text
pin-tx -- nut MODE, nhan-ngan
pin-rx -- nut REVERSE, phai giu trong suot thoi gian lui
```

- nut van la dry-contact keo GPIO ve `GND` cua ESC, khong cap dien ap ngoai vao
  GPIO;
- bo hoan toan `pin-ppm` va thread reverse rieng tren PPM trong ban cu;
- bo top-level `gpio-configure pin-rx`; configure/doc RX trong
  `monitor-reverse` duoc `spawn-trap` bao ve de loi GPIO khong lam chet toan bo
  script. `pin-tx` van configure rieng cho nut Mode;
- RX debounce 40 ms, active-low;
- luc boot phai thay RX o trang thai released on dinh va thay tay ga released
  truoc khi cho phep arm;
- neu RX bi ho/open-circuit thi khong the yeu cau lui;
- neu RX bi chap GND tu luc boot thi reverse fail-closed cho den khi nut da
  released on dinh it nhat mot lan;
- neu RX duoc nhan khi xe con chay tien, request do bi latch reject; phai tha
  RX on dinh, xe dung + ga nha, roi nhan lai moi duoc bat dau arming;
- neu doc/configure RX loi thi dat fault `UNSUPPORTED_HARDWARE` va reverse
  disable, nhung mode tien van hoat dong.

### Phan dang dung va giu lai

- reverse default OFF va fail-closed neu RX khong khoi tao/doc duoc;
- nut active-low, debounce 40 ms;
- boot khi nut R bi giu khong duoc arm cho den khi da thay nut released;
- nhan R khi dang chay tien chi vao interlock, khong phat dong am;
- phanh uu tien cao hon reverse output;
- release R ramp dong lui ve 0, chi tra forward khi xe dung va ga nha;
- vao reverse huy PAS;
- reverse current da la A tuyet doi va clamp theo configured reverse current,
  `abs(l-current-min)` va `l-current-max`.
- reverse requested current dung cung range `1 .. 999 A` nhu Mode 1/2/3; thay
  doi nay khong tu nang Motor Current Min/Max tren ESC.

### Hai lo hong logic can sua

1. Hien tai brake-hold co the arm trong khi ga van dang bi giu. Khi nha phanh,
   reverse co the nhan torque ngay.
2. Khi dang lui ma bop phanh, `rv-armed` khong bi clear; nha phanh trong khi ga
   van giu co the lam reverse chay lai.

State machine de xuat:

```text
BOOT_LOCKED
  -> FORWARD chi sau khi da thay R released va throttle released

FORWARD + nhan R khi xe dang dung
  -> INTERLOCK
  -> ARMING khi |speed| <= 0.3 km/h, throttle <= 5%, brake duoc giu >= 200 ms
  -> READY sau khi nha brake, throttle van <= 5%
  -> REVERSE khi nguoi dung moi tang throttle va van giu R

FORWARD + nhan R khi xe con chay
  -> REQUEST_REJECTED/INTERLOCK, khong duoc arm
  -> FORWARD chi sau khi tha R, xe dung va throttle released
  -> muon lui phai nhan R lai tu dau

REVERSE + brake
  -> huy armed, ramp torque ve 0
  -> phai nha throttle va thuc hien lai brake-hold moi duoc lui tiep

REVERSE/INTERLOCK + nha R
  -> EXIT_INTERLOCK
  -> FORWARD chi khi xe dung va throttle released
```

Quy tac bat buoc bo sung:

- chi dem 200 ms brake-hold khi tay ga dang released; ga > 5% thi xoa bo dem va
  `reverse_armed`;
- bop phanh trong luc dang lui phai huy `reverse_armed`, ramp dong am ve 0 va
  bat buoc nha ga + thuc hien lai brake-hold;
- RX phai con pressed o moi tick phat dong am. Tha RX khong duoc cho phep mot
  tick torque lui nao sau debounce;
- giu RX khi xe con chay tien chi cat torque tien/vao interlock, tuyet doi khong
  doi dau dong, va bat buoc tha/nhan lai sau khi xe dung;
- moi nguon select mode (TX, quick panel, ride `0x09`, BLE helper) trong
  reverse/interlock deu bi bo qua; khi ve FORWARD moi cho doi mode;
- khi `reverse_enabled=false`, RX khong anh huong den torque tien.

Khi disable reverse, de xuat restore `min-speed` runtime da capture luc boot.
Sau khi upload v2 lan dau phai reboot VESC de loai runtime `max-speed` do script
v1 tung dat.

## 7. Test bat buoc

### Host/C

- parser v2 valid, short packet, bad version, stale seq, unknown message;
- chap nhan mode khong theo thu tu: `90/40/70 A`;
- chap nhan requested 100 A khi response bao ESC max 70 A;
- parse requested/effective/ESC max trong status;
- v1 khong duoc bi doc nham thanh v2.
- xoa/cap nhat assertion cruise cu trong `lisp_lint_test.dart` va
  `lisp_patch_test.dart`; them check RX chi do reverse monitor so huu;
- grep gate khong con cruise runtime/DASH API trong Lisp, P4 updater, overlay va
  dashboard theme (ngoai metadata VESC goc da ghi ro la out-of-scope).

### Simulator

- edit/save/reload `50/70/100 A`;
- Mode 3 hien requested 100 A, effective 70 A;
- thu cac thu tu bat ky;
- restart simulator ve Mode 1;
- tat/bat reverse va test moi state/canh bao UI.
- dashboard va AA overlay khong con icon/label/toc do cruise.

### Bench VESC

- boot luon Mode 1;
- ESC max 70 A: M1 <= 50, M2 <= 70, M3 <= 70 A;
- doi ESC max live 70 -> 60 -> 120 A, effective cap cap nhat dung;
- throttle va PAS deu bi clamp;
- boot giu R, giu ga truoc khi arm, R khi dang chay tien, brake khi dang lui,
  release R khi xe con lui: tat ca phai khong tao torque ngoai y muon;
- giu RX + dung ga moi tao dong am; tha RX thi dong am ramp ve 0;
- TX duoc debounce khong nhay hai mode; TX/quick panel/ride SELECT/BLE helper
  trong luc RX dang giu/reverse/interlock khong doi mode;
- nut RX bi ho, bi chap GND tu boot, hoac gpio read loi: fail closed;
- nhan/giu RX khi dang chay tien, dung xe van giu RX: khong arm; chi arm sau
  khi tha va nhan lai;
- xac nhan khong con nhanh PI/current output nao cua cruise trong motor arbiter.

## 8. Phan cong de nghi

- **Claude/BE:** protocol v2, parser/tests, Lisp model + EEPROM + dynamic clamp,
  xoa cruise, map `pin-rx` thanh reverse-hold, state machine reverse va bench
  checklist; dong bo mirror `lisp/main.ru.lisp` neu mirror nay van duoc duy tri.
- **Codex/FE:** model header phan FE, Ride Modes screen, simulator fixture,
  requested/effective display, xoa moi hien thi cruise va UI tests.
- Ca hai cap nhat `COLLABORATION_LOG.md`; khong sua de file cua nhau khi dang
  co thay doi chua commit.

## 9. Ba diem can duyet truoc khi trien khai

1. `50/70/100 A` la **default moi** hay chi la vi du?
2. Duyet range nhap de xuat `1..200 A` hay muon ceiling khac?
3. Duyet hanh vi an toan: bop phanh khi dang lui se huy arm va bat buoc thao tac
   brake-hold lai truoc khi lui tiep.

Mapping nut khong con la diem mo: `TX = Mode`, `RX = Reverse hold`, cruise bi
loai bo. Sau khi ba diem tren duoc duyet moi sua source va build.
