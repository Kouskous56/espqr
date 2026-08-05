# ESP32 QR Code Generator

ESP32 nhận link gửi từ trình duyệt, tạo mã QR ngay trên máy rồi in ra màn hình. Trang web và màn hình hiện cùng một mã, nên bạn quét cái nào cũng được.

Có hai phiên bản, khác nhau ở màn hình:

| Thư mục | Màn hình | Khác nhau |
|---------|----------|-----------|
| `qrweb/` | OLED SSD1306 0.96" I2C (128x64) | QR nằm bên phải, link hiện bên trái |
| `qrweb_tft/` | TFT ST7789 1.54" SPI (240x240) | Có status bar, màn hình chờ, panel link riêng |

Code hai phiên bản giống nhau về logic, chỉ khác phần vẽ lên màn hình.

## Nó làm được gì

- Nhận link qua web (`POST /api/qr`), sinh QR ngay trên chip, không cần dịch vụ bên ngoài
- Trang web tự cập nhật trạng thái, vẽ lại QR y hệt màn hình
- Chọn version QR 1–8 theo độ dài link (tối đa 120 ký tự)
- Khi mất WiFi sẽ tự kết nối lại, có hỗ trợ `http://espqr.local` nếu mạng bạn bật mDNS
- QR vẽ từng hàng trong `loop()` nên không bao giờ treo cả hệ thống

## Phần cứng

- ESP32 DevKitC V4 (38 chân)
- Một trong hai màn hình ở trên

### Nối OLED (`qrweb`)

| OLED | ESP32 |
|------|-------|
| VCC | 3V3 |
| GND | GND |
| SCL | G22 |
| SDA | G21 |

### Nối TFT ST7789 (`qrweb_tft`)

| TFT | ESP32 |
|-----|-------|
| VCC | 5V (nếu module có sẵn mạch ổn áp) |
| GND | GND |
| SCL | G18 (SPI SCK) |
| SDA | G23 (SPI MOSI) |
| RST | G4 |
| DC | G2 |
| CS | G5 |
| BL | G21 |

## Cài đặt

1. Cài Arduino CLI hoặc Arduino IDE.
2. Cài board ESP32: `arduino-cli core install esp32:esp32`
3. Cài thư viện:
   - `QRCodeGenerator` (bản port của ricmoo/QRCode)
   - `Adafruit GFX` — bắt buộc
   - `Adafruit SSD1306` (chỉ `qrweb`)
   - `Adafruit ST7735 and ST7789` (chỉ `qrweb_tft`)
4. Điền thông tin WiFi. Mỗi thư mục dự án có file `wifi_config.example.h`. Copy thành `wifi_config.h` rồi sửa hai dòng dưới thành SSID và mật khẩu của bạn:

   ```cpp
   // wifi_config.h
   #pragma once
   #define WIFI_SSID     "TenWifiCuaBan"
   #define WIFI_PASSWORD "MatKhauWifiCuaBan"
   ```

   Thiếu file này thì không nạp được code.

## Nạp code

Ví dụ với `qrweb_tft`, cổng COM4:

```powershell
arduino-cli compile --fqbn esp32:esp32:esp32 "qrweb_tft"
arduino-cli upload -p COM4 --fqbn esp32:esp32:esp32 --upload-property upload.speed=115200 "qrweb_tft"
```

Upload ESP32 hay lỗi "Unable to verify flash chip connection" nếu để baud mặc định 921600, nên phải gắn thêm `--upload-property upload.speed=115200`. Lần đầu upload thường fail, chạy lại là được.

## Dùng

1. Cắm điện, chờ màn hình hiện IP (kiểu `192.168.1.6`).
2. Mở trình duyệt vào `http://192.168.1.6/` (hoặc `http://espqr.local` nếu mạng có mDNS).
3. Dán link, bấm **Tạo QR**. QR hiện trên màn hình và trên web.
4. Quét bằng điện thoại.

### API

| Method | Path | Trả về |
|--------|------|--------|
| GET | `/` | Trang web |
| POST | `/api/qr` | `url=<link>` → `{"status":"accepted"}` |
| GET | `/api/qr/status` | `{"ready":true,"size":N,"url":"...","b64":"..."}` |

## Code hoạt động thế nào

- `pickVersion()` — chọn version 1–8 vừa đủ chứa link, dựa trên bảng dung lượng byte-mode mức ECC_LOW.
- State machine `qrStep()` — `QR_GEN` sinh QR → `QR_COPY` chép module → `QR_DRAW` vẽ từng hàng lên màn hình. Chia nhỏ như vậy để loop không bị nghẽn, web vẫn trả lời trong lúc vẽ.
- HTML để trong PROGMEM, không chiếm RAM động.
- ESP32 trả dữ liệu QR dạng base64, trình duyệt tự dựng SVG.

### Một lỗi dễ mắc

Thư viện `QRCodeGenerator` bắt buộc truyền version rõ ràng (1–40) vào `qrcode_initText`. Truyền `0` sẽ khiến thư viện đọc mảng `NUM_RAW_DATA_MODULES[-1]` ra ngoài giới hạn, ESP32 reboot. Ngoài ra buffer `g_qrData` phải đủ `(49*49+7)/8 = 301` bytes cho version 8. Hai chỗ này từng làm máy reset liên tục khi tạo QR, đã sửa từ lâu.

## Bảo mật

`wifi_config.h` chứa mật khẩu của bạn, file này đã nằm trong `.gitignore` nên không bao giờ được đẩy lên git. Trong repo chỉ có `wifi_config.example.h` với placeholder, bạn tự điền khi clone về.
