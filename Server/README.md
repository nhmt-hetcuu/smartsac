# ⚡ SmartSac — Ổ Cắm Sạc Xe Điện Thông Minh

> 🥇 **Giải Nhất KHKT cấp Tỉnh Đồng Tháp** &nbsp;|&nbsp; 🥉 **Giải Ba KHKT cấp Quốc gia**

Hệ thống giám sát và điều khiển sạc xe điện thông minh. ESP32 gửi dữ liệu cảm biến lên Web Server (Flask + Redis + MySQL) để hiển thị Dashboard realtime, cảnh báo Telegram và phân tích AI.

---

## Cài đặt & Khởi chạy

### 1. Tải mã nguồn

```bash
git clone https://github.com/nguyenminhtri-1234/sacxedienthongminh.git
cd sacxedienthongminh
```

### 2. Cấu hình môi trường

```bash
cp .env.example .env
```

Mở file `.env` và điền các giá trị thực:

| Biến | Mô tả |
|---|---|
| `USE_AI` | `true` bật AI Gemini, `false` tạm tắt (hiển thị thông báo bảo trì) |
| `GEMINI_API_KEY` | API key Google Gemini |
| `SECRET_KEY` | Khóa bí mật Flask (chuỗi ngẫu nhiên dài) |
| `DATABASE_URL` | Chuỗi kết nối MySQL |
| `REDIS_HOST` | Địa chỉ Redis server |
| `EMAIL_USER` / `EMAIL_PASS` | Tài khoản email SMTP (dùng gửi OTP quên mật khẩu) |

### 3. Khởi chạy bằng Docker Compose

```bash
docker-compose up --build
```

Truy cập: **http://localhost:8888**

> Để chạy nền: `docker-compose up -d --build`  
> Xem log: `docker-compose logs -f web`  
> Tắt: `docker-compose down`

### 4. Khởi chạy thủ công (không dùng Docker)

```bash
pip install -r requirements.txt
python -m Flask_Web.index
```

Đảm bảo MySQL và Redis đang chạy trước khi khởi động Flask.

---

## Endpoint API cốt lõi (ESP32 / IoT)

Tài liệu đầy đủ: **[/tailieu/api](https://khkt.hcuu.xyz/tailieu/api)**

Xác thực bằng **Station Token** (lấy từ trang Quản Lý Trạm):

```
Authorization: Bearer SK_YOUR_TOKEN
```

### POST `/api/station/device/telemetry`
Thiết bị gửi dữ liệu cảm biến lên server.

```json
// Request body (JSON)
{
  "p": 150.5,         // Công suất (W)
  "v": 220.1,         // Điện áp (V)
  "i": 0.68,          // Dòng điện (A)
  "ds": 38.5,         // Nhiệt DS18B20 — bộ sạc (°C)
  "mlx": 35.2,        // Nhiệt MLX90614 — vỏ bình (°C)
  "dht_t": 30.0,      // Nhiệt môi trường (°C)
  "dht_h": 65.0,      // Độ ẩm (%)
  "state": "DANG_SAC",
  "charging": true,
  "relay_on": true,
  "energy_session_kwh": 0.012,
  "energy_total_kwh": 1.234
}

// Response
{ "status": "success", "message": "Đã nhận telemetry", "station": 42 }
```

### GET `/api/station/device/config`
Thiết bị lấy cấu hình mới nhất và lệnh điều khiển từ server.

```json
// Response
{
  "status": "success",
  "data": {
    "station_name": "Trạm Sạc 01",
    "config": {
      "wait_time": 60,
      "threshold_w": 10.0,
      "max_temp_charger": 55,
      "max_temp_battery": 55,
      "max_humidity": 85,
      "max_time_h": 10
    },
    "command": { "action": "charge_now" },
    "telegram": { "enabled": true, "token": "...", "chat_id": "..." }
  }
}
```

**Các giá trị `action`:** `charge_now` | `charge_wait` | `charge_stop` | `""` (không có lệnh)

### Mã trạng thái sạc (`state`)

| Mã | Ý nghĩa |
|---|---|
| `DANG_SAC` | Đang sạc |
| `DANG_DO` | Đang đo (probe) |
| `SAC_CHO` | Chờ sạc tự động |
| `FULL` | Pin đầy |
| `NGAT_NHIET` | Ngắt quá nhiệt |
| `NGAT_DO_AM` | Ngắt độ ẩm cao |
| `NGAT_QUA_GIO` | Ngắt quá giờ |
| `LOI_CAM_BIEN` | Lỗi cảm biến |
| `MAT_KET_NOI` | Mất kết nối (>60s) |

---

## Kiến trúc hệ thống

```
ESP32 Firmware
   │  POST telemetry (3s)
   │  GET  config    (7s)
   ▼
Flask Web Server (Python)
   ├── Redis  — lưu telemetry realtime (TTL 60s)
   ├── MySQL  — lưu cấu hình, nhật ký, user
   └── Gemini AI — phân tích cấu hình tối ưu
        ▲
Dashboard Web / PWA / Android App
```

---

## Cấu trúc thư mục

```
.
├── Flask_Web/
│   ├── __init__.py       # Khởi tạo Flask, DB, Redis
│   ├── index.py          # Routes / API endpoints
│   ├── models.py         # SQLAlchemy models
│   ├── utils.py          # Logic nghiệp vụ
│   ├── templates/        # Jinja2 HTML
│   └── static/           # CSS, JS, ảnh, PWA
├── .env                  # Cấu hình bí mật (không commit!)
├── .env.example          # Mẫu cấu hình
├── Dockerfile
├── docker-compose.yml
└── requirements.txt
```

---

## Liên kết

- 🌐 Web: [khkt.hcuu.xyz](https://khkt.hcuu.xyz)
- 📱 APK Android: [Tải về](https://raw.githubusercontent.com/nguyenminhtri-1234/sacxedienthongminh/refs/heads/main/Android_App_Clinet/app.apk)
- 📖 API Docs: [/tailieu/api](https://khkt.hcuu.xyz/tailieu/api)

---

&copy; 2025–2026 SmartSac · Đề tài KHKT học sinh THPT
