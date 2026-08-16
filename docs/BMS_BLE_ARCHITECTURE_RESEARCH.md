# Nghien cuu kien truc BLE cho tab BMS

Ngay nghien cuu: 2026-08-16

Tai lieu nay hop nhat hai nhanh khao sat:

- hien trang BLE trong firmware ESP32-P4/ESP32-C6 cua repository;
- cach cac du an cong dong doc smart BMS JK, JBD, Daly va nhieu hang khac qua
  BLE.

Muc tieu la chot kien truc truoc khi viet tab BMS. Day la phan tich va de xuat,
chua thay doi runtime code.

## 1. Ket luan ngan

Kien truc phu hop voi repository la:

```text
Smart BMS
   |
   | BLE proprietary GATT
   v
ESP32-C6 controller/radio
   |
   | ESP-Hosted SDIO/VHCI (HCI, khong phai telemetry RPC)
   v
NimBLE host tren ESP32-P4
   |
   v
ble_central_manager
   |-- cadence client
   `-- BMS GATT session
           |
           v
       vendor driver (JK/JBD/Daly/...)
           |
           v
       normalized BMS snapshot
           |
           v
       BMS tab / simulator / diagnostics
```

Ba quyet dinh quan trong:

1. BMS BLE la mot **GATT central client tren P4**. C6 chi la controller/radio;
   BT-agent ngoai phuc vu Bluetooth Classic cho Android Auto va khong tham gia.
2. Phai tao `ble_central_manager` dung chung truoc. Khong copy nguyen
   `ble_cadence_client`, vi NimBLE chi co mot scan/initiate procedure toan cuc va
   cadence dang giu mot lenh connect vo han.
3. Tach transport/session BLE khoi parser theo hang, sau do quy tat ca driver
   ve mot BMS snapshot chung. MVP chi doc, khong gui lenh thay doi cau hinh,
   MOSFET, balance, reset hoac unlock BMS.

## 2. BLE hien tai trong repository

### 2.1 Vai tro cua tung chip

| Thanh phan | Vai tro BLE thuc te | Bang chung local |
|---|---|---|
| ESP32-P4 | Chay NimBLE host, GAP, GATT server va GATT client | `main/ble_host.c`, `main/ble_cadence_client.c` |
| ESP32-C6 tren board | Wi-Fi/BLE controller, truyen HCI qua ESP-Hosted SDIO/VHCI | `docs/ARCHITECTURE.md`, `main/c6_ota.c`, `sdkconfig.defaults` |
| BT-agent ngoai | Bluetooth Classic/SPP cho Android Auto Wireless qua UART1 | `main/bt_link.h`, `tools/bt_agent/main/main.c` |

Do do khong can va khong nen tao mot protocol telemetry rieng P4-C6 cho BMS.
Notification GATT da duoc NimBLE tren P4 nhan qua duong HCI hien co.

### 2.2 Dual-role dang co

P4 dong thoi lam:

- GATT peripheral/server cho NUS va NotifBridge;
- GATT central/client cho sensor cadence/PAS.

`CONFIG_BT_NIMBLE_MAX_CONNECTIONS=3` dang duoc chia nhu sau:

| Slot | Hien tai |
|---|---|
| 1 | Phone companion ket noi vao GATT server |
| 2 | VESC Tool ket noi vao NUS server |
| 3 | P4 central ket noi sensor cadence |
| 4 | Chua co; BMS se can slot nay |

`main/ble_host.c` co chu dich cho phep hai peripheral peer va chua slot thu ba
cho cadence. Vi vay them BMS ma chi them file client se gay het slot.

Khuyen nghi san pham la nang tong so connection len it nhat 4, nhung phai lam
dong bo va test:

- `CONFIG_BT_NIMBLE_MAX_CONNECTIONS` phia host P4;
- gioi han active connection trong firmware controller C6
  `network_adapter.bin`;
- NimBLE mbuf/MTU pool;
- 4 link dong thoi trong luc Wi-Fi Android Auto dang stream.

Phuong an MVP neu 4 link chua on dinh la giu tong 3 va ha peripheral peer toi da
tu 2 xuong 1. Doi lai, phone companion va VESC Tool khong the cung ket noi khi
cadence va BMS dang live. Khong nen am tham tao regression nay; can hien thi ro
policy va trang thai `no connection slot`.

### 2.3 Vi sao phai co central manager

Cadence hien goi `ble_gap_connect(..., BLE_HS_FOREVER, ...)`. Khi sensor ngu,
initiator co the bi giu vo han. Chinh module cadence cung phai cancel pending
connect truoc khi scan chon sensor.

Neu BMS client tu quan scan/connect rieng, cac case sau se xay ra:

- cadence dang doi sensor ngu lam BMS khong initiate duoc;
- scan BMS cancel connect cadence;
- scan cadence cancel connect BMS;
- hai module cung gap `BLE_HS_EBUSY` va tu retry thanh reconnect storm;
- UI khong biet loi la `khong tim thay BMS` hay `tai nguyen GAP dang bi chiem`.

`ble_central_manager` can la noi duy nhat so huu:

- scan selection va scan reconnect;
- hang doi initiate cho cadence/BMS;
- connect timeout co gioi han;
- connection-slot reservation;
- exponential backoff co jitter;
- dispatch GAP event ve dung client;
- host reset/sync lifecycle.

Mot scan co the dispatch advertisement cho ca cadence matcher va BMS driver
matcher. Selection scan co owner ro rang, het scan thi manager khoi phuc lich
reconnect cho tat ca target da bind.

## 3. Cach cong dong dang lam

### 3.1 BLE Battery Service chuan khong du cho smart BMS

Bluetooth SIG Battery Service 1.1 co Battery Level 0-100%, status, energy,
health, cycle count va mot so thong tin tong quat. Specification khong dinh
nghia mang dien ap tung cell cua smart BMS.

Vi vay nen ho tro BAS nhu fallback cho SOC/thong tin co ban neu thiet bi co
expose, nhung khong dung UUID `0x180F` de ket luan day la BMS tuong thich. Du lieu
cell, balance, MOSFET va alarm thuong nam trong custom GATT service cua hang.

### 3.2 JK BMS: GATT chi la ong van chuyen frame rieng

Tai lieu reverse-engineering cua `esphome-jk-bms` cho thay:

- service UUID `0xFFE0`;
- write va notify deu co UUID `0xFFE1`, nhung nam o handle khac nhau;
- sau subscribe, client gui command `0x96` va `0x97` de lay settings/device info;
- BMS sau do stream cell frame;
- response dai it nhat 300 byte, mot so model toi 320 byte, nen bi chia qua nhieu
  BLE notification;
- parser phai tim frame header, gioi han buffer, ghep fragment va kiem CRC.

Day la bang chung truc tiep rang khong du chi map UUID -> value. Driver phai co
session handshake va frame assembler.

### 3.3 JBD va Daly: ten hang khong dong nghia mot protocol

`esphome-jbd-bms` dong goi chung transport UART/BLE quanh giao thuc JBD va co
fixture/faker cho test. `esphome-daly-bms` tach ro cac dong H/K/M/S dung frame
Modbus bat dau `0xD2`, trong khi J/T/A/U/W/ND dung giao thuc khac bat dau `0xA5`
va khong duoc driver do ho tro.

He qua cho firmware nay:

- `vendor = DALY` chua du de chon parser;
- driver ID can gom family/protocol revision;
- cho phep user override driver khi auto-detect sai;
- GATT database/UUID va frame version can duoc coi la du lieu co the thay doi
  theo firmware/model.

### 3.4 BatMon, aiobmsble va BMS_BLE-HA: driver plugin + model chung

Ba du an da ho tro nhieu hang deu hoi tu o mot mau:

- BLE backend/connection tach khoi BMS driver;
- advertisement matcher de auto-detect, kem cau hinh thu cong theo address/type;
- moi driver tu biet write mode, init command, polling va parser;
- output duoc normalize thanh voltage, current, SOC, cells, temperatures,
  alarms va diagnostic;
- co fixture, fake device va fuzz test du lieu BLE;
- theo doi RSSI, link quality, stale/unavailable thay vi bien packet loi thanh 0.

Home Assistant integration mac dinh co gang giu permanent connection, vi
reconnect lien tuc lam mot so BMS kem on dinh. No cung ghi nhan nhieu BMS chi
chap nhan mot central: app hang tren dien thoai dang ket noi se lam head unit
khong vao duoc.

`aiobmsble` canh bao ro telemetry BLE khong duoc dung cho tac vu an toan. Du lieu
co the sai hoac mat do protocol reverse-engineered va nhieu BLE. Bao ve qua ap,
qua nhiet, qua dong van phai do BMS/ESC phan cung dam nhiem.

## 4. Kien truc de xuat cho repository

### 4.1 Tach acquisition khoi UI

Nen tao mot model chung ma ca BLE va CAN co the cap nhat:

```text
BLE provider -- vendor driver --+
                                +--> bms_snapshot --> BMS tab
VESC CAN BMS provider ----------+
Simulator/fake provider --------+
```

Dieu nay giu lai duong `COMM_BMS_GET_VALUES` da phan tich trong
`docs/BMS_TAB_TECHNICAL_ANALYSIS.md`, nhung khong tron gia tri tu nhieu source
ma khong dan nhan. Moi snapshot phai co `source`, `driver_id` va timestamp.

### 4.2 Lop va trach nhiem

| Lop | Trach nhiem |
|---|---|
| `ble_central_manager` | Chia se scan/initiator, slot, reconnect, dispatch GAP event |
| `ble_bms_client` | Bind peer, discover GATT, subscribe, auth/probe, request scheduling |
| `bms_driver` | Match model/revision, mo ta UUID, tao request, assemble/parse frame |
| `bms_model` | Snapshot chuan hoa, validity bitmap, age/stale, atomic publish |
| `bms_ui_updater` / BMS tab | Chi doc snapshot; khong goi NimBLE trong LVGL callback |

Driver interface o muc thiet ke:

```c
typedef struct {
    bool (*match_adv)(const bms_adv_t *adv, int *confidence);
    const bms_gatt_profile_t *(*gatt_profile)(void);
    int  (*on_session_ready)(bms_tx_t *tx);
    int  (*build_poll)(uint8_t *out, size_t cap);
    bms_parse_result_t (*feed_notify)(const uint8_t *data, size_t len,
                                      bms_snapshot_builder_t *out);
    uint32_t default_poll_ms;
    uint32_t minimum_poll_ms;
} bms_driver_t;
```

Khong nen hardcode handle. JK la vi du cung UUID nhung hai handle, nen phai
discover characteristic theo properties/thu tu va luu handle theo session.

### 4.3 Snapshot chuan hoa

Nen dung integer theo don vi co ty le de tranh mo ho scale va giu parsing don
gian:

- `pack_mv`, `pack_current_ma`, `soc_permille`, `soh_permille`;
- `remaining_mah`, `nominal_mah`, `cycle_count`;
- `cell_count`, `cell_mv[]`, `cell_min/max/delta_mv`, cell index;
- `temp_count`, `temp_deci_c[]`, MOS temperature;
- charge/discharge MOSF, balancing state/mask;
- alarm/fault raw code va normalized alarm flags;
- `valid_mask`, `sample_seq`, `rx_timestamp_us`, `age_ms`;
- `source`, `driver_id`, model/firmware ID, RSSI/link quality;
- counters: CRC error, length error, timeout, reconnect, dropped notification.

Gia tri khong co trong protocol phai la `invalid`, khong phai zero. Snapshot cu
duoc giu khi gap mot frame loi, nhung UI phai chuyen sang `STALE` theo age.

Quy uoc dau cua current phai duoc chot tai model boundary. De cung chieu voi
telemetry VESC tren dashboard, de xuat `current > 0` la dang xa pin va
`current < 0` la dang sac/regen; driver nao nguoc dau phai doi trong parser.

### 4.4 Xu ly notification

NimBLE host callback phai ngan:

1. kiem length toi thieu va copy mbuf vao fixed ring/queue;
2. khong parse mang cell lon, khong cap phat dong, khong goi LVGL;
3. worker BMS uu tien thap ghep fragment, check header/length/CRC;
4. frame hop le moi publish atomic snapshot;
5. frame qua lon, timeout hoac CRC sai bi drop va tang diagnostic counter.

Voi JK, buffer frame toi thieu can bao duoc 320 byte; nen co gioi han compile-time
va test fragment tai moi bien ATT packet. Pool cung phai chiu duoc burst BMS
song song NUS response va NotifBridge.

### 4.5 State machine

```text
DISABLED / UNBOUND
        |
        v
SCANNING -> SELECTED -> WAIT_SLOT -> CONNECTING
                                  -> DISCOVERING
                                  -> SUBSCRIBING / AUTH / PROBING
                                  -> LIVE -> STALE
                                      |       |
                                      `-> BACKOFF -> WAIT_SLOT
```

Moi transition co timeout va reason code. Connect khong dung
`BLE_HS_FOREVER`; manager dung attempt co gioi han de target khac van co co hoi.
Backoff de xuat 1, 2, 4, 8... giay, cap 30-60 giay va co jitter.

Host reset phai clear tat ca handle/session, danh dau snapshot stale va cho
`on_sync` khoi phuc. Hien tai `ble_host.c` moi log reset, nen day la mot phan
can bo sung khi implement.

### 4.6 Connection va polling policy

Mac dinh de xuat:

- chi bind mot BMS trong MVP;
- giu ket noi/subscription trong luc head unit bat de tranh reconnect lien tuc;
- khi tab dang hien: request toi da khoang 1 Hz neu driver/model cho phep;
- khi tab an: khong request, hoac 10-30 giay neu dashboard/alert can du lieu nen;
- notification-tu-stream nhu JK khong duoc ep thanh poll 1 Hz;
- UI timer 250-500 ms chi render snapshot, khong tao them BLE traffic;
- moi driver co `minimum_poll_ms`/`default_poll_ms`, khong ap mot cadence cho
  tat ca hang;
- chi mot command-response in flight, co timeout, khong overlap request.

Can co nut `Disconnect BMS` hoac session policy `persistent/on-demand`, vi nhieu
BMS chi cho mot central va nguoi dung co the can mo app hang tren dien thoai.

## 5. Discovery va pairing

Auto-detect nen cham diem theo nhieu tin hieu:

- service UUID advertised;
- ten/prefix thiet bi;
- manufacturer data;
- GATT profile sau khi ket noi;
- model/device-info frame.

Khong chi dua vao ten. BMS co the bi rename; cung mot ten hang co the la protocol
khac. Cau hinh NVS can luu peer identity/address type, driver ID va protocol
revision da xac nhan. Neu thiet bi dung random resolvable address thi can bonding
identity thay vi persist raw address.

Mot so firmware doi pairing/PIN hoac ma hoa. Repository hien chua co lifecycle
SMP/passkey cho BMS, nen UI can phan biet:

- khong tim thay;
- co thay nhung khong connect;
- can pairing/PIN;
- auth/encryption failed;
- connect thanh cong nhung protocol khong tuong thich.

Credential neu co khong duoc log; luu bang co che NVS bao mat phu hop. MVP co the
ho tro truoc cac model khong can pairing, nhung state machine khong duoc gom loi
auth vao `timeout` chung.

## 6. Case bat buoc phai xu ly

### Tai nguyen BLE va coexistence

- 3 slot hien tai da day; C6 blob va P4 host lech connection limit;
- cadence ngu dang giu initiator;
- scan BMS/cadence dien ra cung luc;
- phone companion va VESC Tool cung ket noi;
- Wi-Fi Android Auto stream cung radio 2.4 GHz voi BLE;
- mbuf pool het khi NUS, notification bridge va BMS cung burst.

### Thiet bi va giao thuc

- app hang dang chiem ket noi duy nhat cua BMS;
- BMS ngu/khong advertise khi dong gan 0 A;
- weak RSSI/EMI, disconnect giua mot fragmented frame;
- UUID giong nhau nhung handle/property khac;
- firmware BMS doi GATT layout hoac protocol revision;
- frame split, ghep hai frame, truncate, qua lon, CRC sai;
- endian/scale/sign current khac nhau;
- cell count hoac temp count thay doi/qua gioi han;
- write-with-response va write-without-response khac nhau;
- BMS can pairing, PIN, encryption hoac vendor handshake.

### Data va UI

- connected nhung chua co sample dau tien;
- chi co pack data, khong co cell data;
- sample stale nhung connection van con;
- cell delta cao, over/under voltage, qua nhiet, MOSFET off, alarm unknown;
- source la ESC estimate, VESC CAN BMS hay direct BLE phai duoc dan nhan;
- simulator: normal, charging, stale, no BMS, CRC storm, cell imbalance, hot;
- unload tab phai xoa LVGL timer/callback va ha poll activity.

### Nhieu pack

MVP nen chi mot BMS. Neu them nhieu BMS sau nay:

- manager co the round-robin thay vi mo tat ca link;
- khong tu cong dien ap/dong/SOC neu chua biet topology series/parallel;
- moi pack co snapshot va stale rieng;
- aggregate chi la lop rieng co config topology va quy tac fault ro rang.

## 7. File de xuat khi implement

```text
main/
  ble_central_manager.[ch]     # owner duy nhat cua scan/initiate
  ble_bms_client.[ch]          # GATT session + worker/queue
  ble_cadence_client.c         # refactor dung central manager
  ble_host.c                   # init/sync/reset, connection policy
  bms_ui_updater.[ch]          # snapshot -> LVGL

components/bms/
  include/bms/bms_model.h
  include/bms/bms_driver.h
  bms_model.c
  drivers/bms_jk.c
  drivers/bms_jbd.c
  drivers/bms_daly.c
  tests/fixtures/...

Super_VESC_Display/custom/
  bms_screen.[ch]              # code custom, khong de logic trong generated/
```

Neu MVP chi nham dung model BMS thuc te cua xe, chi implement mot driver dau
tien sau khi capture advertisement, GATT service/characteristic va raw frame.
Khong nen viet ca JK/JBD/Daly khi chua biet BMS cua xe.

## 8. Thu tu trien khai de xuat

1. Xac dinh hang/model/firmware BMS va capture BLE advertisement + GATT dump.
2. Viet fixture raw frame va parser host-test cho dung model do.
3. Tao `bms_model` va fake provider de lam tab/simulator khong phu thuoc hardware.
4. Tao `ble_central_manager`, refactor cadence sang manager, test khong regression.
5. Chot policy 4 connection; rebuild/xac minh C6 controller blob neu can.
6. Them BMS GATT session, queue/worker, reconnect/stale diagnostics.
7. Noi normalized snapshot vao tab BMS.
8. Stress tren xe: Android Auto Wi-Fi + phone companion + VESC Tool + cadence +
   BMS, bao gom sleep/wake va app hang tranh ket noi.

## 9. Tieu chi chap nhan MVP

- Cadence khong mat ket noi do scan/reconnect BMS.
- Khong block NimBLE host task va LVGL task.
- Frame loi khong tao telemetry zero/gia.
- UI phan biet `unbound`, `connecting`, `auth`, `live`, `stale`, `unsupported`.
- Cell count, length va buffer deu co bound check.
- Parser co fixture cho fragmentation, CRC, truncated va oversized input.
- Tat ca lenh BLE MVP nam trong read-only whitelist.
- Mat BLE khong tham gia dieu khien bao ve pin/xe.
- Connection policy van hoat dong khi Android Auto dang stream.

## 10. Nguon tham khao cong dong va chinh thuc

- Bluetooth SIG, Battery Service 1.1:
  <https://www.bluetooth.com/wp-content/uploads/Files/Specification/HTML/BAS_v1.1/out/en/index-en.html>
- Espressif, ESP32-C6 BLE multi-connection guide:
  <https://docs.espressif.com/projects/esp-idf/en/release-v5.5/esp32c6/api-guides/ble/ble-multiconnection-guide.html>
- JK BLE protocol design:
  <https://github.com/syssi/esphome-jk-bms/blob/main/docs/protocol-design-ble.md>
- JBD ESPHome component:
  <https://github.com/syssi/esphome-jbd-bms>
- Daly ESPHome component:
  <https://github.com/syssi/esphome-daly-bms>
- BatMon multi-vendor BMS monitor:
  <https://github.com/fl4p/batmon-ha>
- aiobmsble normalized async library:
  <https://github.com/patman15/aiobmsble>
- Home Assistant BMS BLE integration:
  <https://github.com/patman15/BMS_BLE-HA>

## 11. Thong tin con thieu de chot driver dau tien

Can lay tu xe/BMS thuc te:

- hang va model BMS;
- ten hien khi scan BLE;
- app hang dang dung;
- anh man hinh device info/firmware version;
- cell count va hoa hoc pin;
- GATT service/characteristic dump;
- raw notification/request capture neu co;
- BMS co doi PIN/pairing hay khong.

Hai thong so da biet, Motor Current Max 70 A va dien ap pin dang quan sat 40 V,
khong du de suy ra protocol BLE, cell count hoac driver BMS.
