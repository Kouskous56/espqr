/*
 * QR Web Server - ESP32 + TFT ST7789 1.54" SPI (240x240)
 * - Web: nhận URL -> sinh QR -> hiện lên TFT + trả kết quả
 * - Chân: SCL=18, SDA=23, CS=5, RST=4, DC=2, BL=21, VCC=5V
 * - Cấu trúc giữ nguyên qrweb (state machine, pickVersion) đã fix crash
 */
#include <WiFi.h>
#include <WebServer.h>
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7735.h>
#include <Adafruit_ST7789.h>
#include "QRCodeGenerator.h"

// ======= CẤU HÌNH =======
const char* ssid     = "REDACTED_SSID";
const char* password = "REDACTED_PASSWORD";

#define SCREEN_WIDTH  240
#define SCREEN_HEIGHT 240

// Chân TFT ST7789 (hardware SPI VSPI: MOSI=23, SCK=18)
#define TFT_CS   5
#define TFT_RST  4
#define TFT_DC   2
#define TFT_BL   21

#define MAX_QR_VERSION 8
#define MAX_QR_MODULES (4 * MAX_QR_VERSION + 17)  // 49
#define MAX_QR_DATA 120

// ======= BIẾN TOÀN CỤC =======
WebServer server(80);
Adafruit_ST7789 tft(TFT_CS, TFT_DC, TFT_RST);

// ======= MÀU GIAO DIỆN (RGB565) =======
uint16_t C_BG, C_PANEL, C_ACC, C_TXT, C_SUB, C_OK, C_ERR;

void setupColors() {
  C_BG    = tft.color565(10, 14, 32);   // nền tối navy
  C_PANEL = tft.color565(18, 24, 48);   // panel tối
  C_ACC   = tft.color565(60, 200, 255); // xanh dương accent
  C_TXT   = tft.color565(235, 240, 255);// chữ chính
  C_SUB   = tft.color565(130, 145, 180);// chữ phụ
  C_OK    = tft.color565(80, 255, 150); // xanh OK
  C_ERR   = tft.color565(255, 95, 95);  // đỏ lỗi
}

volatile bool g_doQR = false;
char g_pendingUrl[MAX_QR_DATA + 1];

struct QrResult {
  int size = 0;
  char url[MAX_QR_DATA + 1];
  uint8_t modules[MAX_QR_MODULES * MAX_QR_MODULES];
  bool ready = false;
  bool error = false;
} g_result;

// ======= HTML TĨNH (PROGMEM) =======
const char index_html[] PROGMEM = R"rawliteral(
<!doctype html><html><head><meta charset='utf-8'>
<meta name='viewport' content='width=device-width,initial-scale=1'>
<title>ESP32 QR Generator</title>
<style>
  body{font-family:sans-serif;background:#0f1222;color:#e6e9ff;margin:0;padding:24px 12px;display:flex;justify-content:center}
  .wrap{width:100%;max-width:420px;display:flex;flex-direction:column;gap:20px}
  .card,.last{background:#1c2138;padding:20px;border-radius:16px;box-shadow:0 10px 40px rgba(0,0,0,.5)}
  h1{font-size:20px;margin:0 0 6px;color:#7ec8ff}
  h2{font-size:14px;margin:0 0 12px;color:#98a2c8}
  p.sub{color:#98a2c8;font-size:13px;margin:0 0 18px}
  input{width:100%;padding:12px;border-radius:10px;border:1px solid #334;background:#0b0e1d;color:#fff;font-size:15px;box-sizing:border-box}
  button{width:100%;margin-top:12px;padding:12px;border:none;border-radius:10px;background:#2f9bff;color:#fff;font-size:16px;font-weight:700;cursor:pointer}
  button:hover{background:#1f7ee0}
  .status{margin-top:12px;padding:10px;border-radius:8px;background:#0b0e1d;font-size:13px;word-break:break-all}
  .qrbox{max-width:220px;margin:0 auto}
  .link{margin-top:10px;padding:10px;border-radius:8px;background:#0b0e1d;font-size:13px;word-break:break-all;text-align:center}
  .hidden{display:none}
</style></head><body><div class='wrap'>
<div class='card'>
  <h1>QR Code Generator</h1>
  <p class='sub'>Nhập link → hiển thị QR lên TFT & web</p>
  <form id='qrForm'>
    <input type='url' name='url' placeholder='https://example.com' autofocus required>
    <button type='submit'>Tạo QR</button>
  </form>
  <div id='status' class='status hidden'></div>
</div>
<div class='last hidden' id='lastCard'>
  <h2>QR vừa tạo (khớp TFT)</h2>
  <div class='qrbox' id='qrBox'></div>
  <div class='link' id='linkBox'></div>
</div>
</div>
<script>
function renderQR(size, b64) {
  const str = atob(b64);
  const modules = new Array(size * size);
  for (let i = 0; i < str.length; i++) {
    const byte = str.charCodeAt(i);
    for (let b = 7; b >= 0; b--) {
      const idx = i * 8 + (7 - b);
      if (idx < size * size) modules[idx] = (byte >> b) & 1;
    }
  }
  const module = 4, quiet = 2;
  const px = size * module + 2 * quiet;
  let svg = "<svg width='" + px + "' height='" + px +
    "' xmlns='http://www.w3.org/2000/svg' shape-rendering='crispEdges'" +
    " style='background:#fff;border-radius:8px;width:100%;height:auto' viewBox='0 0 " + px + " " + px + "'>";
  for (let y = 0; y < size; y++) {
    let x = 0;
    while (x < size) {
      if (!modules[y * size + x]) { x++; continue; }
      let start = x;
      while (x < size && modules[y * size + x]) x++;
      let rx = quiet + start * module;
      let ry = quiet + y * module;
      svg += "<rect x='" + rx + "' y='" + ry +
        "' width='" + (x - start) * module + "' height='" + module + "' fill='#000'/>";
    }
  }
  svg += "</svg>";
  return svg;
}

async function pollStatus() {
  const status = document.getElementById('status');
  for (let i = 0; i < 30; i++) {
    await new Promise(r => setTimeout(r, 1000));
    try {
      const res = await fetch('/api/qr/status');
      if (!res.ok) continue;
      const data = await res.json();
      if (data.ready) {
        if (data.error) { status.textContent = 'Lỗi: ' + data.error; return; }
        document.getElementById('qrBox').innerHTML = renderQR(data.size, data.b64);
        document.getElementById('linkBox').textContent = data.url;
        document.getElementById('lastCard').classList.remove('hidden');
        status.textContent = 'Đã tạo QR cho: ' + data.url;
        return;
      }
      status.textContent = 'Đang tạo QR... (' + i + 's)';
    } catch (e) {}
  }
  status.textContent = 'Hết thời gian chờ';
}

document.getElementById('qrForm').addEventListener('submit', async (e) => {
  e.preventDefault();
  const form = e.target;
  const url = form.url.value.trim();
  if (!url) return;
  const status = document.getElementById('status');
  status.classList.remove('hidden');
  status.textContent = 'Đang gửi...';
  try {
    const res = await fetch('/api/qr', {
      method: 'POST',
      headers: {'Content-Type': 'application/x-www-form-urlencoded'},
      body: 'url=' + encodeURIComponent(url)
    });
    if (!res.ok) throw new Error('Lỗi server');
    status.textContent = 'Đã nhận, đang xử lý...';
    await pollStatus();
  } catch (err) {
    status.textContent = 'Lỗi: ' + err.message;
  }
});
</script></body></html>
)rawliteral";

// ======= TIỆN ÍCH =======
void sendHTML() {
  server.sendHeader("Connection", "close");
  server.send_P(200, "text/html; charset=utf-8", index_html);
}

void sendJSON(int code, const String& json) {
  server.sendHeader("Connection", "close");
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.send(code, "application/json", json);
}

// Base64 encode modules
String modulesToBase64(const uint8_t* modules, int size) {
  int bits = size * size;
  int bytes = (bits + 7) / 8;
  uint8_t buf[301];
  memset(buf, 0, bytes);
  for (int i = 0; i < bits; i++) {
    if (modules[i]) buf[i >> 3] |= (1 << (7 - (i & 7)));
  }
  const char* b64 = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  String out;
  out.reserve(((bytes + 2) / 3) * 4);
  for (int i = 0; i < bytes; i += 3) {
    uint32_t v = (buf[i] << 16) | (i+1<bytes ? buf[i+1] << 8 : 0) | (i+2<bytes ? buf[i+2] : 0);
    out += b64[(v >> 18) & 63];
    out += b64[(v >> 12) & 63];
    out += (i+1<bytes) ? b64[(v >> 6) & 63] : '=';
    out += (i+2<bytes) ? b64[v & 63] : '=';
  }
  return out;
}

enum QrState { QR_IDLE, QR_GEN, QR_COPY, QR_DRAW, QR_DONE };
QrState g_qrState = QR_IDLE;
int g_qrRow = 0;
QRCode g_qrcode;
uint8_t g_qrData[301];   // version 8 = 49 modules -> (49*49+7)/8 = 301 bytes

// Chọn version QR (1..8) vừa đủ chứa chuỗi, trả 0 nếu quá dài.
// Dung lượng byte-mode ECC_LOW theo chuẩn QR (version 1..8)
static const uint16_t QR_CAPACITY_LOW[8] = { 17, 32, 53, 78, 106, 134, 154, 192 };

int pickVersion(const char* s) {
  int len = strlen(s);
  for (int v = 1; v <= 8; v++) {
    if (len <= (int)QR_CAPACITY_LOW[v - 1]) return v;
  }
  return 0; // quá dài
}

// ======= GIAO DIỆN TFT =======
// Thanh trạng thái trên cùng: tiêu đề trái + trạng thái phải
void drawStatusBar(const char* right, uint16_t rightColor) {
  tft.fillRect(0, 0, SCREEN_WIDTH, 22, C_PANEL);
  tft.drawFastHLine(0, 22, SCREEN_WIDTH, C_ACC);
  tft.setTextSize(1);
  tft.setTextColor(C_ACC);
  tft.setTextWrap(false);
  tft.setCursor(6, 6);
  tft.print("ESP32 QR");
  tft.setTextColor(rightColor);
  tft.setCursor(SCREEN_WIDTH - 6 - strlen(right) * 6, 6);
  tft.print(right);
}

// Màn hình chờ (sau khi khởi động / trước khi có QR)
void drawHome() {
  tft.fillScreen(C_BG);

  tft.setTextSize(2);
  tft.setTextColor(C_ACC);
  const char* title = "QR GENERATOR";
  tft.setCursor((SCREEN_WIDTH - strlen(title) * 12) / 2, 56);
  tft.print(title);

  tft.setTextSize(1);
  tft.setTextColor(C_SUB);
  const char* sub = "ESP32  +  ST7789 1.54";
  tft.setCursor((SCREEN_WIDTH - strlen(sub) * 6) / 2, 88);
  tft.print(sub);

  tft.drawFastHLine(30, 108, SCREEN_WIDTH - 60, C_PANEL);

  tft.setTextColor(C_TXT);
  char buf[40];
  snprintf(buf, sizeof(buf), "IP: %s", WiFi.localIP().toString().c_str());
  tft.setCursor((SCREEN_WIDTH - strlen(buf) * 6) / 2, 126);
  tft.print(buf);

  tft.setTextColor(C_SUB);
  const char* hint1 = "Nhap link tren web";
  const char* hint2 = "QR hien ra tai day";
  tft.setCursor((SCREEN_WIDTH - strlen(hint1) * 6) / 2, 148);
  tft.print(hint1);
  tft.setCursor((SCREEN_WIDTH - strlen(hint2) * 6) / 2, 158);
  tft.print(hint2);

  drawStatusBar("STANDBY", C_SUB);
}

// Panel link dưới cùng, tự xuống dòng
void drawUrlPanel(const char* url) {
  int y0 = SCREEN_HEIGHT - 52;
  int h = 48;
  tft.fillRoundRect(4, y0, SCREEN_WIDTH - 8, h, 6, C_PANEL);
  tft.drawRoundRect(4, y0, SCREEN_WIDTH - 8, h, 6, C_ACC);

  tft.setTextSize(1);
  tft.setTextColor(C_SUB);
  tft.setTextWrap(false);
  tft.setCursor(12, y0 + 3);
  tft.print("LINK");

  int lineCap = (SCREEN_WIDTH - 24) / 6;   // 36 ký tự/dòng
  int len = strlen(url);
  int y = y0 + 15;
  char buf[40];
  tft.setTextColor(C_TXT);
  for (int i = 0; i < len && y < y0 + h - 4;) {
    int n = len - i;
    if (n > lineCap) n = lineCap;
    memcpy(buf, url + i, n);
    buf[n] = 0;
    tft.setCursor(12, y);
    tft.print(buf);
    y += 8;
    i += n;
  }
}

// Màn hình lỗi
void drawError(const char* msg) {
  tft.fillScreen(C_BG);
  drawStatusBar("ERROR", C_ERR);

  tft.setTextSize(2);
  tft.setTextColor(C_ERR);
  const char* t = "QR LOI";
  tft.setCursor((SCREEN_WIDTH - strlen(t) * 12) / 2, 76);
  tft.print(t);

  tft.setTextSize(1);
  tft.setTextColor(C_TXT);
  tft.setTextWrap(false);
  int lineCap = 36;
  int len = strlen(msg);
  int y = 116;
  char buf[40];
  for (int i = 0; i < len && y < SCREEN_HEIGHT - 8;) {
    int n = len - i;
    if (n > lineCap) n = lineCap;
    memcpy(buf, msg + i, n);
    buf[n] = 0;
    tft.setCursor((SCREEN_WIDTH - n * 6) / 2, y);
    tft.print(buf);
    y += 10;
    i += n;
  }
}

// Chạy một bước nhỏ của QR generation trong loop()
void qrStep() {
  switch (g_qrState) {
    case QR_GEN: {
      // init QR với version tường minh (1..8) - KHÔNG dùng version 0 (gây crash thư viện)
      int ver = pickVersion(g_pendingUrl);
      Serial.printf("QR_GEN url=[%s] len=%d ver=%d\n", g_pendingUrl, (int)strlen(g_pendingUrl), ver);
      if (ver == 0) {
        g_result.error = true;
        strlcpy(g_result.url, "QR too long", sizeof(g_result.url));
        g_result.ready = true;
        drawError("Link qua dai cho QR");
        g_qrState = QR_IDLE;
        return;
      }
      int rc = qrcode_initText(&g_qrcode, g_qrData, ver, ECC_LOW, g_pendingUrl);
      Serial.printf("QR_GEN rc=%d size=%d\n", rc, g_qrcode.size);
      if (rc) {
        g_result.error = true;
        strlcpy(g_result.url, "QR error", sizeof(g_result.url));
        g_result.ready = true;
        drawError("QR loi khi tao");
        g_qrState = QR_IDLE;
        return;
      }
      g_result.size = g_qrcode.size;
      strlcpy(g_result.url, g_pendingUrl, sizeof(g_result.url));
      g_qrRow = 0;
      g_qrState = QR_COPY;
      break;
    }
    case QR_COPY: {
      // copy một hàng module
      for (int x = 0; x < g_qrcode.size; x++) {
        g_result.modules[g_qrRow * g_qrcode.size + x] = qrcode_getModule(&g_qrcode, x, g_qrRow);
      }
      g_qrRow++;
      if (g_qrRow >= g_qrcode.size) {
        g_qrRow = 0;
        g_qrState = QR_DRAW;
      }
      yield();
      break;
    }
    case QR_DRAW: {
      // Bố cục: status bar 22px | vùng QR giữa | panel link dưới
      const int barH = 22;
      int qrY0 = barH + 6;
      int qrAreaH = SCREEN_HEIGHT - qrY0 - 56;   // 156
      int quiet = 2;                              // vùng lặng quanh QR (px)
      int scale = (qrAreaH - 2 * quiet) / g_qrcode.size;
      if (scale < 1) scale = 1;
      int px = g_qrcode.size * scale;
      int offX = (SCREEN_WIDTH - px) / 2;
      int offY = qrY0 + (qrAreaH - px) / 2;

      if (g_qrRow == 0) {
        tft.fillScreen(C_BG);
        drawStatusBar("READY", C_OK);
        // nền trắng cho QR + viền accent
        tft.fillRect(offX - quiet, offY - quiet, px + 2 * quiet, px + 2 * quiet, ST77XX_WHITE);
        tft.drawRect(offX - quiet, offY - quiet, px + 2 * quiet, px + 2 * quiet, C_ACC);
        drawUrlPanel(g_result.url);
      }

      for (int x = 0; x < g_qrcode.size; x++) {
        if (g_result.modules[g_qrRow * g_qrcode.size + x]) {
          tft.fillRect(offX + x * scale, offY + g_qrRow * scale, scale, scale, ST77XX_BLACK);
        }
      }
      g_qrRow++;
      if (g_qrRow >= g_qrcode.size) {
        g_result.ready = true;
        g_qrState = QR_IDLE;
      }
      yield();
      break;
    }
    default:
      g_qrState = QR_IDLE;
  }
}

// ======= HANDLERS =======
void handleRoot() { sendHTML(); }

void handleAPI() {
  if (!server.hasArg("url")) { sendJSON(400, "{\"error\":\"missing url\"}"); return; }
  String url = server.arg("url");
  url.trim();
  if (url.length() == 0 || url.length() > MAX_QR_DATA) { sendJSON(400, "{\"error\":\"invalid url length\"}"); return; }

  url.toCharArray(g_pendingUrl, sizeof(g_pendingUrl));
  g_result.ready = false;
  g_result.error = false;
  g_qrState = QR_GEN;
  sendJSON(202, "{\"status\":\"accepted\"}");
}

void handleStatus() {
  if (!g_result.ready) { sendJSON(200, "{\"ready\":false}"); return; }
  if (g_result.error) { sendJSON(200, "{\"ready\":true,\"error\":\"" + String(g_result.url) + "\"}"); return; }
  String b64 = modulesToBase64(g_result.modules, g_result.size);
  String json = "{\"ready\":true,\"size\":" + String(g_result.size) + ",\"url\":\"" + String(g_result.url) + "\",\"b64\":\"" + b64 + "\"}";
  sendJSON(200, json);
}

void handleNotFound() { sendJSON(404, "{\"error\":\"not found\"}"); }

// ======= SETUP =======
void setup() {
  Serial.begin(115200);
  delay(200);

  pinMode(TFT_BL, OUTPUT);
  digitalWrite(TFT_BL, HIGH);   // bật đèn nền

  tft.init(SCREEN_WIDTH, SCREEN_HEIGHT);
  tft.setRotation(0);
  setupColors();
  tft.fillScreen(C_BG);
  tft.setTextSize(2);
  tft.setTextColor(C_ACC);
  tft.setCursor(28, 100);
  tft.print("WiFi...");

  // WiFi
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);

  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < 20000) {
    delay(200);
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.print("IP: ");
    Serial.println(WiFi.localIP());
    drawHome();
  } else {
    Serial.println("WiFi connect failed");
    tft.fillScreen(C_BG);
    drawStatusBar("ERROR", C_ERR);
    tft.setTextSize(2);
    tft.setTextColor(C_ERR);
    tft.setCursor(44, 100);
    tft.print("WiFi FAIL");
  }

  server.on("/", HTTP_GET, handleRoot);
  server.on("/api/qr", HTTP_POST, handleAPI);
  server.on("/api/qr/status", HTTP_GET, handleStatus);
  server.onNotFound(handleNotFound);
  server.begin();
  Serial.println("HTTP server started");
}

// ======= LOOP: xử lý QR theo state machine =======
void loop() {
  server.handleClient();
  if (g_qrState != QR_IDLE) {
    qrStep();
  }
}
