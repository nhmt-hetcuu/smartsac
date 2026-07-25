# ⚡ SmartSac — Hệ Thống Trạm Sạc Xe Điện Thông Minh

**SmartSac** là hệ thống giám sát và điều khiển trạm sạc xe điện thông minh, giải quyết các vấn đề an toàn phòng chống cháy nổ và tối ưu hóa chu trình sạc thông qua cảm biến thời gian thực, điều khiển từ xa và phân tích an toàn.

🌐 **Website dự án:** [hetcuu.com/smartsac](https://hetcuu.com/smartsac/)

> 📖 **Tài liệu chi tiết:**
> * [Hướng dẫn chi tiết Mã nguồn ESP32](docs/ESP32.md)
> * [Kiến trúc & Vận hành Hệ thống Server](docs/Server.md)
> * [Tài liệu API Hệ thống](https://hetcuu.com/smartsac/tailieu_api)

---

## 🏛️ Kiến Trúc Hệ Thống

Hệ thống kết hợp thiết bị IoT (ESP32), máy chủ lưu trữ (MySQL), bộ nhớ đệm thời gian thực (Redis) và giao diện điều khiển (Web Dashboard/Telegram Bot).

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

---

## 🌟 Tính Năng Nổi Bật

### 1. Phần Cứng & Firmware (ESP32)
* **Phần cứng kiểm thử**: Code đã được kiểm tra và hoạt động ổn định trên kit **ESP32-S3 DevKit**.
* **Giám sát thời gian thực**: Đo điện áp, dòng điện, công suất (PZEM-004T), nhiệt độ sạc (DS18B20), nhiệt độ cell pin không tiếp xúc (MLX90614) và nhiệt độ/độ ẩm môi trường (DHT).
* **Tự động ngắt khẩn cấp**: Ngắt relay vật lý khi vượt ngưỡng an toàn hoặc nhận lệnh từ server.
* **Cấu hình chân động (Dynamic Pin Routing)**: Thay đổi chân GPIO của cảm biến trực tiếp từ giao diện web settings cục bộ mà không cần nạp lại code.
* **Bảo vệ ngắt cảm biến (Auto-Disable)**: Tự động tắt và lưu cấu hình nếu phát hiện cảm biến lỗi vật lý, chuyển sang chế độ sạc hẹn giờ thuần túy để tránh lỗi vòng lặp.
* **Bảo mật cục bộ & AP Timeout**: Hỗ trợ HTTP Basic Auth trên Web Portal cục bộ. WiFi AP tự động ẩn sau 5 phút để bảo mật và tiết kiệm điện. Cách ly mạng nội bộ (chỉ cho phép cấu hình khi kết nối trực tiếp vào AP của mạch).

### 2. Máy Chủ & Giao Diện (Flask/MySQL/Redis)
* **Realtime Cache**: Sử dụng Redis lưu telemetry và hàng đợi lệnh để tăng tốc hiển thị và giảm tải MySQL.
* **Phân tích an toàn**: Tích hợp AI (Gemini) phân tích tối ưu hóa ngưỡng bảo vệ; tự động gửi lệnh ngắt sạc và khóa trạng thái lỗi (`lockout`) khi phát hiện rủi ro.
* **Telegram Bot**: Xem trạng thái (`/xem`), bật/tắt sạc (`/sac_ngay`, `/dung_sac`), cấu hình ngưỡng an toàn và bật/tắt nóng tính năng.

---

## 📂 Cấu Trúc Dự Án

* **`/ESP32`**: Mã nguồn C++ (Arduino/PlatformIO) cho vi điều khiển ESP32 và các file header cấu hình module.
* **`/Server`**: Mã nguồn máy chủ Flask Web, Dockerfile, cấu hình môi trường và tài liệu tích hợp API.

---

## ⚙️ Hướng Dẫn Cài Đặt

> 💡 **Lưu ý:** Máy chủ (Server) chỉ là tùy chọn mở rộng để lưu trữ dữ liệu lịch sử và giám sát từ xa. Người dùng hoàn toàn **không bắt buộc** phải thiết lập hay chạy Server. Thiết bị ESP32 có khả năng hoạt động độc lập và cho phép người dùng điều khiển, cấu hình đầy đủ bằng 2 cách khác:
> 1. **WiFi cục bộ (Local Web Portal):** Kết nối trực tiếp vào mạng WiFi do ESP32 tự phát ra để truy cập giao diện cấu hình tại địa chỉ `192.168.4.1`.
> 2. **Telegram Bot:** Điều khiển và cấu hình hệ thống từ xa thông qua chat bot Telegram (khi thiết bị kết nối với mạng WiFi có Internet).

### 1. Khởi chạy Server (Tùy chọn)

#### Cách 1: Sử dụng Docker Compose (Khuyên dùng)
Yêu cầu đã cài đặt Docker và Docker Compose.
```bash
cd Server
cp .env.example .env  # Điền thông tin cấu hình (Gemini API, SMTP, DB...)
docker-compose up -d --build
```
Ứng dụng sẽ chạy tại: **http://localhost:8004**

#### Cách 2: Chạy trực tiếp
Yêu cầu Python 3.9+, MySQL và Redis server đang hoạt động.
```bash
cd Server
pip install -r Flask_Web/requirements.txt
# Sửa cấu hình kết nối DB và Redis trong file .env
python -c "from Flask_Web import db, app; app.app_context().push(); db.create_all()"
python -m Flask_Web.index
```

### 2. Nạp code và cấu hình ESP32
1. Sử dụng Arduino IDE hoặc PlatformIO mở thư mục `/ESP32`.
2. Kiểm tra sơ đồ kết nối chân mặc định hoặc sửa đổi trong `PinConfig.h` trước khi nạp.
3. Nạp code vào mạch ESP32.
4. Kết nối vào mạng WiFi AP của mạch:
   * **SSID**: `SmartSac` | **Mật khẩu**: `12345678`
5. Truy cập địa chỉ IP: `192.168.4.1/setting` để cấu hình mạng WiFi Station (mạch kết nối internet), Station Token liên kết server, bot Telegram và các ngưỡng an toàn.
   * **Username**: `admin`
   * **Password**: Mật khẩu WiFi Router đã cấu hình cho mạch (hoặc mặc định `12345678` nếu chưa cấu hình).

---

## 📌 Sơ Đồ Đi Chân Mặc Định (Pin Mapping)

| Thiết Bị / Cảm Biến | Tên Chân / Tín Hiệu | Chân GPIO Mặc Định | Mô tả |
| :--- | :--- | :--- | :--- |
| **DS18B20** | DATA (Nhiệt độ sạc) | `GPIO 5` | Cảm biến đo nhiệt độ cục sạc |
| **MLX90614** | SDA | `GPIO 7` | Giao tiếp I2C cảm biến MLX90614 |
| | SCL | `GPIO 8` | |
| **DHT21** | DATA (Nhiệt/Ẩm mt) | `GPIO 4` | Cảm biến đo nhiệt độ/độ ẩm môi trường |
| **PZEM-004T** | RX | `GPIO 16` | Giao tiếp UART nhận dữ liệu đo điện |
| | TX | `GPIO 17` | Giao tiếp UART truyền dữ liệu đo điện |
| **LCD 2004** | SDA | `GPIO 10` | Giao tiếp I2C màn hình LCD |
| | SCL | `GPIO 11` | |
| **RTC DS3231** | SDA | `GPIO 12` | Giao tiếp I2C đồng hồ thời gian thực |
| | SCL | `GPIO 13` | |
| **Relay** | Control Pin (Sạc) | `GPIO 36` | Điều khiển bật/tắt rơ-le kích hoạt sạc |
| **Button** | BTN3 (Sạc ngay/Dừng) | `GPIO 3` | Nút nhấn cứng đa năng |
| **Reset** | BTN_RESTORE | `GPIO 38` | Nút nhấn khôi phục cài đặt gốc |

---

## 🤖 Danh Sách Lệnh Telegram Bot Cơ Bản

* `/xem` hoặc `/status`: Xem trạng thái hiện tại (Thông số điện, nhiệt độ, độ ẩm).
* `/sac_ngay` | `/dung_sac`: Điều khiển relay sạc lập tức.
* `/wifi [SSID] [MẬT_KHẨU]`: Thiết lập WiFi cho thiết bị từ xa.
* `/dat_t_ds [Độ_C]` | `/dat_t_mlx [Độ_C]`: Thiết lập ngưỡng ngắt sạc khẩn cấp khi quá nhiệt.
* `/bat_ds` | `/tat_ds` | `/bat_cloud`...: Bật/tắt nhanh các cảm biến và tính năng đồng bộ.
* `/khoi_dong_lai`: Reset thiết bị.
