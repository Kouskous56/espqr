# ESP32 QR Code Generator

Dự án ESP32 tạo mã QR từ link nhập trên web và hiển thị lên màn hình — có 2 phiên bản theo màn hình:

| Thư mục | Màn hình | Giao diện |
|---------|----------|-----------|
| [`qrweb/`](qrweb/qrweb.ino) | OLED SSD1306 0.96" (I2C, 128x64) | QR bên phải, link bên trái |
| [`qrweb_tft/`](qrweb_tft/qrweb_tft.ino) | TFT ST7789 1.54" (SPI, 240x240) | Giao diện chuyên nghiệp: status bar, home screen, panel link |

## Tính năng

- **Web server** nhận link (HTTP POST `/api/qr`), sinh QR ngay trên ESP32, hiện lên màn hình và trả kết quả cho web (JSON + base64)
- **Web UI** đẹp: nhập link → poll trạng thái → render QR khớp 100% với màn hình
- **Tự chọn version QR (1–8)** theo độ dài link, tối đa 120 ký tự
- **Không crash**: state machine non-blocking + version tường minh (thư viện KHÔNG hỗ trợ version 0)
- **Auto-reconnect WiFi** + **mDNS** (`http://espqr.local`)
- OLED: bố cục QR lệch phải / link trái; TFT: nền tối, status bar, panel link bo góc, màn hình lỗi

## Phần cứng

- ESP32 DevKitC V4 (38 chân)
- Màn hình theo từng phiên bản (xem bảng trên)

### Nối dây OLED (`qrweb`)

| OLED | ESP32 |
|------|-------|
| VCC | 3V3 |
| GND | GND |
| SCL | G22 |
| SDA | G21 |

### Nối dây TFT ST7789 (`qrweb_tft`)

| TFT | ESP32 |
|-----|-------|
| VCC | 5V (nếu module có regulator) |
| GND | GND |
| SCL | G18 (SPI SCK) |
| SDA | G23 (SPI MOSI) |
| RST | G4 |
| DC | G2 |
| CS | G5 |
| BL | G21 |

## Cài đặt

1. **Cài Arduino CLI** (hoặc Arduino IDE)
2. **Cài board ESP32**: `arduino-cli core install esp32:esp32`
3. **Cài thư viện**:
   - `QRCodeGenerator` (ricmoo/QRCode)
   - `Adafruit GFX`, `Adafruit SSD1306` (cho `qrweb`), `Adafruit ST7735 and ST7789` (cho `qrweb_tft`)
4. **Cấu hình WiFi**: copy `wifi_config.example.h` → `wifi_config.h` trong thư mục dự án, điền SSID + password.

> ⚠️ `wifi_config.h` đã bị `.gitignore` — **tuyệt đối không commit file này** (chứa mật khẩu).

## Nạp code

```powershell
# Ví dụ với qrweb_tft (cổng COM4)
arduino-cli compile --fqbn esp32:esp32:esp32 "qrweb_tft"
arduino-cli upload -p COM4 --fqbn esp32:esp32:esp32 --upload-property upload.speed=115200 "qrweb_tft"
```

> Lưu ý: dùng `--upload-property upload.speed=115200` (baud 921600 hay lỗi "Unable to verify flash chip connection"). Nếu upload fail lần đầu, chạy lại lần 2.

## Sử dụng

1. Bật nguồn ESP32 → màn hình hiện IP (ví dụ `192.168.1.6`)
2. Mở trình duyệt truy cập `http://192.168.1.6/` (hoặc `http://espqr.local` nếu mạng hỗ trợ mDNS)
3. Nhập link → bấm **Tạo QR** → QR hiện trên màn hình + trên web
4. Dùng điện thoại quét QR

### API

| Phương thức | Đường dẫn | Mô tả |
|-------------|-----------|-------|
| GET | `/` | Trang web UI |
| POST | `/api/qr` | Body `url=<link>` → `{"status":"accepted"}` |
| GET | `/api/qr/status` | `{"ready":true,"size":N,"url":"...","b64":"..."}` |

## Cấu trúc mã nguồn (cả 2 phiên bản)

- **`pickVersion()`**: chọn version 1–8 theo dung lượng byte-mode ECC_LOW
- **State machine `qrStep()`**: `QR_GEN` (sinh QR) → `QR_COPY` (copy module) → `QR_DRAW` (vẽ từng hàng lên màn hình, non-blocking)
- **HTML trong PROGMEM**: tránh tốn RAM động
- **base64 module**: ESP32 trả dữ liệu QR nén base64, web tự render SVG

### Lưu ý kỹ thuật quan trọng

Thư viện `QRCodeGenerator` yêu cầu truyền **version tường minh (1–40)** khi gọi `qrcode_initText`. Truyền `0` sẽ khiến thư viện truy cập mảng `NUM_RAW_DATA_MODULES[-1]` → out-of-bounds → ESP32 reboot. Buffer `g_qrData` phải đủ `(49*49+7)/8 = 301` bytes cho version 8.

## Bảo mật

- Mật khẩu WiFi chỉ nằm trong `wifi_config.h` (gitignored)
- Repo GitHub chỉ chứa `wifi_config.example.h` với placeholder
