# ⚡ SmartSac — Tài Liệu Kiến Trúc & Vận Hành Hệ Thống

Tài liệu chi tiết về kiến trúc, cơ sở dữ liệu, các lớp bảo mật, cơ chế đồng bộ realtime và hướng dẫn vận hành hệ thống **SmartSac** (Ổ cắm sạc xe điện thông minh).

---

## 📌 1. Giới thiệu Tổng quan
**SmartSac** là hệ thống giám sát và điều khiển sạc xe điện thông minh. Hệ thống giải quyết các vấn đề an toàn phòng chống cháy nổ và tối ưu hóa thời gian sạc bằng cách:
* Giám sát thông số điện áp ($V$), dòng điện ($A$), công suất ($W$), điện năng tiêu thụ ($kWh$).
* Đo đạc nhiệt độ bộ sạc (cảm biến DS18B20), nhiệt độ vỏ bình/pin (MLX90614 không tiếp xúc), và nhiệt độ/độ ẩm môi trường (DHT).
* Tự động ngắt sạc khi phát hiện quá nhiệt, quá dòng, quá áp, sụt áp mạng lưới, hoặc khi pin đầy dựa trên cấu hình động được phân tích bởi AI (Gemini).
* Điều khiển từ xa qua Dashboard realtime (Web/PWA/Android App) và gửi cảnh báo qua Telegram.

---

## 🏛️ 2. Kiến trúc Hệ thống

Hệ thống hoạt động theo mô hình Hybrid kết hợp **Realtime Cache (Redis)** và **Persistent Database (MySQL)**:

```
                  ┌────────────────────────────────────────┐
                  │              ESP32 Device              │
                  └───────┬────────────────────────▲───────┘
                          │ (3s)                   │ (7s)
            POST Telemetry│                        │GET Config & Commands
                          ▼                        │
         ┌────────────────────────────────────────────────────────┐
         │                  Flask Web Server                      │
         │   (Xác thực Token, Rate Limit, Phân tích Ngưỡng An Toàn)│
         └──────┬──────────────────────┬───────────────────▲──────┘
                │                      │                   │
                ▼                      ▼                   │
       ┌─────────────────┐    ┌─────────────────┐          │
       │   Redis Cache   │    │    MySQL DB     │    ┌─────┴──────┐
       │ (TTL 60s, Queue)│    │ (Users, Configs,│    │ Gemini AI  │
       │  - Telemetry    │    │  Logs, Stats)   │    │ (Analyze & │
       │  - Commands     │    └─────────────────┘    │  Optimize) │
       └────────▲────────┘                           └────────────┘
                │ (Polling / Server-Sent Events)
                │
       ┌────────┴────────┐
       │ Dashboard (WEB) │
       │ (React/PWA/APK) │
       └─────────────────┘
```

1. **ESP32 Firmware**: 
   * Gửi dữ liệu cảm biến (`POST /api/station/device/telemetry`) định kỳ mỗi **3 giây**.
   * Lấy cấu hình và lệnh điều khiển từ server (`GET /api/station/device/config`) định kỳ mỗi **7 giây**.
2. **Flask Web Server**:
   * Kiểm soát request thông qua các bộ lọc Middleware (Rate limiting bằng Redis, kiểm tra an toàn telemetry bằng `verify_telemetry_safety`).
   * Phân phối luồng dữ liệu: telemetry lưu tạm vào **Redis** dạng Key-Value để truy xuất nhanh, đồng thời cập nhật dữ liệu lịch sử/thống kê định kỳ vào **MySQL**.
3. **Cơ chế Realtime**:
   * Dashboard frontend lấy dữ liệu realtime thông qua API hoặc SSE kết nối đến Redis, hạn chế tối đa tải truy vấn trực tiếp vào MySQL.

---

## ⚡ 3. Quy Trình Vận Hành & Cách Hoạt Động

### Luồng Hoạt Động Của Thiết Bị Sạc (ESP32)

```
                       ┌─────────────────────────┐
                       │    Thiết bị khởi động   │
                       └────────────┬────────────┘
                                    │
                                    ▼
                       ┌─────────────────────────┐
                       │ Đọc cấu hình từ API     │◄────────────────┐
                       │ (GET /device/config)    │                 │
                       └────────────┬────────────┘                 │
                                    │                              │
                                    ▼                              │
                       ┌─────────────────────────┐                 │
                       │    Có lệnh điều khiển?  │                 │
                       └────────────┬────────────┘                 │
                                    │                              │
                        Có          │ Không                        │
                   ┌────────────────┴────────────────┐             │
                   ▼                                 ▼             │
        ┌─────────────────────┐           ┌─────────────────────┐  │
        │ Thực thi lệnh       │           │ Chạy theo cấu hình  │  │
        │ (Bật/Tắt Relay sạc) │           │ tự động (Ngắt nếu   │  │
        └──────────┬──────────┘           │ quá nhiệt/đầy pin)  │  │
                   │                      └──────────┬──────────┘  │
                   └────────────────┬────────────────┘             │
                                    │                              │ (Lặp lại)
                                    ▼                              │
                       ┌─────────────────────────┐                 │
                       │ Đọc cảm biến & gửi dữ   │                 │
                       │ liệu lên Web server     │─────────────────┘
                       │ (POST /device/telemetry)│
                       └─────────────────────────┘
```

#### Bước 1: Khởi động và Xác thực
Thiết bị ESP32 lưu Station Token nhận được từ giao diện quản trị trạm sạc. Mỗi request lên Web Server đều kèm theo token này.

#### Bước 2: Chu kỳ Telemetry (3s / lần)
Thiết bị đọc các cảm biến và POST lên `/api/station/device/telemetry`.
* Server kiểm tra token, kiểm tra rate limit, kiểm tra an toàn (`verify_telemetry_safety`).
* Nếu dữ liệu an toàn, server lưu trạng thái mới nhất vào Redis với key `sc:telemetry:<station_id>` (TTL 60 giây).
* Dashboard của người dùng lắng nghe/fetch dữ liệu từ Redis để hiển thị biểu đồ realtime.

#### Bước 3: Chu kỳ Nhận cấu hình & Lệnh điều khiển (7s / lần)
Thiết bị gửi GET request đến `/api/station/device/config`.
* Server trả về các cấu hình bảo vệ (nhiệt độ tối đa, độ ẩm tối đa, công suất tối đa...).
* Server cũng kiểm tra xem trong Redis key `sc:command:<station_id>` có chứa lệnh điều khiển nào từ người dùng gửi qua Dashboard không (như `charge_now`, `charge_stop`). Nếu có, trả lệnh về cho thiết bị thực thi và xóa lệnh trong hàng đợi.

---

## 💻 4. Tài Liệu Kết Nối IoT (ESP32 / Arduino C++)

Mục này hướng dẫn chi tiết cách thiết bị ESP32 cấu hình kết nối, gửi dữ liệu telemetry, nhận cấu hình điều khiển từ máy chủ, xử lý lỗi an toàn phần cứng và đính kèm mã nguồn mẫu hoàn chỉnh bằng C++.

### A. Phương thức Xác thực với Server
ESP32 cần đính kèm Station Token do hệ thống cung cấp vào Header HTTP khi thực hiện mọi yêu cầu API. Hỗ trợ hai phương thức header:
* `Authorization: Bearer <STATION_TOKEN>`
* `X-Station-Token: <STATION_TOKEN>`

### B. Cấu trúc Payload Telemetry (JSON gửi lên)
Được gửi qua phương thức `POST /api/station/device/telemetry`.
```json
{
  "p": 128.5,             // Công suất tiêu thụ hiện tại (Watts)
  "v": 220.3,             // Điện áp lưới điện AC (Volts)
  "i": 0.58,              // Dòng điện tải (Amperes)
  "ds": 36.2,             // Nhiệt độ bộ sạc (Độ C, cảm biến DS18B20)
  "mlx": 38.7,            // Nhiệt độ vỏ bình/pin (Độ C, MLX90614)
  "dht_t": 30.0,          // Nhiệt độ môi trường (Độ C, DHT)
  "dht_h": 64.5,          // Độ ẩm môi trường (Phần trăm, DHT)
  "state": "DANG_SAC",    // Trạng thái sạc: DANG_SAC, SAC_CHO, FULL, DUNG, LOI
  "charge_time_min": 47,  // Thời gian đã sạc tính bằng phút
  "sensor_ds_ok": true,   // Trạng thái cảm biến DS18B20 (true = hoạt động, false = lỗi)
  "sensor_mlx_ok": true,  // Trạng thái cảm biến MLX90614 (true = hoạt động, false = lỗi)
  "sensor_pzem_ok": true  // Trạng thái cảm biến PZEM (true = hoạt động, false = lỗi)
}
```
*Lưu ý:* Khi bất kỳ cảm biến nào bị hỏng hóc hoặc không có dữ liệu, hãy truyền giá trị `"NA"` hoặc `null` thay vì giá trị `0` để tránh kích hoạt tính năng dừng khẩn cấp do sụt áp lưới hoặc mất kiểm soát cảm biến.

### C. Cơ Chế Xử Lý Lỗi Và Ngắt Sạc Khẩn Cấp (Safety Check)
Khi thực hiện POST dữ liệu telemetry hoặc GET cấu hình, ESP32 phải phân tích kỹ phản hồi từ máy chủ:
1. **Trường hợp Telemetry bình thường (HTTP 200 OK):**
   * Server trả về JSON dạng: `{"status": "success", ...}`.
   * Thiết bị tiếp tục chu kỳ sạc bình thường.
2. **Trường hợp Phát hiện Nguy hiểm (HTTP 400 Bad Request):**
   * Server trả về JSON chứa mã lỗi và hành động xử lý:
     ```json
     {
       "status": "error",
       "error_code": "SAFETY_RISK_DETECTED",
       "message": "CẢNH BÁO AN TOÀN: Nhiệt độ pin (66.5°C) vượt ngưỡng...",
       "action": "charge_stop",
       "command": "charge_stop"
     }
     ```
   * Khi phát hiện trường hợp này hoặc nhận mã HTTP lỗi, ESP32 phải **lập tức ngắt relay sạc bằng phần cứng**, chuyển trạng thái thiết bị sang `LOI` hoặc `DUNG`, và hiển thị chuỗi `message` lên màn hình LCD/OLED nếu có.

### D. Mã Nguồn Mẫu Hoàn Chỉnh ESP32 (Arduino IDE C++)

Dưới đây là mã nguồn mẫu tối ưu sử dụng thư viện `WiFi.h`, `HTTPClient.h` và `ArduinoJson.h` (yêu cầu cài đặt thư viện ArduinoJson phiên bản 6 hoặc 7).

```cpp
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>

// 1. Cấu hình mạng WiFi và thông tin kết nối Server
const char* ssid = "YOUR_WIFI_SSID";
const char* password = "YOUR_WIFI_PASSWORD";

const char* server_url_telemetry = "http://YOUR_SERVER_IP:8888/api/station/device/telemetry";
const char* server_url_config    = "http://YOUR_SERVER_IP:8888/api/station/device/config";
const char* station_token        = "SK_YOUR_SECURE_STATION_TOKEN"; // Lấy từ giao diện quản trị

// 2. Định nghĩa chân phần cứng điều khiển Relay sạc
#define RELAY_PIN 23 
bool is_charging_enabled = false;

// 3. Khai báo chu kỳ gửi dữ liệu (milli-giây)
unsigned long last_telemetry_time = 0;
const unsigned long telemetry_interval = 3000; // 3 giây gửi telemetry một lần

unsigned long last_config_time = 0;
const unsigned long config_interval = 7000;    // 7 giây lấy cấu hình & lệnh một lần

void setup() {
  Serial.begin(115200);
  pinMode(RELAY_PIN, OUTPUT);
  digitalWrite(RELAY_PIN, LOW); // Mặc định tắt sạc khi khởi động

  // Kết nối WiFi
  Serial.print("Connecting to WiFi");
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWiFi Connected!");
}

void loop() {
  if (WiFi.status() != WL_CONNECTED) {
    // Nếu mất WiFi, tự động ngắt relay sạc để đảm bảo an toàn ngoại tuyến
    digitalWrite(RELAY_PIN, LOW);
    is_charging_enabled = false;
    Serial.println("WiFi disconnected! Relay turned OFF for safety.");
    delay(1000);
    return;
  }

  unsigned long current_time = millis();

  // Chu kỳ gửi telemetry
  if (current_time - last_telemetry_time >= telemetry_interval) {
    last_telemetry_time = current_time;
    send_telemetry();
  }

  // Chu kỳ lấy cấu hình & nhận lệnh điều khiển
  if (current_time - last_config_time >= config_interval) {
    last_config_time = current_time;
    check_config_and_commands();
  }
}

// Hàm giả lập đọc giá trị cảm biến
float read_pzem_voltage() { return 220.5; }
float read_pzem_current() { return is_charging_enabled ? 4.2 : 0.0; }
float read_pzem_power()   { return is_charging_enabled ? 924.0 : 0.0; }
float read_ds18b20_temp() { return 35.8; }
float read_mlx90614_temp(){ return 38.2; }
float read_dht_temp()     { return 30.5; }
float read_dht_humidity() { return 65.0; }

// Hàm gửi dữ liệu Telemetry lên Web Server
void send_telemetry() {
  HTTPClient http;
  http.begin(server_url_telemetry);
  
  // Thiết lập Headers bắt buộc
  http.addHeader("Content-Type", "application/json");
  http.addHeader("Authorization", "Bearer " + String(station_token));

  // Tạo tài liệu JSON payload
  StaticJsonDocument<512> doc;
  doc["v"] = read_pzem_voltage();
  doc["i"] = read_pzem_current();
  doc["p"] = read_pzem_power();
  doc["ds"] = read_ds18b20_temp();
  doc["mlx"] = read_mlx90614_temp();
  doc["dht_t"] = read_dht_temp();
  doc["dht_h"] = read_dht_humidity();
  doc["state"] = is_charging_enabled ? "DANG_SAC" : "DUNG";
  doc["charge_time_min"] = 15; // Giả lập thời gian sạc
  doc["sensor_ds_ok"] = true;
  doc["sensor_mlx_ok"] = true;
  doc["sensor_pzem_ok"] = true;

  String json_payload;
  serializeJson(doc, json_payload);

  Serial.print("Sending telemetry: ");
  Serial.println(json_payload);

  int http_code = http.POST(json_payload);
  String response_body = http.getString();
  http.end();

  Serial.printf("HTTP Code: %d\n", http_code);
  Serial.println("Response: " + response_body);

  if (http_code == 200 || http_code == 201) {
    Serial.println("Telemetry sent successfully!");
  } 
  else if (http_code == 400 || http_code == 403) {
    // Khi Server trả về lỗi 400/403 kèm cảnh báo an toàn
    StaticJsonDocument<512> res_doc;
    DeserializationError err = deserializeJson(res_doc, response_body);
    if (!err) {
      String action = res_doc["action"].as<String>();
      String message = res_doc["message"].as<String>();
      if (action == "charge_stop") {
        digitalWrite(RELAY_PIN, LOW); // NGẮT SẠC KHẨN CẤP
        is_charging_enabled = false;
        Serial.println("EMERGENCY STOP DETECTED: " + message);
      }
    }
  }
}

// Hàm lấy cấu hình và lệnh điều khiển từ Web Server
void check_config_and_commands() {
  HTTPClient http;
  http.begin(server_url_config);
  
  // Thiết lập Headers bắt buộc
  http.addHeader("Authorization", "Bearer " + String(station_token));

  int http_code = http.GET();
  String response_body = http.getString();
  http.end();

  if (http_code == 200) {
    StaticJsonDocument<1024> res_doc;
    DeserializationError err = deserializeJson(res_doc, response_body);
    if (err) {
      Serial.print("JSON Deserialization failed: ");
      Serial.println(err.c_str());
      return;
    }

    // 1. Phân tích lệnh điều khiển (command)
    if (res_doc["data"].containsKey("command")) {
      JsonObject command = res_doc["data"]["command"];
      String action = command["action"].as<String>();
      
      Serial.println("Received Command Action: " + action);
      
      if (action == "charge_now") {
        digitalWrite(RELAY_PIN, HIGH); // BẬT SẠC
        is_charging_enabled = true;
        Serial.println("Command: Start Charging NOW");
      } 
      else if (action == "charge_stop") {
        digitalWrite(RELAY_PIN, LOW);  // TẮT SẠC
        is_charging_enabled = false;
        Serial.println("Command: Stop Charging NOW");
      }
    }

    // 2. Phân tích ngưỡng cấu hình động (config) nếu cần sử dụng offline local
    if (res_doc["data"].containsKey("config")) {
      JsonObject config = res_doc["data"]["config"];
      float max_temp_charger = config["max_temp_charger"].as<float>();
      float max_temp_battery = config["max_temp_battery"].as<float>();
      // ESP32 có thể lưu các biến này để tự kiểm tra an toàn tại chỗ khi mất mạng
    }
  } else {
    Serial.printf("Failed to get config, HTTP code: %d\n", http_code);
  }
}
```

---

## 🛠️ 5. Hướng Dẫn Cài Đặt & Khởi Chạy

### Cách 1: Sử dụng Docker Compose (Khuyên dùng)
Docker Compose tự động thiết lập 3 containers bao gồm: Web server (Flask), Database (MySQL), Cache (Redis) liên kết chặt chẽ với nhau.

1. **Chuẩn bị file môi trường**:
   ```bash
   cp .env.example .env
   ```
   *Điền các thông số API Key Gemini, thông tin SMTP gửi mail OTP, và Secret Key.*

2. **Khởi chạy hệ thống**:
   ```bash
   docker-compose up -d --build
   ```
   * Docker sẽ tự build ảnh container Flask dựa trên `Dockerfile`, kéo image `redis:7-alpine` và `mysql:8.0`.
   * Cấu hình healthcheck trong `docker-compose.yml` đảm bảo Flask chỉ khởi chạy sau khi MySQL và Redis đã sẵn sàng.

3. **Truy cập ứng dụng**:
   Địa chỉ: **http://localhost:8004** (Bên trong container Flask chạy cổng `8888` và được ánh xạ ra cổng `8004` của máy chủ).

---

### Cách 2: Chạy trực tiếp (Không dùng Docker)
Yêu cầu máy chủ đã cài đặt sẵn Python 3.9+, MySQL Server, và Redis Server.

1. **Khởi chạy dịch vụ Redis và MySQL** trên máy chủ và tạo database có tên trùng cấu hình.
2. **Cài đặt thư viện**:
   ```bash
   pip install -r Flask_Web/requirements.txt
   ```
3. **Cấu hình môi trường**:
   Sửa file `.env` chỉ định `DATABASE_URL` kết nối tới MySQL và `REDIS_HOST` chỉ định địa chỉ Redis.
4. **Khởi tạo Database**:
   Chạy script để tạo các bảng dữ liệu:
   ```bash
   python -c "from Flask_Web import db, app; app.app_context().push(); db.create_all()"
   ```
5. **Chạy server**:
   ```bash
   python -m Flask_Web.index
   ```

# ⚡ SmartSac — Tài Liệu Kiến Trúc & Vận Hành Hệ Thống

Tài liệu chi tiết về kiến trúc, cơ sở dữ liệu, các lớp bảo mật, cơ chế đồng bộ realtime và hướng dẫn vận hành hệ thống **SmartSac** (Ổ cắm sạc xe điện thông minh).

---

## 📌 1. Giới thiệu Tổng quan
**SmartSac** là hệ thống giám sát và điều khiển sạc xe điện thông minh. Hệ thống giải quyết các vấn đề an toàn phòng chống cháy nổ và tối ưu hóa thời gian sạc bằng cách:
* Giám sát thông số điện áp ($V$), dòng điện ($A$), công suất ($W$), điện năng tiêu thụ ($kWh$).
* Đo đạc nhiệt độ bộ sạc (cảm biến DS18B20), nhiệt độ vỏ bình/pin (MLX90614 không tiếp xúc), và nhiệt độ/độ ẩm môi trường (DHT).
* Tự động ngắt sạc khi phát hiện quá nhiệt, quá dòng, quá áp, sụt áp mạng lưới, hoặc khi pin đầy dựa trên cấu hình động được phân tích bởi AI (Gemini).
* Điều khiển từ xa qua Dashboard realtime (Web/PWA/Android App) và gửi cảnh báo qua Telegram.

---

## 🏛️ 2. Kiến trúc Hệ thống

Hệ thống hoạt động theo mô hình Hybrid kết hợp **Realtime Cache (Redis)** và **Persistent Database (MySQL)**:

```
                  ┌────────────────────────────────────────┐
                  │              ESP32 Device              │
                  └───────┬────────────────────────▲───────┘
                          │ (3s)                   │ (7s)
            POST Telemetry│                        │GET Config & Commands
                          ▼                        │
         ┌────────────────────────────────────────────────────────┐
         │                  Flask Web Server                      │
         │   (Xác thực Token, Rate Limit, Phân tích Ngưỡng An Toàn)│
         └──────┬──────────────────────┬───────────────────▲──────┘
                │                      │                   │
                ▼                      ▼                   │
       ┌─────────────────┐    ┌─────────────────┐          │
       │   Redis Cache   │    │    MySQL DB     │    ┌─────┴──────┐
       │ (TTL 60s, Queue)│    │ (Users, Configs,│    │ Gemini AI  │
       │  - Telemetry    │    │  Logs, Stats)   │    │ (Analyze & │
       │  - Commands     │    └─────────────────┘    │  Optimize) │
       └────────▲────────┘                           └────────────┘
                │ (Polling / Server-Sent Events)
                │
       ┌────────┴────────┐
       │ Dashboard (WEB) │
       │ (React/PWA/APK) │
       └─────────────────┘
```

1. **ESP32 Firmware**: 
   * Gửi dữ liệu cảm biến (`POST /api/station/device/telemetry`) định kỳ mỗi **3 giây**.
   * Lấy cấu hình và lệnh điều khiển từ server (`GET /api/station/device/config`) định kỳ mỗi **7 giây**.
2. **Flask Web Server**:
   * Kiểm soát request thông qua các bộ lọc Middleware (Rate limiting bằng Redis, kiểm tra an toàn telemetry bằng `verify_telemetry_safety`).
   * Phân phối luồng dữ liệu: telemetry lưu tạm vào **Redis** dạng Key-Value để truy xuất nhanh, đồng thời cập nhật dữ liệu lịch sử/thống kê định kỳ vào **MySQL**.
3. **Cơ chế Realtime**:
   * Dashboard frontend lấy dữ liệu realtime thông qua API hoặc SSE kết nối đến Redis, hạn chế tối đa tải truy vấn trực tiếp vào MySQL.

---

## ⚡ 3. Quy Trình Vận Hành & Cách Hoạt Động

### Luồng Hoạt Động Của Thiết Bị Sạc (ESP32)

```
                       ┌─────────────────────────┐
                       │    Thiết bị khởi động   │
                       └────────────┬────────────┘
                                    │
                                    ▼
                       ┌─────────────────────────┐
                       │ Đọc cấu hình từ API     │◄────────────────┐
                       │ (GET /device/config)    │                 │
                       └────────────┬────────────┘                 │
                                    │                              │
                                    ▼                              │
                       ┌─────────────────────────┐                 │
                       │    Có lệnh điều khiển?  │                 │
                       └────────────┬────────────┘                 │
                                    │                              │
                        Có          │ Không                        │
                   ┌────────────────┴────────────────┐             │
                   ▼                                 ▼             │
        ┌─────────────────────┐           ┌─────────────────────┐  │
        │ Thực thi lệnh       │           │ Chạy theo cấu hình  │  │
        │ (Bật/Tắt Relay sạc) │           │ tự động (Ngắt nếu   │  │
        └──────────┬──────────┘           │ quá nhiệt/đầy pin)  │  │
                   │                      └──────────┬──────────┘  │
                   └────────────────┬────────────────┘             │
                                    │                              │ (Lặp lại)
                                    ▼                              │
                       ┌─────────────────────────┐                 │
                       │ Đọc cảm biến & gửi dữ   │                 │
                       │ liệu lên Web server     │─────────────────┘
                       │ (POST /device/telemetry)│
                       └─────────────────────────┘
```

#### Bước 1: Khởi động và Xác thực
Thiết bị ESP32 lưu Station Token nhận được từ giao diện quản trị trạm sạc. Mỗi request lên Web Server đều kèm theo token này.

#### Bước 2: Chu kỳ Telemetry (3s / lần)
Thiết bị đọc các cảm biến và POST lên `/api/station/device/telemetry`.
* Server kiểm tra token, kiểm tra rate limit, kiểm tra an toàn (`verify_telemetry_safety`).
* Nếu dữ liệu an toàn, server lưu trạng thái mới nhất vào Redis với key `sc:telemetry:<station_id>` (TTL 60 giây).
* Dashboard của người dùng lắng nghe/fetch dữ liệu từ Redis để hiển thị biểu đồ realtime.

#### Bước 3: Chu kỳ Nhận cấu hình & Lệnh điều khiển (7s / lần)
Thiết bị gửi GET request đến `/api/station/device/config`.
* Server trả về các cấu hình bảo vệ (nhiệt độ tối đa, độ ẩm tối đa, công suất tối đa...).
* Server cũng kiểm tra xem trong Redis key `sc:command:<station_id>` có chứa lệnh điều khiển nào từ người dùng gửi qua Dashboard không (như `charge_now`, `charge_stop`). Nếu có, trả lệnh về cho thiết bị thực thi và xóa lệnh trong hàng đợi.

---

## 💻 4. Tài Liệu Kết Nối IoT (ESP32 / Arduino C++)

Mục này hướng dẫn chi tiết cách thiết bị ESP32 cấu hình kết nối, gửi dữ liệu telemetry, nhận cấu hình điều khiển từ máy chủ, xử lý lỗi an toàn phần cứng và đính kèm mã nguồn mẫu hoàn chỉnh bằng C++.

### A. Phương thức Xác thực với Server
ESP32 cần đính kèm Station Token do hệ thống cung cấp vào Header HTTP khi thực hiện mọi yêu cầu API. Hỗ trợ hai phương thức header:
* `Authorization: Bearer <STATION_TOKEN>`
* `X-Station-Token: <STATION_TOKEN>`

### B. Cấu trúc Payload Telemetry (JSON gửi lên)
Được gửi qua phương thức `POST /api/station/device/telemetry`.
```json
{
  "p": 128.5,             // Công suất tiêu thụ hiện tại (Watts)
  "v": 220.3,             // Điện áp lưới điện AC (Volts)
  "i": 0.58,              // Dòng điện tải (Amperes)
  "ds": 36.2,             // Nhiệt độ bộ sạc (Độ C, cảm biến DS18B20)
  "mlx": 38.7,            // Nhiệt độ vỏ bình/pin (Độ C, MLX90614)
  "dht_t": 30.0,          // Nhiệt độ môi trường (Độ C, DHT)
  "dht_h": 64.5,          // Độ ẩm môi trường (Phần trăm, DHT)
  "state": "DANG_SAC",    // Trạng thái sạc: DANG_SAC, SAC_CHO, FULL, DUNG, LOI
  "charge_time_min": 47,  // Thời gian đã sạc tính bằng phút
  "sensor_ds_ok": true,   // Trạng thái cảm biến DS18B20 (true = hoạt động, false = lỗi)
  "sensor_mlx_ok": true,  // Trạng thái cảm biến MLX90614 (true = hoạt động, false = lỗi)
  "sensor_pzem_ok": true  // Trạng thái cảm biến PZEM (true = hoạt động, false = lỗi)
}
```
*Lưu ý:* Khi bất kỳ cảm biến nào bị hỏng hóc hoặc không có dữ liệu, hãy truyền giá trị `"NA"` hoặc `null` thay vì giá trị `0` để tránh kích hoạt tính năng dừng khẩn cấp do sụt áp lưới hoặc mất kiểm soát cảm biến.

### C. Cơ Chế Xử Lý Lỗi Và Ngắt Sạc Khẩn Cấp (Safety Check)
Khi thực hiện POST dữ liệu telemetry hoặc GET cấu hình, ESP32 phải phân tích kỹ phản hồi từ máy chủ:
1. **Trường hợp Telemetry bình thường (HTTP 200 OK):**
   * Server trả về JSON dạng: `{"status": "success", ...}`.
   * Thiết bị tiếp tục chu kỳ sạc bình thường.
2. **Trường hợp Phát hiện Nguy hiểm (HTTP 400 Bad Request):**
   * Server trả về JSON chứa mã lỗi và hành động xử lý:
     ```json
     {
       "status": "error",
       "error_code": "SAFETY_RISK_DETECTED",
       "message": "CẢNH BÁO AN TOÀN: Nhiệt độ pin (66.5°C) vượt ngưỡng...",
       "action": "charge_stop",
       "command": "charge_stop"
     }
     ```
   * Khi phát hiện trường hợp này hoặc nhận mã HTTP lỗi, ESP32 phải **lập tức ngắt relay sạc bằng phần cứng**, chuyển trạng thái thiết bị sang `LOI` hoặc `DUNG`, và hiển thị chuỗi `message` lên màn hình LCD/OLED nếu có.

### D. Mã Nguồn Mẫu Hoàn Chỉnh ESP32 (Arduino IDE C++)

Dưới đây là mã nguồn mẫu tối ưu sử dụng thư viện `WiFi.h`, `HTTPClient.h` và `ArduinoJson.h` (yêu cầu cài đặt thư viện ArduinoJson phiên bản 6 hoặc 7).

```cpp
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>

// 1. Cấu hình mạng WiFi và thông tin kết nối Server
const char* ssid = "YOUR_WIFI_SSID";
const char* password = "YOUR_WIFI_PASSWORD";

const char* server_url_telemetry = "http://YOUR_SERVER_IP:8888/api/station/device/telemetry";
const char* server_url_config    = "http://YOUR_SERVER_IP:8888/api/station/device/config";
const char* station_token        = "SK_YOUR_SECURE_STATION_TOKEN"; // Lấy từ giao diện quản trị

// 2. Định nghĩa chân phần cứng điều khiển Relay sạc
#define RELAY_PIN 23 
bool is_charging_enabled = false;

// 3. Khai báo chu kỳ gửi dữ liệu (milli-giây)
unsigned long last_telemetry_time = 0;
const unsigned long telemetry_interval = 3000; // 3 giây gửi telemetry một lần

unsigned long last_config_time = 0;
const unsigned long config_interval = 7000;    // 7 giây lấy cấu hình & lệnh một lần

void setup() {
  Serial.begin(115200);
  pinMode(RELAY_PIN, OUTPUT);
  digitalWrite(RELAY_PIN, LOW); // Mặc định tắt sạc khi khởi động

  // Kết nối WiFi
  Serial.print("Connecting to WiFi");
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWiFi Connected!");
}

void loop() {
  if (WiFi.status() != WL_CONNECTED) {
    // Nếu mất WiFi, tự động ngắt relay sạc để đảm bảo an toàn ngoại tuyến
    digitalWrite(RELAY_PIN, LOW);
    is_charging_enabled = false;
    Serial.println("WiFi disconnected! Relay turned OFF for safety.");
    delay(1000);
    return;
  }

  unsigned long current_time = millis();

  // Chu kỳ gửi telemetry
  if (current_time - last_telemetry_time >= telemetry_interval) {
    last_telemetry_time = current_time;
    send_telemetry();
  }

  // Chu kỳ lấy cấu hình & nhận lệnh điều khiển
  if (current_time - last_config_time >= config_interval) {
    last_config_time = current_time;
    check_config_and_commands();
  }
}

// Hàm giả lập đọc giá trị cảm biến
float read_pzem_voltage() { return 220.5; }
float read_pzem_current() { return is_charging_enabled ? 4.2 : 0.0; }
float read_pzem_power()   { return is_charging_enabled ? 924.0 : 0.0; }
float read_ds18b20_temp() { return 35.8; }
float read_mlx90614_temp(){ return 38.2; }
float read_dht_temp()     { return 30.5; }
float read_dht_humidity() { return 65.0; }

// Hàm gửi dữ liệu Telemetry lên Web Server
void send_telemetry() {
  HTTPClient http;
  http.begin(server_url_telemetry);
  
  // Thiết lập Headers bắt buộc
  http.addHeader("Content-Type", "application/json");
  http.addHeader("Authorization", "Bearer " + String(station_token));

  // Tạo tài liệu JSON payload
  StaticJsonDocument<512> doc;
  doc["v"] = read_pzem_voltage();
  doc["i"] = read_pzem_current();
  doc["p"] = read_pzem_power();
  doc["ds"] = read_ds18b20_temp();
  doc["mlx"] = read_mlx90614_temp();
  doc["dht_t"] = read_dht_temp();
  doc["dht_h"] = read_dht_humidity();
  doc["state"] = is_charging_enabled ? "DANG_SAC" : "DUNG";
  doc["charge_time_min"] = 15; // Giả lập thời gian sạc
  doc["sensor_ds_ok"] = true;
  doc["sensor_mlx_ok"] = true;
  doc["sensor_pzem_ok"] = true;

  String json_payload;
  serializeJson(doc, json_payload);

  Serial.print("Sending telemetry: ");
  Serial.println(json_payload);

  int http_code = http.POST(json_payload);
  String response_body = http.getString();
  http.end();

  Serial.printf("HTTP Code: %d\n", http_code);
  Serial.println("Response: " + response_body);

  if (http_code == 200 || http_code == 201) {
    Serial.println("Telemetry sent successfully!");
  } 
  else if (http_code == 400 || http_code == 403) {
    // Khi Server trả về lỗi 400/403 kèm cảnh báo an toàn
    StaticJsonDocument<512> res_doc;
    DeserializationError err = deserializeJson(res_doc, response_body);
    if (!err) {
      String action = res_doc["action"].as<String>();
      String message = res_doc["message"].as<String>();
      if (action == "charge_stop") {
        digitalWrite(RELAY_PIN, LOW); // NGẮT SẠC KHẨN CẤP
        is_charging_enabled = false;
        Serial.println("EMERGENCY STOP DETECTED: " + message);
      }
    }
  }
}

// Hàm lấy cấu hình và lệnh điều khiển từ Web Server
void check_config_and_commands() {
  HTTPClient http;
  http.begin(server_url_config);
  
  // Thiết lập Headers bắt buộc
  http.addHeader("Authorization", "Bearer " + String(station_token));

  int http_code = http.GET();
  String response_body = http.getString();
  http.end();

  if (http_code == 200) {
    StaticJsonDocument<1024> res_doc;
    DeserializationError err = deserializeJson(res_doc, response_body);
    if (err) {
      Serial.print("JSON Deserialization failed: ");
      Serial.println(err.c_str());
      return;
    }

    // 1. Phân tích lệnh điều khiển (command)
    if (res_doc["data"].containsKey("command")) {
      JsonObject command = res_doc["data"]["command"];
      String action = command["action"].as<String>();
      
      Serial.println("Received Command Action: " + action);
      
      if (action == "charge_now") {
        digitalWrite(RELAY_PIN, HIGH); // BẬT SẠC
        is_charging_enabled = true;
        Serial.println("Command: Start Charging NOW");
      } 
      else if (action == "charge_stop") {
        digitalWrite(RELAY_PIN, LOW);  // TẮT SẠC
        is_charging_enabled = false;
        Serial.println("Command: Stop Charging NOW");
      }
    }

    // 2. Phân tích ngưỡng cấu hình động (config) nếu cần sử dụng offline local
    if (res_doc["data"].containsKey("config")) {
      JsonObject config = res_doc["data"]["config"];
      float max_temp_charger = config["max_temp_charger"].as<float>();
      float max_temp_battery = config["max_temp_battery"].as<float>();
      // ESP32 có thể lưu các biến này để tự kiểm tra an toàn tại chỗ khi mất mạng
    }
  } else {
    Serial.printf("Failed to get config, HTTP code: %d\n", http_code);
  }
}
```

---

## 🛠️ 5. Hướng Dẫn Cài Đặt & Khởi Chạy

### Cách 1: Sử dụng Docker Compose (Khuyên dùng)
Docker Compose tự động thiết lập 3 containers bao gồm: Web server (Flask), Database (MySQL), Cache (Redis) liên kết chặt chẽ với nhau.

1. **Chuẩn bị file môi trường**:
   ```bash
   cp .env.example .env
   ```
   *Điền các thông số API Key Gemini, thông tin SMTP gửi mail OTP, và Secret Key.*

2. **Khởi chạy hệ thống**:
   ```bash
   docker-compose up -d --build
   ```
   * Docker sẽ tự build ảnh container Flask dựa trên `Dockerfile`, kéo image `redis:7-alpine` và `mysql:8.0`.
   * Cấu hình healthcheck trong `docker-compose.yml` đảm bảo Flask chỉ khởi chạy sau khi MySQL và Redis đã sẵn sàng.

3. **Truy cập ứng dụng**:
   Địa chỉ: **http://localhost:8004** (Bên trong container Flask chạy cổng `8888` và được ánh xạ ra cổng `8004` của máy chủ).

---

### Cách 2: Chạy trực tiếp (Không dùng Docker)
Yêu cầu máy chủ đã cài đặt sẵn Python 3.9+, MySQL Server, và Redis Server.

1. **Khởi chạy dịch vụ Redis và MySQL** trên máy chủ và tạo database có tên trùng cấu hình.
2. **Cài đặt thư viện**:
   ```bash
   pip install -r Flask_Web/requirements.txt
   ```
3. **Cấu hình môi trường**:
   Sửa file `.env` chỉ định `DATABASE_URL` kết nối tới MySQL và `REDIS_HOST` chỉ định địa chỉ Redis.
4. **Khởi tạo Database**:
   Chạy script để tạo các bảng dữ liệu:
   ```bash
   python -c "from Flask_Web import db, app; app.app_context().push(); db.create_all()"
   ```
5. **Chạy server**:
   ```bash
   python -m Flask_Web.index
   ```
   Server mặc định lắng nghe tại cổng `8888`.
