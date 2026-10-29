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

## Mã lỗi (DTC)
Cách đọc lấy từ phần mềm mã nguồn mở [HondaECU](https://github.com/Adeen117/HondaECU).

**Đã kiểm chứng trên Air Blade 2023 (xe không có lỗi):**
- ECU trả lời đủ 11 trang của cả hai bảng, ví dụ `02 0C 74 01 00 00 00 00 00 00 00 7D`.
- Mỗi trang dài 12 byte và có đủ 3 ô mã. Checksum đều hợp lệ.

**Chưa kiểm chứng:** vị trí byte của mã khi xe có lỗi thật. Phần này vẫn dựa trên HondaECU, cần đối chiếu với số lần nháy đèn FI khi xe gặp lỗi.

| Lệnh | Nội dung |
|---|---|
| `72 05 74 <trang> <cks>` | Lỗi **hiện tại**, trang 1–11 |
| `72 05 73 <trang> <cks>` | Lỗi **đã lưu**, trang 1–11 |

**Cấu trúc trả lời:** `02 <len> 74|73 <trang> ?? m1 s1 m2 s2 m3 s3 <cks>`. Mỗi trang chứa tối đa 3 mã, mỗi mã gồm (mã chính, mã phụ). `00` là ô trống. Ví dụ `07 01` là lỗi **7-1**.

**Ý nghĩa mã:** dùng bảng chung của Honda PGM-FI, lấy từ thư viện `eculib` đi kèm HondaECU:
- 1-x: cảm biến MAP
- 7-x: nhiệt độ nước làm mát
- 8-x: vị trí bướm ga
- 9-x: nhiệt độ khí nạp
- 11-1: cảm biến tốc độ
- 12-1: kim phun
- 21-1: cảm biến oxy
- 29-1: van IACV
- 54-x: cảm biến nghiêng đổ xe
- …

Bảng đầy đủ kèm mô tả tiếng Việt nằm trong `firmware/kline-reader/src/dashboard.h`.

**Nháy đèn FI:** đèn FI chỉ nháy được mã chính (1 lần nháy dài = 10, 1 lần nháy ngắn = 1). Ví dụ lỗi 54-1 nháy 5 lần dài và 4 lần ngắn.

**Chỉ đọc:** firmware **không** gửi lệnh xóa lỗi.

## Chân SCS (4.7V) trên giắc DLC
Giắc DLC còn một chân ở khoảng 4.7V: đó là **SCS (Service Check Signal)**. Nối chân này xuống GND qua điện trở 1kΩ thì đèn FI nháy mã lỗi. Dự án này không dùng chân SCS.
