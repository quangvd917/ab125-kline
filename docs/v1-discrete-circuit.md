# Mạch v1: giao tiếp K-Line bằng transistor (không cần IC)

Đây là mạch đầu tiên của dự án, làm từ linh kiện phổ thông và chạy ổn trên xe. Firmware hỗ trợ mạch này qua env **`wrover_2n2222`**.

## Xác định chân giắc DLC
Giắc DLC trên Air Blade 2023 có vỏ **6 chân nhưng chỉ 4 chân có tiếp xúc**. Đo bằng đồng hồ vạn năng khi đã bật khóa:

| Kết quả đo | Chân |
|---|---|
| 0Ω tới mass | GND |
| ~12.5V, vẫn giữ nguyên khi nối tải 1kΩ, **mất điện khi tắt khóa** | +12V sau khóa |
| ~11.7V, **tụt còn ~0.1V** khi nối tải 1kΩ | **K-Line** |
| ~4.7V | SCS (không dùng) |

**Cách thử tải:** mắc điện trở 1kΩ **song song** với đồng hồ, tức là từ chân cần thử xuống GND. Que đỏ đặt ở chân, que đen ở GND.

## Sơ đồ
```
 +12V ─[1kΩ]─┬──────────────── K-Line
             │
             C
 GPIO26 ─[1kΩ]─ B  (2N2222)       B ─[10kΩ]─ GND  (nên thêm: giữ transistor tắt khi ESP32 khởi động)
             E
             │
            GND

 K-Line ─[22kΩ]─┬── GPIO25
                │
             [6.8kΩ]
                │
               GND
```
- **Transistor đảo tín hiệu:** GPIO ở mức cao thì K-Line bị kéo xuống thấp. Vì vậy firmware bật chế độ đảo TX của UART (`K_TX_INVERTED=1`).
- **Cầu chia áp 22k/6.8k:** hạ mức 12–14.5V xuống khoảng 2.8–3.4V cho ESP32. Nếu dùng tỉ lệ 20k/6.8k thì ở 14.5V ra khoảng 3.7V, vượt mức chịu đựng của ESP32.
- **Với board ESP32-WROVER:** không dùng GPIO16/17, vì hai chân này nối với PSRAM bên trong module.

## Nạp firmware
```bash
cd firmware
./flash.sh -e wrover_2n2222
```
