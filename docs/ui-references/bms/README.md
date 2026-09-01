# BMS UI references

Thư mục này lưu ảnh và ghi chú tham chiếu cho giao diện đọc BMS. Đây chỉ là
nguồn tham khảo bố cục; code triển khai thật phải nằm trong
`Super_VESC_Display/custom/` để không bị GUI Guider ghi đè.

## Ảnh nguồn

Đặt ảnh gốc do người dùng cung cấp vào `source/` với tên thống nhất:

| File | Nội dung |
|---|---|
| `source/01-pack-overview.png` | Tổng quan SOC, điện áp/dòng pack, cell cao/thấp, nhiệt độ và trạng thái |
| `source/02-cell-resistance-detail.png` | Danh sách điện áp từng cell và điện trở dây cân bằng |

![Pack overview](source/01-pack-overview.png)

![Cell and balance-wire detail](source/02-cell-resistance-detail.png)

Ảnh đang được cung cấp trong cuộc hội thoại nhưng chưa có binary file trong
workspace. Không dùng ảnh tái tạo làm ảnh gốc; hãy chép PNG nguyên bản vào hai
đường dẫn trên khi file được đưa vào workspace.

## Thành phần cần giữ từ mẫu

### Màn hình tổng quan

- SOC là thông tin nổi bật nhất, hiển thị bằng vòng cung lớn.
- Điện áp pack và dòng pack nằm ngay trong/ở dưới SOC.
- Thẻ cell cao nhất, cell thấp nhất và delta cell dùng màu trạng thái.
- Nhiệt độ, công suất, trạng thái sạc/xả và thời gian dữ liệu cuối cùng nằm ở
  lớp thông tin thứ hai.
- Có chỉ báo kết nối BLE/BMS và trạng thái dữ liệu live/stale.

### Màn hình chi tiết

- Cell được trình bày dạng grid, mỗi ô gồm số cell và điện áp.
- Màu điện áp không được hardcode theo ảnh; màu phải lấy từ ngưỡng cấu hình và
  cell min/max thực tế.
- Số cột phải tự thay đổi theo chiều rộng màn hình và số cell.
- Điện trở dây cân bằng là khu vực riêng, không trộn với điện áp cell.
- Các trạng thái charger, balancer, heater, temperature và cycle data nằm ở
  phần summary trước danh sách cell.

## Điều chỉnh cho màn hình xe 800x480

Mẫu nguồn là giao diện điện thoại dọc. Trên dashboard 800x480 nên tách thành:

1. `Overview`: SOC, pack voltage/current/power, temperature, cell delta và
   alarm quan trọng.
2. `Cells`: grid cell có thể cuộn, đánh dấu min/max/balancing.
3. `Details`: capacity, cycle count, SOH, MOSFET, heater, balance-wire
   resistance và diagnostic BLE.

Không đưa toàn bộ thông tin của màn hình điện thoại lên một trang landscape;
chữ sẽ quá nhỏ khi xe đang di chuyển.

## Quy ước trạng thái đề xuất

| Trạng thái | Cách hiển thị |
|---|---|
| Bình thường | Xanh lá/trắng theo theme |
| Cảnh báo | Vàng/cam và có nhãn nguyên nhân |
| Lỗi/nguy hiểm | Đỏ, ưu tiên alarm hơn dữ liệu phụ |
| Không có trường dữ liệu | `--`, không hiển thị `0` giả |
| Dữ liệu stale | Làm mờ và hiện tuổi dữ liệu |
| Mất kết nối | Giữ sample cuối nhưng ghi rõ `Disconnected` |

## Tài liệu liên quan

- `docs/BMS_TAB_TECHNICAL_ANALYSIS.md`
- `docs/BMS_BLE_ARCHITECTURE_RESEARCH.md`

## User-facing current sign

The BMS gauge presents power flow from the pack rider's point of view:

- charge or regenerative current entering the pack is positive (`+A`, `+W`);
- discharge current leaving the pack for the load is negative (`-A`, `-W`);
- zero is shown without a sign (`0.00A`).

The canonical backend/display snapshot currently follows the older VESC-style
sign (`+` discharge, `-` charge). `custom/bms_view.c` performs the inversion at
one explicit FE boundary; parser/model code must not add a second inversion.

## Large cell and wire section

`CELLS` and `WIRE` live in a standalone full-width section below the pack
overview. Each data card is `238 x 52` pixels (twice the former `119 x 26`)
with a 24 px value font. The section uses three columns and expands only to the
row count required by the connected pack; the outer BMS page provides vertical
scrolling through all 32 supported cells.
