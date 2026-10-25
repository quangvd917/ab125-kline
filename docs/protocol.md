# Giao thức Honda K-Line (Air Blade 125, 2023)

Tất cả thông tin dưới đây đã kiểm chứng trên xe thật. Log gốc nằm trong [sample-logs/](sample-logs/).

## Lớp vật lý
- Một dây **K-Line** dùng chung cho cả gửi và nhận, mức nghỉ khoảng 12V. Bên trong ECU, đường này được kéo lên qua một điện trở lớn và một diode: khi chưa có tải đo được khoảng 11.7V, nối tải 1kΩ thì tụt còn khoảng 0.1V.
- Bộ đọc phải tự **kéo K-Line lên 12V qua khoảng 510Ω–1kΩ**, vì điện trở kéo lên của ECU quá yếu để truyền dữ liệu.
- UART **10400 baud, 8N1**.
- Vì chỉ có một dây, mọi byte gửi đi đều bị đọc lại (**echo**). Firmware đọc phần echo, so với dữ liệu đã gửi để phát hiện lỗi dây, rồi bỏ đi.

## Khung dữ liệu
```
[header] [length] [data ...] [checksum]
```
- `length`: tổng số byte của cả khung, tính luôn header và checksum.
- `checksum` = `(0x100 − tổng các byte trước nó) & 0xFF`. Cộng mọi byte của một khung hợp lệ, kể cả checksum, sẽ được `0x00`.
- Lệnh gửi đi có header `0x72`. ECU trả lời với header `0x02`.

## Đánh thức ECU
1. Giữ K-Line ở mức cao (nghỉ) ít nhất 300 ms.
2. Kéo K-Line **xuống thấp 70 ms**.
3. Thả lên cao, chờ **130 ms**.
4. Gửi `FE 04 FF FF`. ECU không trả lời gói này.
5. Chờ 200 ms.
6. Gửi `72 05 00 F0 99`. ECU trả lời **`02 04 00 FA`**: đã kết nối.

Nếu bộ đọc khởi động lại giữa chừng trong khi ECU vẫn giữ phiên cũ, vài lần đánh thức đầu có thể không được trả lời. Sau khoảng vài giây, ECU hủy phiên cũ và kết nối lại bình thường.

## Đọc bảng dữ liệu
Gửi lệnh: `72 05 71 <bảng> <checksum>`. Ví dụ đọc bảng `0x17`: `72 05 71 17 01`.

ECU trả lời: `02 <len> 71 <bảng> <dữ liệu…> <checksum>`.

Kết quả quét các bảng trên Air Blade 2023:

| Bảng | Kết quả |
|---|---|
| `0x10`–`0x13` | Trống (chỉ có header) |
| **`0x17`** | **19 byte dữ liệu thời gian thực** |
| `0x20` | 4 byte, chưa rõ ý nghĩa |
| `0x21` | Trống |
| `0xD0` | 30 byte, gần như toàn số 0 |
| `0xD1` | 7 byte, giống cờ trạng thái |

## Bảng `0x17`
Đánh số byte từ 0, tính từ ngay sau `71 17`:

| Byte | Ý nghĩa | Cách đổi | Bằng chứng |
|---|---|---|---|
| 0–1 | **RPM** | `byte0 × 256 + byte1` | Tắt máy 0, không tải ~1500–1700, vặn ga lên ~3900 |
| 2 | Điện áp cảm biến bướm ga | `× 5/256` V | Đóng ga `0x19` ≈ 0.49V |
| 3 | **Độ mở bướm ga** | giá trị thô | 0 khi nhả ga, tăng khi vặn |
| 4–5 | Luôn `FF FF` | — | Có thể là cảm biến xe không có |
| 6 | Điện áp cảm biến khí nạp | — | |
| 7 | **Nhiệt độ khí nạp** | `− 40` °C | Đứng yên ~29–32°C |
| 8 | Điện áp cảm biến nhiệt độ máy | — | Giảm dần khi máy nóng lên |
| 9 | **Nhiệt độ máy (nước làm mát)** | `− 40` °C | Tăng dần 29 → 76°C khi máy nóng lên |
| 10 | **Điện áp ắc quy** | `÷ 10` V | Tắt máy 12.5V, nổ máy 14.5V |
| 11–12 | Chưa rõ | — | Thay đổi theo lượng phun xăng |
| 13 | Chưa rõ | — | Tăng theo tải |
| 14 | Chưa rõ | — | ~128 khi nhả ga, ~107 khi mở ga |
| 15 | **Tốc độ** | km/h | Trễ hơn RPM, giảm dần khi bánh quay trớn |
| 16–18 | Luôn 0 | — | |

## Chân SCS (4.7V) trên giắc DLC
Giắc DLC còn một chân ở khoảng 4.7V: đó là **SCS (Service Check Signal)**. Nối chân này xuống GND qua điện trở 1kΩ thì đèn FI nháy mã lỗi. Dự án này không dùng chân SCS.
