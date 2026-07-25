"""
security.py — Ba lớp bảo mật cho ứng dụng Flask/MySQL/Redis:

  1. Rate Limiting    — giới hạn request theo IP hoặc User qua Redis
  2. Row Level Security — kiểm tra ownership tại tầng ứng dụng (MySQL không có RLS gốc)
  3. IDOR Prevention  — decorator xác thực quyền sở hữu resource trước mọi route

Dùng chung cho tất cả API endpoint trong index.py.
"""

import functools
import secrets
import string

from flask import request, jsonify, g, abort
from flask_login import current_user

from Flask_Web import redis_client
from Flask_Web.models import Station, Config


# ─────────────────────────────────────────────────────────────────────────────
# PHẦN 1 — RATE LIMITING (Redis Fixed-Window Counter)
# ─────────────────────────────────────────────────────────────────────────────
#
# Thuật toán Fixed Window Counter:
#   • Mỗi cặp (scope, identifier) → 1 key Redis với TTL = window_seconds
#   • Mỗi request → INCR key
#   • Nếu count > max_requests → từ chối (429) và trả về Retry-After
#   • Khi Redis không khả dụng → fail-open (không chặn) để tránh downtime
#
# Ví dụ key Redis:  "rl:login:203.0.113.42"
#                   "rl:api_general:user:admin"


class RateLimitExceeded(Exception):
    """Raise nội bộ khi client vượt ngưỡng. Mang thông tin retry_after (giây)."""
    def __init__(self, retry_after: int):
        self.retry_after = retry_after


def get_real_client_ip() -> str:
    """
    Trích xuất Real Client IP qua 2 lớp proxy (Cloudflare → Nginx → Flask).

    Thứ tự ưu tiên:
      1. CF-Connecting-IP  — Cloudflare đặt header này khi bật đám mây cam,
                             luôn chứa IP gốc của người dùng cuối.
      2. X-Forwarded-For   — phần tử đầu tiên trong chuỗi phân tách bởi dấu phẩy
                             (IP gốc, trước khi Cloudflare/Nginx thêm vào).
      3. remote_addr       — fallback khi không đi qua proxy nào (môi trường dev).
    """
    cf_ip = request.headers.get('CF-Connecting-IP', '').strip()
    if cf_ip:
        return cf_ip
    xff = request.headers.get('X-Forwarded-For', '').strip()
    if xff:
        return xff.split(',')[0].strip()
    return request.remote_addr or 'unknown'


# Alias nội bộ — dùng bởi check_rate_limit và các hàm legacy trong module này
def _client_ip() -> str:
    return get_real_client_ip()


def check_rate_limit(scope: str, max_requests: int, window_seconds: int,
                     identifier: str | None = None) -> None:
    """
    Kiểm tra rate limit cho một request cụ thể.

    Args:
        scope:          Tên định danh endpoint, ví dụ 'login', 'api_general'.
        max_requests:   Số request tối đa được phép trong window.
        window_seconds: Độ dài cửa sổ thời gian (giây).
        identifier:     IP hoặc username — mặc định tự lấy IP từ request.

    Raises:
        RateLimitExceeded: khi client vượt giới hạn.
    """
    if identifier is None:
        identifier = get_real_client_ip()

    key = f"rl:{scope}:{identifier}"

    try:
        with redis_client.pipeline() as pipe:
            pipe.incr(key)   # tăng bộ đếm nguyên tử
            pipe.ttl(key)    # đọc TTL còn lại
            count, ttl = pipe.execute()

        if ttl < 0:
            # Key mới (TTL = -1) hoặc đã hết TTL (-2) → đặt lại cửa sổ
            redis_client.expire(key, window_seconds)
            ttl = window_seconds

        if count > max_requests:
            raise RateLimitExceeded(retry_after=max(1, ttl))

    except RateLimitExceeded:
        raise  # luôn truyền lên trên, không bắt ở đây

    except Exception:
        # Redis lỗi / offline → fail-open để không làm tê liệt service
        pass


def rate_limit(scope: str, max_requests: int = 60, window_seconds: int = 60,
               use_user: bool = False):
    """
    Decorator áp dụng rate limit lên một route Flask.

    Args:
        scope:          Tên ngắn gọn, duy nhất cho route này.
        max_requests:   Số request tối đa trong window.
        window_seconds: Độ dài cửa sổ (giây). Mặc định 60s.
        use_user:       True  → dùng username làm identifier (rate limit theo user).
                        False → dùng IP (dùng cho endpoint không yêu cầu đăng nhập).

    Trả về 429 kèm header `Retry-After` và `X-RateLimit-Limit` nếu vượt giới hạn.

    Ví dụ dùng:
        @app.route('/login', methods=['POST'])
        @rate_limit('login', max_requests=10, window_seconds=900)  # 10 lần / 15 phút
        def user_login(): ...

        @app.route('/api/dashboard/command', methods=['POST'])
        @rate_limit('dashboard_cmd', max_requests=100, window_seconds=900, use_user=True)
        def api_dashboard_command(): ...
    """
    def decorator(f):
        @functools.wraps(f)
        def wrapped(*args, **kwargs):
            identifier = None
            if use_user and current_user.is_authenticated:
                # Rate limit theo user — mỗi account riêng biệt, không bị ảnh hưởng bởi IP chung (NAT)
                identifier = f"user:{current_user.username}"

            try:
                check_rate_limit(scope, max_requests, window_seconds, identifier)
            except RateLimitExceeded as exc:
                # Lưu thông tin vào g để @app.errorhandler(429) có thể đọc
                # và chọn định dạng phản hồi phù hợp (JSON vs HTML).
                g.retry_after = exc.retry_after
                g.ratelimit_max = max_requests
                g.ratelimit_window = window_seconds
                abort(429)

            return f(*args, **kwargs)
        return wrapped
    return decorator


# ─────────────────────────────────────────────────────────────────────────────
# PHẦN 2 — ROW LEVEL SECURITY (Application Layer cho MySQL)
# ─────────────────────────────────────────────────────────────────────────────
#
# MySQL không có RLS gốc như PostgreSQL. Thay vào đó, mọi truy vấn phải
# luôn gắn điều kiện `username = <current_user>` — không bao giờ query
# chỉ theo ID rồi mới kiểm tra ownership sau.
#
# Pattern an toàn:    Station.query.filter_by(id=X, username=U).first()
# Pattern nguy hiểm:  Station.query.filter_by(id=X).first()  ← IDOR!


def get_station_owned_by(station_id: int, username: str) -> Station:
    """
    Lấy Station theo ID kết hợp với username — đảm bảo RLS tại tầng ứng dụng.

    Luôn dùng hàm này thay vì query thẳng Station.query.filter_by(id=...).
    Trả về thông báo lỗi giống nhau cho cả "không tồn tại" và "không phải chủ"
    để tránh information leakage (kẻ tấn công biết ID nào tồn tại trên hệ thống).

    Raises:
        PermissionError: nếu station không tồn tại hoặc không thuộc username.
    """
    station = Station.query.filter_by(id=station_id, username=username).first()
    if station is None:
        # Dùng cùng một thông báo lỗi — không tiết lộ sự khác biệt
        raise PermissionError('Không tìm thấy trạm hoặc bạn không có quyền truy cập.')
    return station


def get_config_owned_by(station_id: int, username: str) -> Config:
    """
    Lấy Config theo station_id kết hợp với username.
    Cùng pattern RLS với get_station_owned_by.

    Raises:
        PermissionError: nếu config không tồn tại hoặc station không thuộc username.
    """
    config = Config.query.filter_by(station_id=station_id, username=username).first()
    if config is None:
        raise PermissionError('Không tìm thấy cấu hình hoặc bạn không có quyền truy cập.')
    return config


# ─────────────────────────────────────────────────────────────────────────────
# PHẦN 3 — IDOR PREVENTION (Decorator kiểm tra quyền sở hữu)
# ─────────────────────────────────────────────────────────────────────────────
#
# IDOR (Insecure Direct Object Reference) xảy ra khi:
#   GET /api/config/42  → server trả về config của station 42 dù user không sở hữu
#
# Decorator `require_station_ownership` giải quyết bằng cách:
#   1. Chặn trước khi route function chạy
#   2. Dùng RLS query (filter theo cả ID + username) — không chỉ ID
#   3. Inject `_station` vào kwargs để route không cần query DB lần nữa
#   4. Luôn trả về 403 (không phải 404) để tránh tiết lộ ID nào tồn tại


def require_station_ownership(param_name: str = 'station_id'):
    """
    Decorator xác thực quyền sở hữu station trước khi thực thi route.

    Cách dùng:
        @app.route('/api/config/<int:station_id>', methods=['GET'])
        @require_station_ownership('station_id')
        def api_config_get(station_id, **kwargs):
            station = kwargs['_station']  # đã được verify, không cần query lại
            ...

    Nếu station không tồn tại hoặc không thuộc user hiện tại → 403 Forbidden.
    Không phân biệt "không tồn tại" vs "không phải chủ" để chống enumeration attack.
    """
    def decorator(f):
        @functools.wraps(f)
        def wrapped(*args, **kwargs):
            # Bước 1: Xác thực đăng nhập (phòng trường hợp decorator dùng độc lập)
            if not current_user.is_authenticated:
                return jsonify({'status': 'error', 'message': 'Chưa đăng nhập'}), 401

            # Bước 2: Lấy station_id từ URL params
            station_id = kwargs.get(param_name)
            if station_id is None:
                return jsonify({
                    'status': 'error',
                    'message': f'Thiếu tham số {param_name}',
                }), 400

            # Bước 3: RLS check — truy vấn kết hợp ID + username
            try:
                station = get_station_owned_by(int(station_id), current_user.username)
                # Inject kết quả vào kwargs — route dùng trực tiếp, không query lại
                kwargs['_station'] = station
            except PermissionError as exc:
                # HTTP 403 (không phải 404) để không tiết lộ sự tồn tại của ID
                return jsonify({'status': 'error', 'message': str(exc)}), 403

            return f(*args, **kwargs)
        return wrapped
    return decorator


# ─────────────────────────────────────────────────────────────────────────────
# PHẦN 4 — SECURE TOKEN GENERATION
# ─────────────────────────────────────────────────────────────────────────────
#
# Thay thế random.choices() bằng secrets — module CSPRNG của Python.
# random.choices() dùng Mersenne Twister, có thể bị dự đoán sau ~624 output.
# secrets.choice() / secrets.token_urlsafe() dùng /dev/urandom (Linux) hoặc
# CryptGenRandom (Windows) — không thể dự đoán được.

_NANOID_ALPHABET = string.ascii_letters + string.digits  # 62 ký tự URL-safe


def generate_secure_token() -> str:
    """
    Tạo station token mật mã học an toàn, thay thế `random.choices()` trong utils.py.

    Output: 'SK_' + 32 ký tự base64url → ~192 bits entropy
    Ví dụ: 'SK_aB3mK9xQz7nR2sW1pLvYuN4cQ8wE6gT'
    """
    # secrets.token_urlsafe(24) → 24 bytes ngẫu nhiên → ~32 ký tự base64url
    return 'SK_' + secrets.token_urlsafe(24)


def generate_nanoid(size: int = 21) -> str:
    """
    Tạo ID ngẫu nhiên dạng NanoID (không cần thư viện ngoài).

    Dùng khi cần ID ngắn khó đoán cho URL công khai thay vì integer sequential.
    Size 21 → ~124 bits entropy (tương đương UUID v4).
    Ví dụ output: 'aB3mK9xQz7nR2sW1pLvYu'
    """
    return ''.join(secrets.choice(_NANOID_ALPHABET) for _ in range(size))


def verify_telemetry_safety(station, payload: dict) -> tuple:
    """
    Kiểm tra mức độ an toàn của dữ liệu telemetry gửi lên từ thiết bị.
    Nếu phát hiện rủi ro (vượt ngưỡng cấu hình hoặc ngưỡng vật lý nguy hiểm):
      - Ghi log cảnh báo bảo mật vào database.
      - Gửi lệnh 'charge_stop' xuống hàng đợi Redis của trạm.
      - Trả về (False, lý_do_lỗi).
    Ngược lại, trả về (True, "").
    """
    import json
    import time
    from Flask_Web import db
    from Flask_Web.models import Config, nhatky

    # 1. Trích xuất các tham số đo lường
    def _parse_value(k: str) -> float | None:
        val = payload.get(k)
        if val is None:
            return None
        s_val = str(val).strip().upper()
        if s_val in {'', 'NA', 'N/A', 'NULL', 'NONE'}:
            return None
        try:
            return float(val)
        except (TypeError, ValueError):
            return None

    def _get_bool(k: str, default: bool = True) -> bool:
        val = payload.get(k)
        if val is None or val == '':
            return default
        if isinstance(val, bool):
            return val
        text = str(val).strip().lower()
        if text in {'na', 'n/a'}:
            return default
        return text not in {'0', 'false', 'no', 'off'}

    ds = _parse_value('ds')       # Nhiệt độ sạc
    mlx = _parse_value('mlx')     # Nhiệt độ pin
    
    # Nhiệt độ môi trường: ưu tiên dht_t, env_t, temp_env
    dht_t = _parse_value('dht_t')
    if dht_t is None:
        dht_t = _parse_value('env_t')
    if dht_t is None:
        dht_t = _parse_value('temp_env')

    # Độ ẩm: ưu tiên dht_h, hum
    hum = _parse_value('dht_h')
    if hum is None:
        hum = _parse_value('hum')

    p = _parse_value('p')         # Công suất
    
    # Dòng điện: i hoặc current
    i = _parse_value('i')
    if i is None:
        i = _parse_value('current')

    # Điện áp: v hoặc voltage
    v = _parse_value('v')
    if v is None:
        v = _parse_value('voltage')

    sensor_ds_ok = _get_bool('sensor_ds_ok', True)
    sensor_mlx_ok = _get_bool('sensor_mlx_ok', True)
    sensor_pzem_ok = _get_bool('sensor_pzem_ok', True)

    raw_state = payload.get('state', payload.get('status', ''))
    state = str(raw_state or '').strip().upper()
    is_charging = state in {'DANG_SAC', 'CHARGING', 'SAC', 'DANG_DO', 'PROBING', 'DO'}

    # 2. Lấy cấu hình ngưỡng của trạm từ database
    cfg = Config.query.filter_by(station_id=station.id, username=station.username).first()
    
    # Thiết lập ngưỡng mặc định nếu không có trong DB
    max_ds = float(cfg.max_temp_charger) if cfg and cfg.max_temp_charger is not None else 60.0
    max_mlx = float(cfg.max_temp_battery) if cfg and cfg.max_temp_battery is not None else 50.0
    max_env = float(cfg.max_temp_env) if cfg and cfg.max_temp_env is not None else 50.0
    max_hum = float(cfg.max_humidity) if cfg and cfg.max_humidity is not None else 80.0
    limit_w = float(cfg.limit_input_w) if cfg and cfg.limit_input_w is not None else 1000.0

    errors = []

    # 3. Kiểm tra các ngưỡng cấu hình động (chỉ áp dụng khi đang sạc hoặc đo sạc)
    if is_charging:
        if ds is not None and ds >= max_ds:
            errors.append(f"Nhiệt độ bộ sạc ({ds}°C) vượt ngưỡng an toàn ({max_ds}°C)")
        if mlx is not None and mlx >= max_mlx:
            errors.append(f"Nhiệt độ pin ({mlx}°C) vượt ngưỡng an toàn ({max_mlx}°C)")
        if dht_t is not None and dht_t >= max_env:
            errors.append(f"Nhiệt độ môi trường ({dht_t}°C) vượt ngưỡng an toàn ({max_env}°C)")
        if hum is not None and hum >= max_hum:
            errors.append(f"Độ ẩm môi trường ({hum}%) vượt ngưỡng an toàn ({max_hum}%)")
        if p is not None and p >= limit_w:
            errors.append(f"Công suất sạc ({p}W) vượt giới hạn đầu vào ({limit_w}W)")

        # Lỗi cảm biến phần cứng trong lúc sạc
        if not sensor_ds_ok:
            errors.append("Lỗi cảm biến nhiệt độ bộ sạc (DS18B20)")
        if not sensor_mlx_ok:
            errors.append("Lỗi cảm biến nhiệt độ pin (MLX90614)")
        if not sensor_pzem_ok:
            errors.append("Lỗi cảm biến đo công suất (PZEM)")

    # 4. Kiểm tra các giới hạn vật lý tuyệt đối (bảo vệ phần cứng ngay cả khi ở chế độ chờ/idle)
    if ds is not None and ds > 85.0:
        errors.append(f"Nguy hiểm: Nhiệt độ bộ sạc quá cao ({ds}°C > 85°C)")
    if mlx is not None and mlx > 65.0:
        errors.append(f"Nguy hiểm cực độ: Nhiệt độ pin quá cao ({mlx}°C > 65°C - nguy cơ cháy nổ)")
    if dht_t is not None and dht_t > 60.0:
        errors.append(f"Nguy hiểm: Nhiệt độ môi trường quá cao ({dht_t}°C > 60°C)")
    if i is not None and i > 22.0:
        errors.append(f"Nguy hiểm: Dòng điện quá tải ({i}A > 22A)")
    if v is not None and v > 265.0:
        errors.append(f"Nguy hiểm: Điện áp lưới quá cao ({v}V > 265V)")
    if v is not None and v > 50.0 and v < 150.0: # Chỉ check sụt áp lưới xoay chiều khi có điện lưới > 50V
        errors.append(f"Nguy hiểm: Điện áp lưới sụt sâu ({v}V < 150V)")

    # 5. Xử lý khi phát hiện rủi ro
    if errors:
        error_msg = "; ".join(errors)
        
        # Gửi lệnh Dừng sạc khẩn cấp xuống Redis
        payload_cmd = {
            'action': 'charge_stop',
            'by': 'system_security',
            'ts': int(time.time()),
            'attempt': 1,
        }
        try:
            redis_client.setex(f"sc:command:{station.id}", 300, json.dumps(payload_cmd, ensure_ascii=False))
            
            # Ghi vào lịch sử audit lệnh của trạm
            audit_key = f"sc:command_audit:{station.id}"
            redis_client.lpush(audit_key, json.dumps(payload_cmd, ensure_ascii=False))
            redis_client.ltrim(audit_key, 0, 49)
            redis_client.expire(audit_key, 86400)
        except Exception:
            pass

        # Ghi log nhật ký sự kiện bảo mật
        try:
            nk = nhatky(
                username=station.username,
                noidung=f"[CẢNH BÁO AN TOÀN] Trạm '{station.name}' tự động ngắt: {error_msg}.",
                loai="Security"
            )
            db.session.add(nk)
            db.session.commit()
        except Exception:
            db.session.rollback()

        return False, error_msg

    return True, ""
