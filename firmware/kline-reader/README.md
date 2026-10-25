# kline-reader: firmware đọc K-Line

Firmware chạy trên **ESP32-C3 SuperMini** cùng mạch [L9637D](../../docs/hardware.md). Cũng chạy được trên ESP32 với [mạch v1 dùng 2N2222](../../docs/v1-discrete-circuit.md).

Firmware làm 3 việc:
- Đánh thức ECU và đọc **bảng `0x17`** khoảng 7 lần mỗi giây. Xem [protocol.md](../../docs/protocol.md).
- Phát WiFi **AB125** (mật khẩu `ab125kline`) kèm trang web đồng hồ tại `http://192.168.4.1`. API `GET /data` trả về JSON.
- Gửi gói **ESP-NOW** `DashPacket` trên kênh 1 cho [dashboard CYD](../dashboard-cyd).

Muốn đổi tên hoặc mật khẩu WiFi, sửa `AP_SSID` / `AP_PASS` trong `src/main.cpp`.

## Cấu hình (`platformio.ini`)
| Env | Board | RX / TX | `K_TX_INVERTED` |
|---|---|---|---|
| `c3_supermini` (mặc định) | ESP32-C3 SuperMini + L9637D | GPIO3 / GPIO4 | 0 |
| `wrover_2n2222` | ESP32 (WROVER) + 2N2222 | GPIO25 / GPIO26 | 1 |

**Chân cần tránh trên C3:**
- **GPIO2, 8, 9:** C3 đọc các chân này lúc khởi động để chọn chế độ.
- **GPIO20, 21:** C3 in log khởi động ra GPIO21. Nếu dùng làm TX, các xung đó sẽ lọt lên K-Line.

**Giảm công suất WiFi:** env `c3_supermini` giảm công suất phát xuống 8.5dBm, vì nhiều bản SuperMini có WiFi chập chờn khi phát hết công suất. Nếu WiFi vẫn yếu, thử 11dBm. Nếu board của bạn không bị lỗi này, xóa dòng `WIFI_TX_POWER`.

## Nạp firmware
```bash
./flash.sh            # C3 SuperMini
./flash.sh -m         # nạp xong mở serial monitor
./flash.sh -e wrover_2n2222
```
Script kiểm tra MAC của board, khai báo trong `../boards.local` (xem [../flash.sh](../flash.sh)).

## Đèn LED trạng thái (GPIO8 trên C3)
| Đèn | Nghĩa |
|---|---|
| Sáng liên tục | Đang nhận dữ liệu từ ECU |
| Nháy nhanh | Đang kết nối hoặc kết nối lại |
| Nháy chậm (1 giây) | K-Line có tín hiệu nhưng ECU không trả lời |
| Chớp ngắn mỗi 2 giây | Không có tín hiệu K-Line |

## Log serial (115200 baud)
```
=== Honda K-Line reader v2 ===
RX=GPIO3 TX=GPIO4 (L9637D)
INIT OK: 02 04 00 FA
rpm=1622 km/h=0 tps=0 ect=63C iat=29C batt=14.5V | raw 06 56 19 00 FF FF ...
```
Các dòng báo lỗi:
- **`Echo mismatch` / `Bad echo`:** không có tín hiệu K-Line. Thường do chưa bật khóa, thiếu 12V, hoặc thiếu 3.3V cho L9637D.
- **`INIT FAIL, code=0`:** mạch chạy đúng nhưng ECU không trả lời. Trên bàn (chưa cắm xe) đây là kết quả đúng.
- **`Lost connection`:** mất kết nối, firmware tự kết nối lại.

**Ghi log ra file:**
```bash
python scripts/log_kline.py            # tới khi bấm Ctrl+C
python scripts/log_kline.py -d 120     # 2 phút
```
Script cần `pyserial`. Python đi kèm PlatformIO đã có sẵn: `~/.platformio/penv/bin/python`. Log lưu ở `logs/`.

Mở cổng serial sẽ làm C3 khởi động lại. ECU có thể cần vài giây để hủy phiên cũ trước khi kết nối lại.
