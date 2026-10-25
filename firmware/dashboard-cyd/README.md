# dashboard-cyd: dashboard Air Blade 125 trên màn CYD 2.8"

Đồng hồ hiển thị cho xe Air Blade 125, chạy trên **Cheap Yellow Display** (ESP32-2432S028R, 320×240). Dashboard nhận dữ liệu không dây qua **ESP-NOW** từ bộ đọc K-Line ([`kline-reader`](../kline-reader)).

![Dashboard](../../docs/images/dashboard_cyd.png)

## Hiển thị

- **Vòng đồng hồ RPM:** 0–10.000 vòng/phút, chia 40 vạch. Màu đổi từ xanh dương sang xanh ngọc, vàng trên 6500, đỏ trên 8000.
- **Tốc độ** (km/h) chữ 7 đoạn ở giữa vòng, số RPM ngay bên dưới.
- **Ba ô bên phải:**
  - ENGINE: nhiệt độ máy. Vàng khi trên 98°C, đỏ khi trên 105°C.
  - BATTERY: điện áp ắc quy. Đỏ khi trên 15V hoặc dưới 11.8V. Vàng khi nổ máy mà dưới 13V.
  - THROTTLE: độ mở bướm ga, kèm thanh vạch.
- **Nhãn trạng thái** ở góc trên:
  - **DEMO** (vàng): không nhận được gói nào trong 3 giây. Dashboard chạy một vòng mô phỏng 20 giây.
  - **LIVE** (xanh lá): đang nhận dữ liệu thật từ xe.
  - **ECU** (đỏ): vẫn nhận được gói, nhưng ESP32 đọc K-Line báo lỗi phía xe. Giữa vòng đồng hồ hiện lý do: `NO K-LINE`, `ECU NO REPLY`, `RECONNECTING`, `CONNECTING`.

## Nạp firmware

```bash
./flash.sh          # build + nạp
./flash.sh -m       # nạp xong mở serial monitor
./flash.sh -b       # chỉ build
```

Script chỉ nạp vào board có MAC khai báo ở dòng `cyd=` trong `../boards.local`, nên không nạp nhầm sang bộ đọc K-Line. Bỏ qua kiểm tra MAC khi chỉ cắm một board: `./flash.sh -f`.

## Thiết lập riêng cho màn này (`platformio.ini`)

| Cờ | Lý do |
|---|---|
| `ILI9341_2_DRIVER` | Driver của panel |
| `TFT_INVERSION_ON` | Panel này hiển thị **đảo màu** nếu không bật cờ này (đã kiểm tra bằng màn test màu) |
| `SPI_READ_FREQUENCY=6000000` | Đọc ngược dữ liệu từ màn hình (dùng khi chụp màn hình) ở tốc độ 20 MHz bị lệch kênh màu. 6 MHz thì đọc đúng |
| `ESPNOW_CHANNEL=1` | Phải trùng kênh WiFi AB125 của bộ đọc K-Line |

Nếu đổi sang màn CYD khác mà màu bị sai, dùng lệnh `p` (bên dưới) để tìm thiết lập đúng.

## Lệnh qua serial (115200 baud)

| Phím | Tác dụng |
|---|---|
| `s` | Chụp màn hình (dữ liệu RGB565 thô, dùng kèm `tools/screenshot.py`) |
| `d` | Bật/tắt chế độ demo, kể cả khi đang nhận dữ liệu thật |
| `p` | Màn test màu 4 ô A/B/C/D. Ô nào trông đúng màu thì cho biết cần thiết lập gì: A không cần sửa, B bật `TFT_INVERSION_ON`, C đổi thứ tự `TFT_RGB_ORDER=TFT_BGR`, D cần cả hai. Bấm phím bất kỳ để quay lại |
| `c` | Vẽ đỏ, xanh lá, xanh dương, trắng rồi đọc lại để kiểm tra tốc độ đọc SPI |

### Chụp màn hình
```bash
~/.platformio/penv/bin/python tools/screenshot.py -o screenshot.png
```
Mở cổng serial sẽ khởi động lại CYD. Vì vậy script chờ 4 giây cho dashboard chạy rồi mới chụp (đổi bằng `--wait`). Truyền một ảnh mất khoảng 15 giây. Nếu báo `Short read`, chạy lại.

## Gói dữ liệu ESP-NOW

`../common/dash_packet.h` định nghĩa `DashPacket` (magic `0xAB12`, version 1): status, seq, rpm, kmh, tps, ect, iat, battMv. Cả hai firmware dùng chung file này. Đổi định dạng gói thì tăng `DASH_VERSION` và nạp lại cả hai board.

## Cấu trúc thư mục
```
src/main.cpp            giao diện + nhận ESP-NOW + chế độ demo + lệnh serial
tools/screenshot.py     chụp màn hình CYD qua USB
flash.sh                build + nạp (gọi ../flash.sh -e cyd)
```
