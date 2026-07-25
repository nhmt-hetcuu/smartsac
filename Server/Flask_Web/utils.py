import random, datetime, smtplib, threading, requests, time
from Flask_Web import app , db, redis_client
from Flask_Web.models import User, nhatky, Station, Config, MonthlyChargeStat
from typing import Optional
import json
from email.mime.text import MIMEText
from email.mime.multipart import MIMEMultipart
import bcrypt

GEMINI_API_URL = 'https://generativelanguage.googleapis.com/v1beta/models/gemini-pro:generateContent'

MAIL_RESEND_INTERVAL_MINUTES = 30
TIMEOUT = 2
INPUT_VOLTAGE = 220
MAX_INPUT_CURRENT_A = 20
MAX_INPUT_POWER_W = INPUT_VOLTAGE * MAX_INPUT_CURRENT_A
_MONTHLY_STATS_TABLE_READY = False


def _hash_password(raw_password: str) -> str:
    """Dùng salt riêng cho mỗi mật khẩu để tăng bảo mật và tương thích bcrypt."""
    return bcrypt.hashpw(raw_password.strip().encode('utf-8'), bcrypt.gensalt(rounds=10)).decode().replace("$2b$", "$2y$")


def get_email_config() -> dict:
    return {
        'host': app.config.get('EMAIL_HOST', 'smtp.zoho.com'),
        'port': int(app.config.get('EMAIL_PORT', 587) or 587),
        'user': app.config.get('EMAIL_USER', ''),
        'password': app.config.get('EMAIL_PASS', '')
    }


def get_gemini_api_key() -> str:
    return app.config.get('GEMINI_API_KEY', '')

def add_user(username,password,**kwargs):
    username = username.strip()
    password = _hash_password(password)
    email=kwargs.get('email')
    existing_user = User.query.filter_by(username=username).first()
    existing_email = User.query.filter_by(email=email).first()
    if existing_user or existing_email:
        return False
    user=User(username=username,
              password=password,
              email=kwargs.get('email'))
    nk=nhatky(username=username,
              noidung="Tạo Tài Khoản",
              loai="Server")
    db.session.add(user)
    db.session.add(nk)
    db.session.commit()
    return True

def login_user(username: str, password: str, **kwargs) -> Optional[User]:
    if username and password:
        user = User.query.filter_by(username=username.strip()).first()
        if user and bcrypt.checkpw(password.strip().encode('utf-8'), (user.password).encode('utf-8')):
            return user
    return None
def clear_nk(username:str):
    nhatky.query.filter_by(username=username).delete(synchronize_session=False)
    db.session.commit()
    return True

def get_user_by_id(user_id):
    return User.query.get(user_id)


def nhatky_user(username):
    return nhatky.query.filter_by(username=username)


def _offline_since_key(station_id: int) -> str:
    return f"sc:offline_since:{station_id}"


def _full_log_key(station_id: int) -> str:
    return f"sc:full_logged:{station_id}"


def _command_vi(action: str) -> str:
    return {
        'charge_now': 'Bắt đầu sạc',
        'charge_wait': 'Sạc chờ',
        'charge_stop': 'Dừng sạc',
    }.get(str(action or '').strip(), str(action or '').strip())


def _state_vi(state: str) -> str:
    key = _normalize_station_state(state)
    return {
        'DANG_SAC': 'Đang sạc',
        'SAC_CHO': 'Sạc chờ',
        'DUNG': 'Dừng',
        'FULL': 'Sạc đầy',
        'LOI': 'Lỗi',
        'MAT_KET_NOI': 'Mất kết nối',
        'SAN_SANG': 'Sẵn sàng',
        'DANG_DO': 'Đang đo',
        'LOI_CAM_BIEN': 'Lỗi cảm biến',
        'NGAT_NHIET': 'Ngắt quá nhiệt',
        'NGAT_DO_AM': 'Ngắt độ ẩm',
        'NGAT_QUA_GIO': 'Ngắt quá thời gian',
        'NGAT_QUA_CONG_SUAT': 'Ngắt quá công suất',
    }.get(key, key.replace('_', ' '))


def _station_log_label(username: str, station_id: Optional[int], payload: Optional[dict]) -> str:
    """Ưu tiên tên trạm trong log; fallback về id nếu không có tên."""
    payload = payload or {}

    raw_name = (
        payload.get('station_name')
        or payload.get('name')
        or payload.get('station')
    )

    station_name = str(raw_name or '').strip()
    if not station_name and station_id:
        query = Station.query.filter_by(id=station_id)
        user_key = str(username or '').strip()
        if user_key and user_key not in {'anonymous', 'station_unknown'}:
            query = query.filter_by(username=user_key)
        station = query.first()
        if station and station.name:
            station_name = str(station.name).strip()

    if station_name:
        return f" [Trạm {station_name}]"
    if station_id:
        return f" [Trạm #{station_id}]"
    return ""


def _build_log_message(
    username: str,
    action: str,
    status: str,
    source: str,
    station_id: Optional[int],
    detail: str,
    payload: Optional[dict],
) -> str:
    payload = payload or {}
    ok = str(status or '').upper() == 'SUCCESS'
    sid = _station_log_label(username, station_id, payload)

    if action == 'USER_LOGIN':
        return f"Đăng nhập {'thành công' if ok else 'thất bại'}."
    if action == 'USER_LOGOUT':
        return "Đăng xuất hệ thống."
    if action == 'USER_REGISTER':
        return f"Đăng ký tài khoản {'thành công' if ok else 'thất bại'}."
    if action == 'USER_CHANGE_PASSWORD':
        return f"Đổi mật khẩu {'thành công' if ok else 'thất bại'}."
    if action == 'USER_UPDATE_API_CONFIG':
        return f"Cập nhật cấu hình API {'thành công' if ok else 'thất bại'}."
    if action == 'USER_UPDATE_PROFILE':
        return f"Cập nhật hồ sơ cá nhân {'thành công' if ok else 'thất bại'}."
    if action == 'CONFIG_SAVE':
        return f"Lưu thay đổi cấu hình trạm{sid} {'thành công' if ok else 'thất bại'}."
    if action == 'STATION_ADD':
        return f"Thêm trạm sạc{sid} {'thành công' if ok else 'thất bại'}."
    if action == 'STATION_DELETE':
        return f"Xóa trạm sạc{sid} {'thành công' if ok else 'thất bại'}."
    if action == 'STATION_SET_ACTIVE':
        return f"Chọn trạm hoạt động{sid} {'thành công' if ok else 'thất bại'}."
    if action == 'DASHBOARD_COMMAND':
        cmd = _command_vi(payload.get('command', ''))
        return f"Gửi lệnh '{cmd}'{sid} {'thành công' if ok else 'thất bại'}."
    if action == 'STATION_ONLINE':
        offline_seconds = int(payload.get('offline_seconds', 0) or 0)
        return f"Trạm trở lại online{sid}. Mất kết nối trước đó: {offline_seconds} giây."
    if action == 'STATION_OFFLINE':
        online_seconds = int(payload.get('online_seconds', 0) or 0)
        return f"Trạm chuyển offline{sid}. Đã online liên tục: {online_seconds} giây."
    if action == 'STATION_CHARGE_FULL':
        mins = int(payload.get('charge_time_min', 0) or 0)
        return f"Sạc đầy{sid}. Thời gian sạc: {mins} phút."
    if action == 'STATION_STATE_CHANGE':
        old_state = _state_vi(payload.get('from_state', ''))
        new_state = _state_vi(payload.get('to_state', payload.get('state', '')))
        return f"Trạng thái trạm{sid}: {old_state} -> {new_state}."

    base = f"{action}{sid}: {'thành công' if ok else 'thất bại'}"
    if detail:
        return f"{base} - {detail}"
    return base


def log_nhatky_event(
    username: str,
    action: str,
    loai: str,
    status: str = 'SUCCESS',
    source: str = 'WEB',
    station_id: Optional[int] = None,
    detail: str = '',
    payload: Optional[dict] = None,
):
    """Ghi log dạng thông điệp dễ đọc, nếu lỗi ghi log thì bỏ qua để không làm fail API chính."""
    try:
        # Reduce noisy high-frequency logs (dashboard polling + device heartbeat).
        if status == 'SUCCESS' and action in {
            'DASHBOARD_DATA_GET',
            'DEVICE_TELEMETRY_PUSH',
            'DEVICE_GET_CONFIG',
            'CLEAR_LOG',
        }:
            return

        throttle_seconds = {
            'DEVICE_TELEMETRY_PUSH': 60,
            'DEVICE_GET_CONFIG': 60,
        }.get(action, 0)

        redis_ready = _redis_is_ready()
        if status == 'SUCCESS' and throttle_seconds > 0:
            station_part = station_id if station_id is not None else 'none'
            throttle_key = f"sc:log_throttle:{action}:{source}:{station_part}"
            if redis_ready and redis_client.get(throttle_key):
                return
            if redis_ready:
                redis_client.setex(throttle_key, throttle_seconds, '1')

        message = _build_log_message(username, action, status, source, station_id, detail, payload)
        db.session.add(
            nhatky(
                username=(username or 'anonymous').strip() or 'anonymous',
                noidung=message,
                loai=loai,
            )
        )
        db.session.commit()
    except Exception as ex:
        db.session.rollback()
        app.logger.error(f"Log write error: {ex}")

def send_mail_async(to_email, msg):
    email_config = get_email_config()
    try:
        with smtplib.SMTP(email_config['host'], email_config['port']) as server:
            server.starttls()
            server.login(email_config['user'], email_config['password'])
            server.sendmail(email_config['user'], to_email, msg.as_string())
    except Exception as e:
        app.logger.error(f"Mail send error: {e}")

def mail(email: str) -> str:
    user = User.query.filter_by(email=email).first()
    if not user:
        return "Tài khoản không tồn tại!"
    password = str(random.randint(100000, 999999))
    user.password = _hash_password(password)
    db.session.commit()
    email_config = get_email_config()
    msg = MIMEMultipart()
    msg['From'] = email_config['user']
    msg['To'] = user.email
    msg['Subject'] = "Gửi lại mật khẩu"
    body = f"Mật khẩu của bạn là: {password}"
    msg.attach(MIMEText(body, 'plain'))
    t = threading.Thread(target=send_mail_async, args=(user.email, msg))
    t.daemon = True   # không giữ process sống, không tích lũy reference
    t.start()

    return "✅ Gửi email thành công!"



def update_user_api_config(username: str, tg_token: str, tg_chatid: str):
    """Cập nhật cấu hình API của user"""
    user = User.query.filter_by(username=username).first()
    if not user:
        raise Exception("Người dùng không tồn tại!")

    user.api_telegram = str(tg_token or '').strip()
    user.id_client_telegram = str(tg_chatid or '').strip()
    nk = nhatky(username=username, noidung="Cập nhật cấu hình API", loai="API Config")
    db.session.add(nk)
    db.session.commit()

    return True

def change_user_password(username: str, old_pass: str, new_pass: str, confirm_pass: str) -> dict:
    """Đổi mật khẩu user"""
    user = User.query.filter_by(username=username).first()
    if not user:
        return {'status': 'error', 'message': 'Người dùng không tồn tại!'}

    # Validate new password format FIRST (before checking old password)
    # This prevents timing attacks and improves validation order
    new_pass_stripped = new_pass.strip()
    if len(new_pass_stripped) < 6 or len(new_pass_stripped) > 32:
        return {'status': 'error', 'message': 'Mật khẩu phải từ 6-32 ký tự!'}

    # Check password confirmation
    if new_pass_stripped != confirm_pass.strip():
        return {'status': 'error', 'message': 'Mật khẩu xác nhận không khớp!'}

    # NOW check old password (after all new password validations pass)
    if not bcrypt.checkpw(old_pass.strip().encode('utf-8'), user.password.encode('utf-8')):
        return {'status': 'error', 'message': 'Mật khẩu cũ không chính xác!'}

    # Cập nhật mật khẩu
    new_password_hashed = _hash_password(new_pass)
    user.password = new_password_hashed

    nk = nhatky(username=username, noidung="Thay đổi mật khẩu", loai="Security")
    db.session.add(nk)
    db.session.commit()

    return {'status': 'success', 'message': 'Đổi mật khẩu thành công!'}

# ========== STATION MANAGEMENT ==========

def generate_station_token() -> str:
    """Tạo token cho trạm sạc dùng CSPRNG — không dùng random.choices() vì có thể bị đoán."""
    import secrets as _secrets
    return 'SK_' + _secrets.token_urlsafe(24)  # ~192 bits entropy

def get_stations(username: str) -> list:
    """Lấy danh sách trạm sạc của user"""
    stations = Station.query.filter_by(username=username).all()
    return [{
        'id': s.id,
        'name': s.name,
        'token': s.token,
        'status': s.status,
        'is_active': s.is_active
    } for s in stations]

def add_station(username: str, name: str) -> dict:
    """Thêm trạm sạc mới"""
    if not name.strip():
        return {'status': 'error', 'message': 'Tên trạm không được trống!'}

    token = generate_station_token()
    station = Station(
        username=username,
        name=name.strip(),
        token=token,
        status='offline',
        is_active=False
    )

    db.session.add(station)
    nk = nhatky(
        username=username,
        noidung=f"Thêm trạm sạc: {name}",
        loai="Station"
    )
    db.session.add(nk)
    db.session.commit()

    return {
        'status': 'success',
        'message': 'Thêm trạm sạc thành công!',
        'station': {
            'id': station.id,
            'name': station.name,
            'token': station.token,
            'status': station.status,
            'is_active': station.is_active
        }
    }

def delete_station(username: str, station_id: int) -> dict:
    """Xóa trạm sạc"""
    station = Station.query.filter_by(id=station_id, username=username).first()
    if not station:
        return {'status': 'error', 'message': 'Trạm sạc không tồn tại!'}

    station_name = station.name
    db.session.delete(station)
    nk = nhatky(
        username=username,
        noidung=f"Xóa trạm sạc: {station_name}",
        loai="Station"
    )
    db.session.add(nk)
    db.session.commit()

    return {'status': 'success', 'message': 'Xóa trạm sạc thành công!'}

def set_active_station(username: str, station_id: int) -> dict:
    """Chọn trạm sạc hoạt động"""
    station = Station.query.filter_by(id=station_id, username=username).first()
    if not station:
        return {'status': 'error', 'message': 'Trạm sạc không tồn tại!'}

    # Hủy active các trạm khác sau khi xác thực station tồn tại.
    Station.query.filter(
        Station.username == username,
        Station.is_active == True,
        Station.id != station.id,
    ).update({'is_active': False}, synchronize_session=False)

    station.is_active = True
    nk = nhatky(
        username=username,
        noidung=f"Chọn trạm sạc: {station.name}",
        loai="Station"
    )
    db.session.add(nk)
    db.session.commit()

    return {'status': 'success', 'message': f'Đã chọn trạm: {station.name}'}

def get_active_station(username: str):
    """Lấy trạm sạc đang hoạt động"""
    return Station.query.filter_by(username=username, is_active=True).first()


def _normalize_battery_type(raw_type: str) -> str:
    battery_type = str(raw_type or '').strip().lower()
    if battery_type in {'lifepo4', 'lfp'}:
        return 'lfp'
    if battery_type in {'li-ion', 'lion', 'liion'}:
        return 'liion'
    if battery_type == 'lead':
        return 'lead'
    return 'lead'


def _estimate_max_charge_hours(capacity_ah, output_a, battery_type: str, reserve_soc_percent: int = 20) -> int:
    """Ước lượng thời gian sạc đến (100 - reserve_soc_percent)% SOC (mặc định chỉ sạc đến 80% để bảo vệ pin)."""
    try:
        ah = float(capacity_ah or 0)
    except Exception:
        ah = 0.0

    try:
        out_a = float(output_a or 0)
    except Exception:
        out_a = 0.0

    if ah <= 0 or out_a <= 0:
        return 8

    reserve_ratio = max(0.0, min(0.95, float(reserve_soc_percent) / 100.0))
    usable_ah = ah * (1.0 - reserve_ratio)
    if usable_ah <= 0:
        usable_ah = ah

    bt = _normalize_battery_type(battery_type)
    efficiency_factor = {
        'lead': 1.30,
        'liion': 1.15,
        'lfp': 1.10,
    }.get(bt, 1.25)

    hours = (usable_ah / out_a) * efficiency_factor
    return int(max(1, min(24, round(hours))))


def _compute_est_minutes(charge_time_min, max_charge_h) -> int:
    try:
        elapsed = int(float(charge_time_min or 0))
    except Exception:
        elapsed = 0
    try:
        max_h = int(float(max_charge_h or 0))
    except Exception:
        max_h = 0

    if max_h <= 0:
        return 0
    return max(0, (max_h * 60) - max(0, elapsed))

# ========== CONFIG MANAGEMENT ==========

def get_station_config(username: str, station_id: int) -> dict:
    """Lấy cấu hình của trạm sạc. Tự động lưu config mặc định vào DB nếu chưa tồn tại."""
    config = Config.query.filter_by(username=username, station_id=station_id).first()
    if not config:
        default_battery_type = 'lead'
        default_capacity_ah = 20
        default_output_a = 3.0
        default_max_time_h = _estimate_max_charge_hours(default_capacity_ah, default_output_a, default_battery_type)
        
        # Auto-save default config to DB for consistency
        try:
            config = Config(
                username=username,
                station_id=station_id,
                battery_type=default_battery_type,
                voltage=48,
                capacity_ah=default_capacity_ah,
                output_v=60,
                output_a=default_output_a,
                max_time_h=default_max_time_h,
                threshold_w=10.0,
                measure_interval=300,
                wait_time=60,
                limit_input_w=1000,
                max_temp_charger=50,
                max_temp_battery=55,
                max_temp_env=50,
                max_humidity=80,
            )
            db.session.add(config)
            db.session.commit()
        except Exception as ex:
            db.session.rollback()
            app.logger.warning(f"Failed to auto-save default config for station {station_id}: {ex}")
            # Return defaults anyway even if save failed
            return {
                'battery_type': default_battery_type,
                'voltage': 48,
                'capacity_ah': default_capacity_ah,
                'output_v': 60,
                'output_a': default_output_a,
                'max_time_h': default_max_time_h,
                'threshold_w': 10.0,
                'measure_interval': 300,
                'wait_time': 60,
                'limit_input_w': 1000,
                'max_temp_charger': 50,
                'max_temp_battery': 55,
                'max_temp_env': 50,
                'max_humidity': 80,
                'voltage_input': 220,
            }

    battery_type = _normalize_battery_type(config.battery_type)
    capacity_ah = int(config.capacity_ah or 20)
    output_a = float(config.output_a or 3.0)
    max_time_h = int(config.max_time_h) if config.max_time_h else _estimate_max_charge_hours(capacity_ah, output_a, battery_type)

    return {
        'battery_type': battery_type,
        'voltage': config.voltage,
        'capacity_ah': capacity_ah,
        'output_v': config.output_v,
        'output_a': output_a,
        'max_time_h': max_time_h,
        'threshold_w': config.threshold_w,
        'measure_interval': config.measure_interval,
        'wait_time': config.wait_time,
        'limit_input_w': config.limit_input_w,
        'max_temp_charger': config.max_temp_charger,
        'max_temp_battery': config.max_temp_battery,
        'max_temp_env': config.max_temp_env,
        'max_humidity': config.max_humidity,
        'voltage_input': 220,
    }

def analyze_config_with_ai(
    username: str,
    station_id: int,
    battery_type: str = '',
    ah: str = '',
    out_a: str = ''
) -> dict:
    """Phân tích cấu hình bằng AI, kết hợp dữ liệu DB + telemetry realtime của trạm."""
    try:
        station = Station.query.filter_by(id=int(station_id), username=username).first()
        if not station:
            raise Exception('Trạm sạc không tồn tại')

        cfg = get_station_config(username, station.id)

        telemetry = get_station_telemetry(username, station.id)
        telemetry_source = 'station_realtime'
        if telemetry.get('state') == 'MAT_KET_NOI':
            latest = get_latest_station_telemetry(username)
            if latest and int(latest.get('station_id', 0) or 0) == int(station.id):
                telemetry = latest
            else:
                telemetry_source = 'fallback_default'

        ts_val = int(telemetry.get('ts', 0) or 0)
        telemetry_age_seconds = (int(time.time()) - ts_val) if ts_val else None
        stale_timeout = _telemetry_timeout_for_state(telemetry.get('state', ''))
        telemetry_stale = (
            ts_val == 0
            or telemetry.get('state') == 'MAT_KET_NOI'
            or (telemetry_age_seconds is not None and telemetry_age_seconds > stale_timeout)
        )

        # Ưu tiên override từ form, thiếu thì lấy từ DB.
        battery_type_val = str((battery_type or cfg.get('battery_type') or 'lead')).strip().lower()
        if battery_type_val not in {'lead', 'liion', 'lifepo4', 'lfp'}:
            battery_type_val = 'lead'

        def _to_float(v, default):
            try:
                if v is None or str(v).strip() == '':
                    return float(default)
                return float(v)
            except Exception:
                return float(default)

        ah_val = _to_float(ah, cfg.get('capacity_ah', 20) or 20)
        out_a_val = _to_float(out_a, cfg.get('output_a', 3.0) or 3.0)

        # Telemetry context
        p_val = _to_float(telemetry.get('p', 0), 0)
        ds_val = _to_float(telemetry.get('ds', 0), 0)
        mlx_val = _to_float(telemetry.get('mlx', 0), 0)
        env_t_val = _to_float(telemetry.get('dht_t', telemetry.get('env_t', cfg.get('max_temp_env', 50) or 50)), cfg.get('max_temp_env', 50) or 50)
        hum_val = _to_float(telemetry.get('dht_h', cfg.get('max_humidity', 80) or 80), cfg.get('max_humidity', 80) or 80)
        state_val = str(telemetry.get('state', 'MAT_KET_NOI'))

        # Tính sẵn giờ sạc đến 80% SOC để làm tham chiếu cho AI
        hours_80pct = _estimate_max_charge_hours(ah_val, out_a_val, battery_type_val, reserve_soc_percent=20)

        gemini_api_key = get_gemini_api_key()

        # Nếu có API key, gọi Gemini với context DB + telemetry
        if gemini_api_key:
            prompt = f"""Bạn là kỹ sư cấu hình sạc. Mục tiêu: CHỈ SẠC ĐẾN 80% SOC để bảo vệ tuổi thọ pin, KHÔNG sạc 100%.
Dữ liệu trạm:
- Tên trạm: {station.name}
- Loại pin: {battery_type_val}
- Dung lượng pin: {ah_val} Ah
- Dòng sạc output hiện tại: {out_a_val} A
- Thời gian tính toán để sạc đến 80% SOC: {hours_80pct} giờ (đây là tham chiếu, hãy bám sát con số này)

Cấu hình DB hiện tại:
- max_time_h: {cfg.get('max_time_h')}
- threshold_w: {cfg.get('threshold_w')}
- max_temp_battery: {cfg.get('max_temp_battery')}
- max_temp_env: {cfg.get('max_temp_env')}
- max_humidity: {cfg.get('max_humidity')}

Telemetry realtime:
- state: {state_val}
- p: {p_val} W
- ds: {ds_val} C
- mlx: {mlx_val} C
- dht_t: {env_t_val} C
- dht_h: {hum_val} %

Hãy đề xuất thông số an toàn và thực tế:
1) Thời gian sạc tối ưu (hours, 1-12) — CHỈ SẠC ĐẾN 80% SOC, bám sát tham chiếu {hours_80pct}h, giảm thêm nếu nhiệt độ cao.
2) Ngưỡng đầy công suất (watts, 5-20)
3) Nhiệt tối đa pin (temperature, 40-60)
4) Nhiệt môi trường khuyến nghị (temp_env, 35-40)
5) Độ ẩm tối đa khuyến nghị (humidity, 80-90)
Trả về duy nhất JSON:
{{"hours": số, "watts": số, "temperature": số, "temp_env": số, "humidity": số}}"""

            headers = {'Content-Type': 'application/json'}
            payload = {
                'contents': [{
                    'parts': [{'text': prompt}]
                }]
            }

            try:
                response = requests.post(
                    f"{GEMINI_API_URL}?key={gemini_api_key}",
                    json=payload,
                    headers=headers,
                    timeout=10
                )

                if response.status_code == 200:
                    data = response.json()
                    try:
                        result_text = data['candidates'][0]['content']['parts'][0]['text']
                        import re
                        json_match = re.search(r'\{.*?}', result_text, re.DOTALL)
                        if json_match:
                            result = json.loads(json_match.group())

                            def _clamp(v, lo, hi, default):
                                try:
                                    val = float(v)
                                except Exception:
                                    val = float(default)
                                return int(max(lo, min(hi, round(val))))

                            return {
                                'hours': _clamp(result.get('hours', hours_80pct), 1, hours_80pct + 1, hours_80pct),
                                'watts': _clamp(result.get('watts', 10), 5, 20, 10),
                                'temperature': _clamp(result.get('temperature', 50), 40, 60, 50),
                                'temp_env': _clamp(result.get('temp_env', env_t_val), 35, 40, 38),
                                'humidity': _clamp(result.get('humidity', hum_val), 80, 90, 80),
                                'context': {
                                    'station_id': station.id,
                                    'station_name': station.name,
                                    'telemetry_source': telemetry_source,
                                    'state': state_val,
                                    'telemetry_age_seconds': telemetry_age_seconds,
                                    'telemetry_stale': telemetry_stale,
                                }
                            }
                    except (KeyError, IndexError, json.JSONDecodeError, ValueError) as e:
                        app.logger.warning(f"Gemini AI response parse error for station {station.id}: {e}")
                        # Fall through to rule-based fallback
                else:
                    app.logger.warning(f"Gemini API returned status {response.status_code} for station {station.id}")
                    # Fall through to rule-based fallback
            except requests.Timeout:
                app.logger.warning(f"Gemini API timeout for station {station.id}")
                # Fall through to rule-based fallback
            except requests.RequestException as e:
                app.logger.warning(f"Gemini API request failed for station {station.id}: {e}")
                # Fall through to rule-based fallback
            except Exception as e:
                app.logger.error(f"Unexpected error calling Gemini API for station {station.id}: {e}")
                # Fall through to rule-based fallback

        # Fallback rule-based nếu không có Gemini hoặc parse lỗi
        hours = hours_80pct  # đã tính sẵn: chỉ sạc đến 80% SOC
        watts_base = 12 if battery_type_val == 'lead' else 8
        watts = min(max(int(watts_base), 5), 20)
        temp = min(max(int(max(50, mlx_val if mlx_val > 0 else 50)), 40), 60)

        return {
            'hours': hours,
            'watts': watts,
            'temperature': temp,
            'temp_env': int(min(max(env_t_val if env_t_val > 0 else 38, 35), 40)),
            'humidity': int(min(max(hum_val if hum_val > 0 else 80, 80), 90)),
            'context': {
                'station_id': station.id,
                'station_name': station.name,
                'telemetry_source': telemetry_source,
                'state': state_val,
                'telemetry_age_seconds': telemetry_age_seconds,
                'telemetry_stale': telemetry_stale,
            }
        }
    except Exception as e:
        raise Exception(f"Lỗi phân tích: {str(e)}")


def analyze_dashboard_status_with_ai(username: str, station_id: int) -> dict:
    """AI trợ lý dashboard: nhận xét tình trạng hiện tại từ DB + telemetry trạm."""
    try:
        station = Station.query.filter_by(id=int(station_id), username=username).first()
        if not station:
            raise Exception('Trạm sạc không tồn tại')

        cfg = get_station_config(username, station.id)
        telemetry = get_station_telemetry(username, station.id)
        telemetry_source = 'station_realtime'
        if telemetry.get('state') == 'MAT_KET_NOI':
            latest = get_latest_station_telemetry(username)
            if latest and int(latest.get('station_id', 0) or 0) == int(station.id):
                telemetry = latest
            else:
                telemetry_source = 'fallback_default'

        ts_val = int(telemetry.get('ts', 0) or 0)
        telemetry_age_seconds = (int(time.time()) - ts_val) if ts_val else None
        stale_timeout = _telemetry_timeout_for_state(telemetry.get('state', ''))
        telemetry_stale = (
            ts_val == 0
            or telemetry.get('state') == 'MAT_KET_NOI'
            or (telemetry_age_seconds is not None and telemetry_age_seconds > stale_timeout)
        )

        state_val = str(telemetry.get('state', 'MAT_KET_NOI'))
        p_val = float(telemetry.get('p', 0) or 0)
        ds_val = float(telemetry.get('ds', 0) or 0)
        mlx_val = float(telemetry.get('mlx', 0) or 0)
        env_t_val = float(telemetry.get('dht_t', telemetry.get('env_t', 0)) or 0)
        hum_val = float(telemetry.get('dht_h', 0) or 0)

        max_ds = float(cfg.get('max_temp_charger') or 60)
        max_mlx = float(cfg.get('max_temp_battery') or 50)
        max_env = float(cfg.get('max_temp_env') or 50)
        max_h = float(cfg.get('max_humidity') or 80)

        level = 'info'
        if telemetry_stale or state_val == 'MAT_KET_NOI':
            level = 'warning'
        if ds_val >= max_ds or mlx_val >= max_mlx or env_t_val >= max_env or hum_val >= max_h:
            level = 'danger'

        gemini_api_key = get_gemini_api_key()
        if gemini_api_key:
            prompt = f"""Bạn là trợ lý vận hành trạm sạc. Hãy nhận xét ngắn gọn tình trạng hiện tại.

Trạm: {station.name}
State: {state_val}
Telemetry: p={p_val}W, ds={ds_val}C, mlx={mlx_val}C, env={env_t_val}C, hum={hum_val}%
Ngưỡng: ds<={max_ds}, mlx<={max_mlx}, env<={max_env}, hum<={max_h}

Trả về duy nhất JSON:
{{"title":"...","insight":"...","level":"info|warning|danger"}}

Yêu cầu:
- Insight tiếng Việt có dấu, 1-2 câu, tập trung tình trạng hiện tại.
- Nếu nguy cơ cao thì nêu rõ rủi ro chính.
"""
            response = requests.post(
                f"{GEMINI_API_URL}?key={gemini_api_key}",
                json={'contents': [{'parts': [{'text': prompt}]}]},
                headers={'Content-Type': 'application/json'},
                timeout=10
            )
            if response.status_code == 200:
                try:
                    import re
                    raw = response.json()['candidates'][0]['content']['parts'][0]['text']
                    m = re.search(r'\{.*?}', raw, re.DOTALL)
                    if m:
                        parsed = json.loads(m.group())
                        parsed_level = str(parsed.get('level', level)).lower()
                        if parsed_level not in {'info', 'warning', 'danger'}:
                            parsed_level = level
                        return {
                            'title': str(parsed.get('title', 'Đánh giá nhanh')).strip() or 'Đánh giá nhanh',
                            'insight': str(parsed.get('insight', '')).strip() or 'Hệ thống đang hoạt động bình thường.',
                            'level': parsed_level,
                            'context': {
                                'station_id': station.id,
                                'station_name': station.name,
                                'state': state_val,
                                'telemetry_source': telemetry_source,
                                'telemetry_age_seconds': telemetry_age_seconds,
                                'telemetry_stale': telemetry_stale,
                            }
                        }
                except Exception as ex:
                    app.logger.error(f"Dashboard AI parse error: {ex}")

        # Rule-based fallback
        if telemetry_stale or state_val == 'MAT_KET_NOI':
            insight = 'Dữ liệu telemetry đã cũ hoặc trạm đang mất kết nối. Vui lòng kiểm tra WiFi, token và nguồn thiết bị.'
        elif level == 'danger':
            insight = 'Có thông số vượt ngưỡng an toàn. Nên giảm tải hoặc dừng sạc để kiểm tra hệ thống.'
        elif state_val in {'SAC_CHO', 'DUNG'}:
            insight = 'Trạm đang ở chế độ chờ/dừng, chưa sạc thực tế.'
        else:
            insight = 'Trạm đang hoạt động ổn định theo dữ liệu hiện tại.'

        return {
            'title': 'Đánh giá tình trạng trạm',
            'insight': insight,
            'level': level,
            'context': {
                'station_id': station.id,
                'station_name': station.name,
                'state': state_val,
                'telemetry_source': telemetry_source,
                'telemetry_age_seconds': telemetry_age_seconds,
                'telemetry_stale': telemetry_stale,
            }
        }
    except Exception as e:
        raise Exception(f"Lỗi AI dashboard: {str(e)}")

def save_station_config(username: str, station_id: str, config_data: dict) -> str:
    """Lưu cấu hình cho trạm sạc"""
    try:
        station_id_int = int(station_id) if station_id else None
        if not station_id_int:
            return "Station ID không hợp lệ"

        # Kiểm tra trạm có thuộc user này không
        station = Station.query.filter_by(id=station_id_int, username=username).first()
        if not station:
            raise Exception("Trạm sạc không tồn tại")

        # Tìm hoặc tạo config
        config = Config.query.filter_by(station_id=station_id_int, username=username).first()
        if not config:
            config = Config(username=username, station_id=station_id_int)

        # Cập nhật dữ liệu
        config.battery_type = _normalize_battery_type(config_data.get('battery_type', 'lead'))
        config.voltage = int(config_data.get('voltage', 48))
        config.capacity_ah = int(config_data.get('ah', 20))
        config.output_v = int(config_data.get('output_v', 60))
        config.output_a = float(config_data.get('output_a', 3.0))

        max_time_raw = config_data.get('max_time_h')
        if str(max_time_raw or '').strip():
            config.max_time_h = int(max(1, min(24, int(float(max_time_raw)))))
        else:
            config.max_time_h = _estimate_max_charge_hours(
                config.capacity_ah,
                config.output_a,
                config.battery_type,
            )

        config.threshold_w = float(config_data.get('threshold_w', 0)) if config_data.get('threshold_w') else None
        config.measure_interval = int(config_data.get('measure_interval', 300))
        config.wait_time = int(config_data.get('wait_time', 60))
        limit_input_raw = config_data.get('limit_input_w', 1000)
        if str(limit_input_raw or '').strip():
            limit_input_w = int(float(limit_input_raw))
        else:
            limit_input_w = 1000
        config.limit_input_w = int(max(0, min(limit_input_w, MAX_INPUT_POWER_W)))
        config.max_temp_charger = int(config_data.get('max_temp_charger', 60)) if config_data.get('max_temp_charger') else None
        config.max_temp_battery = int(config_data.get('max_temp_battery', 50)) if config_data.get('max_temp_battery') else None
        config.max_temp_env = int(config_data.get('max_temp_env', 50))
        config.max_humidity = int(config_data.get('max_humidity', 80))
        config.date_update = datetime.datetime.now()

        db.session.add(config)
        nk = nhatky(
            username=username,
            noidung=f"Lưu cấu hình trạm: {station.name}",
            loai="Config"
        )
        db.session.add(nk)
        db.session.commit()

        return "Lưu cấu hình thành công!"
    except Exception as e:
        raise Exception(f"Lỗi lưu cấu hình: {str(e)}")



def _default_station_telemetry(station_id: int, station_name: str, ts: int = 0, **overrides) -> dict:
    data = {
        'station_id': station_id,
        'station_name': station_name,
        'p': 0,
        'v': 0,
        'i': 0,
        'ds': 0,
        'mlx': 0,
        'dht_t': 0,
        'env_t': 0,
        'dht_h': 0,
        'hum': 0,
        'state': 'MAT_KET_NOI',
        'state_text': '',
        'charge_time_min': 0,
        'est': '--',
        'relay_on': False,
        'charging': False,
        'auto_enabled': False,
        'lockout': False,
        'probe': False,
        'wifi_rssi': 0,
        'sensor_ds_ok': False,
        'sensor_mlx_ok': False,
        'sensor_pzem_ok': False,
        'wait_min': 0,
        'measure_sec': 0,
        'full_w': 0,
        'p_max': 0,
        'max_charge_h': 0,
        'next_start_sec': 0,
        'full_hold_sec': 0,
        'energy_session_kwh': 0,
        'energy_last_full_kwh': 0,
        'energy_total_kwh': 0,
        'monthly_energy_kwh': 0,
        'monthly_avg_power_w': 0,
        'ts': int(ts or 0),
    }
    if overrides:
        data.update(overrides)
    return data

# ========== REDIS TELEMETRY / DEVICE API ==========

TELEMETRY_TIMEOUT_SECONDS = 60
TELEMETRY_IDLE_TIMEOUT_SECONDS = 180


def _telemetry_timeout_for_state(raw_state: str) -> int:
    state = _normalize_station_state(raw_state)
    if state in {'SAN_SANG', 'SAC_CHO', 'DUNG'}:
        return TELEMETRY_IDLE_TIMEOUT_SECONDS
    return TELEMETRY_TIMEOUT_SECONDS

def _redis_is_ready() -> bool:
    try:
        return redis_client.ping()
    except Exception:
        return False


def _telemetry_key(station_id: int) -> str:
    return f"sc:telemetry:{station_id}"


def _command_key(station_id: int) -> str:
    return f"sc:command:{station_id}"


def _ensure_monthly_stats_table() -> bool:
    global _MONTHLY_STATS_TABLE_READY
    if _MONTHLY_STATS_TABLE_READY:
        return True
    try:
        MonthlyChargeStat.__table__.create(bind=db.engine, checkfirst=True)
        _MONTHLY_STATS_TABLE_READY = True
        return True
    except Exception as ex:
        app.logger.warning(f"MonthlyChargeStat table check/create failed: {ex}")
        return False


def _get_station_monthly_stat(station_id: int, now_dt: Optional[datetime.datetime] = None) -> Optional[MonthlyChargeStat]:
    if not _ensure_monthly_stats_table():
        return None
    now_dt = now_dt or datetime.datetime.now()
    return MonthlyChargeStat.query.filter_by(
        station_id=station_id,
        year=int(now_dt.year),
        month=int(now_dt.month),
    ).first()


def _update_station_monthly_stat(station_id: int, telemetry: dict, prev_payload: Optional[dict]) -> dict:
    now_dt = datetime.datetime.now()
    default = {'monthly_energy_kwh': 0.0, 'monthly_avg_power_w': 0.0}
    if not _ensure_monthly_stats_table():
        return default

    row = _get_station_monthly_stat(station_id, now_dt=now_dt)
    if not row:
        row = MonthlyChargeStat(
            station_id=station_id,
            year=int(now_dt.year),
            month=int(now_dt.month),
            energy_kwh=0.0,
            power_sum_w=0.0,
            sample_count=0,
        )

    prev_payload = prev_payload or {}
    current_total = _payload_float(telemetry, 'energy_total_kwh', 0.0)
    prev_total = _payload_float(prev_payload, 'energy_total_kwh', 0.0)
    delta_kwh = 0.0
    if prev_payload:
        delta_kwh = max(0.0, current_total - prev_total)
        # Reject impossible jumps to avoid bad monthly aggregate due to sensor reset/noise.
        if delta_kwh > 10.0:
            delta_kwh = 0.0

    row.energy_kwh = float(row.energy_kwh or 0.0) + delta_kwh
    row.power_sum_w = float(row.power_sum_w or 0.0) + max(0.0, _payload_float(telemetry, 'p', 0.0))
    row.sample_count = int(row.sample_count or 0) + 1
    row.date_update = now_dt
    db.session.add(row)

    avg_power = 0.0
    if int(row.sample_count or 0) > 0:
        avg_power = float(row.power_sum_w or 0.0) / float(row.sample_count)

    return {
        'monthly_energy_kwh': round(float(row.energy_kwh or 0.0), 4),
        'monthly_avg_power_w': round(avg_power, 2),
    }


def _payload_float(payload: dict, key: str, default: float = 0.0, *aliases: str) -> float:
    for name in (key, *aliases):
        try:
            value = payload.get(name, None)
            if value is None or value == '':
                continue
            return float(value)
        except (TypeError, ValueError):
            continue
    return float(default)


def _payload_int(payload: dict, key: str, default: int = 0, *aliases: str) -> int:
    for name in (key, *aliases):
        try:
            value = payload.get(name, None)
            if value is None or value == '':
                continue
            return int(float(value))
        except (TypeError, ValueError):
            continue
    return int(default)


def _payload_bool(payload: dict, key: str, default: bool = False, *aliases: str) -> bool:
    for name in (key, *aliases):
        value = payload.get(name, None)
        if value is None or value == '':
            continue
        if isinstance(value, bool):
            return value
        text = str(value).strip().lower()
        if text in {'1', 'true', 'yes', 'on'}:
            return True
        if text in {'0', 'false', 'no', 'off'}:
            return False
    return bool(default)


def _normalize_station_state(raw_state: str) -> str:
    state = str(raw_state or '').strip().upper().replace(' ', '_')
    if not state:
        return 'MAT_KET_NOI'

    if state in {'SAC_CHO', 'WAIT', 'IDLE', 'CHO', 'CS_WAIT'}:
        return 'SAC_CHO'
    if state in {'DANG_SAC', 'CHARGING', 'SAC'}:
        return 'DANG_SAC'
    if state in {'DUNG', 'STOP', 'STOPPED', 'PAUSED'}:
        return 'DUNG'
    if state in {'DANG_DO', 'PROBING', 'DO'}:
        return 'DANG_DO'
    if state in {'READY', 'SAN_SANG'}:
        return 'SAN_SANG'
    if state in {'LOI_CAM_BIEN', 'SENSOR_ERROR'}:
        return 'LOI_CAM_BIEN'
    if state in {'NGAT_NHIET', 'STOP_TEMP'}:
        return 'NGAT_NHIET'
    if state in {'NGAT_DO_AM', 'STOP_HUMID'}:
        return 'NGAT_DO_AM'
    if state in {'NGAT_QUA_GIO', 'STOP_TIMEOUT'}:
        return 'NGAT_QUA_GIO'
    if state in {'NGAT_QUA_CONG_SUAT', 'STOP_POWER'}:
        return 'NGAT_QUA_CONG_SUAT'

    full_aliases = {
        'FULL', 'DAY', 'SAC_XONG', 'DONE', 'COMPLETED', 'HOAN_TAT', 'DA_DAY'
    }
    if state in full_aliases:
        return 'FULL'

    # Keep backward compatibility: detailed stop/error states still map to LOI if sent in free-form text.
    if state.startswith('NGAT_'):
        return 'LOI'
    if 'LOI' in state or 'ERROR' in state:
        return 'LOI'

    return state


def save_station_telemetry_by_token(token: str, payload: dict) -> dict:
    station = Station.query.filter_by(token=token).first()
    if not station:
        log_nhatky_event(
            username='station_unknown',
            action='DEVICE_TELEMETRY_PUSH',
            loai='Device',
            status='FAILED',
            source='STATION',
            detail='Token không hợp lệ',
        )
        raise Exception('Token không hợp lệ')

    redis_ready = _redis_is_ready()
    if not redis_ready:
        log_nhatky_event(
            username=station.username,
            action='DEVICE_TELEMETRY_PUSH',
            loai='Device',
            status='FAILED',
            source='STATION',
            station_id=station.id,
            detail='Redis chưa sẵn sàng',
        )
        raise Exception('Redis chưa sẵn sàng')

    prev_state = ''
    prev_payload = {}
    raw_prev = redis_client.get(_telemetry_key(station.id))
    if raw_prev:
        try:
            prev_payload = json.loads(raw_prev) or {}
            prev_state = _normalize_station_state(prev_payload.get('state', ''))
        except Exception:
            prev_state = ''
            prev_payload = {}

    raw_state = payload.get('state', payload.get('status', ''))
    state = _normalize_station_state(raw_state)

    if state == 'MAT_KET_NOI':
        # Device is posting telemetry, so infer a safer online state when explicit state is missing.
        p_val = _payload_float(payload, 'p', 0.0)
        i_val = _payload_float(payload, 'i', 0.0, 'current')
        relay_on_val = _payload_bool(payload, 'relay_on', False, 'relay')
        charging_val = _payload_bool(payload, 'charging', False)
        probe_val = _payload_bool(payload, 'probe', False)

        has_live_signal = relay_on_val or charging_val or probe_val or p_val > 1.0 or i_val > 0.02
        if has_live_signal:
            state = 'DANG_SAC' if (charging_val or p_val > 5.0 or i_val > 0.1) else 'SAN_SANG'
    full_charge_minutes = int(payload.get('charge_time_min', payload.get('time', 0)) or 0)
    cfg = get_station_config(station.username, station.id)
    cfg_max_h = int(cfg.get('max_time_h', 0) or 0)

    telemetry = {
        'station_id': station.id,
        'station_name': station.name,
        'p': _payload_float(payload, 'p', 0.0),
        'v': _payload_float(payload, 'v', 0.0, 'voltage'),
        'i': _payload_float(payload, 'i', 0.0, 'current'),
        'ds': _payload_float(payload, 'ds', 0.0),
        'mlx': _payload_float(payload, 'mlx', 0.0),
        'dht_t': _payload_float(payload, 'dht_t', 0.0, 'env_t', 'temp_env'),
        'env_t': _payload_float(payload, 'env_t', 0.0, 'dht_t', 'temp_env'),
        'dht_h': _payload_float(payload, 'dht_h', 0.0, 'hum'),
        'hum': _payload_float(payload, 'hum', 0.0, 'dht_h'),
        'state': state,
        'state_text': str(payload.get('state_text', '') or '').strip(),
        'charge_time_min': int(payload.get('charge_time_min', payload.get('time', 0)) or 0),
        'est': payload.get('est', None),
        'relay_on': _payload_bool(payload, 'relay_on', False, 'relay'),
        'charging': _payload_bool(payload, 'charging', state == 'DANG_SAC'),
        'auto_enabled': _payload_bool(payload, 'auto_enabled', False),
        'lockout': _payload_bool(payload, 'lockout', False),
        'probe': _payload_bool(payload, 'probe', state == 'DANG_DO'),
        'wifi_rssi': _payload_int(payload, 'wifi_rssi', 0, 'rssi'),
        'sensor_ds_ok': _payload_bool(payload, 'sensor_ds_ok', True, 'ds_ok'),
        'sensor_mlx_ok': _payload_bool(payload, 'sensor_mlx_ok', True, 'mlx_ok'),
        'sensor_pzem_ok': _payload_bool(payload, 'sensor_pzem_ok', True, 'pzem_ok'),
        'wait_min': _payload_int(payload, 'wait_min', 0),
        'measure_sec': _payload_int(payload, 'measure_sec', 0),
        'full_w': _payload_float(payload, 'full_w', 0.0, 'threshold_w'),
        'p_max': _payload_float(payload, 'p_max', 0.0, 'limit_input_w'),
        'max_charge_h': _payload_int(payload, 'max_charge_h', cfg_max_h, 'max_time_h'),
        'next_start_sec': _payload_int(payload, 'next_start_sec', 0),
        'full_hold_sec': _payload_int(payload, 'full_hold_sec', 0),
        'energy_session_kwh': _payload_float(payload, 'energy_session_kwh', 0.0, 'charge_energy_kwh'),
        'energy_last_full_kwh': _payload_float(payload, 'energy_last_full_kwh', 0.0, 'full_energy_kwh'),
        'energy_total_kwh': _payload_float(payload, 'energy_total_kwh', 0.0, 'total_energy_kwh'),
        'monthly_energy_kwh': 0,
        'monthly_avg_power_w': 0,
        'ts': int(time.time()),
    }

    if int(telemetry.get('max_charge_h', 0) or 0) <= 0:
        telemetry['max_charge_h'] = cfg_max_h

    if telemetry.get('charge_time_min', 0) < 0:
        telemetry['charge_time_min'] = 0

    try:
        raw_est = telemetry.get('est', None)
        est_minutes = int(float(raw_est)) if raw_est not in {None, ''} else -1
    except Exception:
        est_minutes = -1

    if est_minutes < 0:
        est_minutes = _compute_est_minutes(telemetry.get('charge_time_min', 0), telemetry.get('max_charge_h', 0))

    telemetry['est'] = max(0, est_minutes)

    # Update monthly stats BEFORE resetting display metrics to preserve energy data
    try:
        monthly_stats = _update_station_monthly_stat(station.id, telemetry, prev_payload)
        telemetry.update(monthly_stats)
    except Exception as ex:
        db.session.rollback()
        app.logger.warning(f"Monthly stat update failed for station {station.id}: {ex}")

    # Save full telemetry data to Redis BEFORE any resets
    redis_client.setex(
        _telemetry_key(station.id),
        _telemetry_timeout_for_state(state),
        json.dumps(telemetry, ensure_ascii=False)
    )

    # Khi trạm báo FULL: ghi lại phiên bản display (reset các metric về 0) vào Redis.
    # Bản full data đã được lưu ở trên để phục vụ thống kê.
    if state == 'FULL':
        display_telemetry = telemetry.copy()
        display_telemetry.update({
            'p': 0, 'v': 0, 'i': 0,
            'ds': 0, 'mlx': 0, 'dht_t': 0, 'env_t': 0, 'dht_h': 0, 'hum': 0,
            'charge_time_min': 0, 'est': 0,
            'relay_on': False, 'charging': False, 'probe': False,
        })
        redis_client.setex(
            _telemetry_key(station.id),
            _telemetry_timeout_for_state(state),
            json.dumps(display_telemetry, ensure_ascii=False),
        )


    now_dt = datetime.datetime.now()
    was_online = str(getattr(station, 'status', '') or '').lower() == 'online'

    if not was_online:
        offline_seconds = 0
        raw_offline = redis_client.get(_offline_since_key(station.id))
        if raw_offline:
            try:
                offline_seconds = max(0, int(time.time()) - int(raw_offline))
            except Exception:
                offline_seconds = 0
            redis_client.delete(_offline_since_key(station.id))
        elif getattr(station, 'date_update', None):
            offline_seconds = max(0, int((now_dt - station.date_update).total_seconds()))

        log_nhatky_event(
            username=station.username,
            action='STATION_ONLINE',
            loai='Station',
            status='SUCCESS',
            source='STATION',
            station_id=station.id,
            detail='Trạm online trở lại',
            payload={'offline_seconds': offline_seconds, 'state': state},
        )

    station.status = 'online'
    station.date_update = now_dt
    db.session.commit()

    if state != prev_state:
        log_nhatky_event(
            username=station.username,
            action='STATION_STATE_CHANGE',
            loai='Station',
            status='SUCCESS',
            source='STATION',
            station_id=station.id,
            detail='Trạm đổi trạng thái',
            payload={'from_state': prev_state, 'to_state': state},
        )

    if state == 'FULL' and prev_state != 'FULL':
        should_log_full = True
        key = _full_log_key(station.id)
        if redis_client.get(key):
            should_log_full = False
        else:
            # Chặn log FULL lặp trong 15 phút (race request / flapping state)
            redis_client.setex(key, 900, '1')

        if should_log_full:
            log_nhatky_event(
                username=station.username,
                action='STATION_CHARGE_FULL',
                loai='Station',
                status='SUCCESS',
                source='STATION',
                station_id=station.id,
                detail='Trạm báo sạc đầy',
                payload={'charge_time_min': full_charge_minutes},
            )

    log_nhatky_event(
        username=station.username,
        action='DEVICE_TELEMETRY_PUSH',
        loai='Device',
        status='SUCCESS',
        source='STATION',
        station_id=station.id,
        detail='Trạm gửi telemetry',
        payload={
            'state': state,
            'p': telemetry['p'],
            'v': telemetry['v'],
            'i': telemetry['i'],
        },
    )

    return {'station_id': station.id, 'station_name': station.name}


def get_station_telemetry(username: str, station_id: int) -> dict:
    station = Station.query.filter_by(id=station_id, username=username).first()
    if not station:
        raise Exception('Trạm sạc không tồn tại')

    default_data = _default_station_telemetry(station.id, station.name)

    def _mark_station_offline_if_needed(reason: str) -> None:
        if str(getattr(station, 'status', '') or '').lower() == 'offline':
            return

        now_dt = datetime.datetime.now()
        online_seconds = 0
        if getattr(station, 'date_update', None):
            online_seconds = max(0, int((now_dt - station.date_update).total_seconds()))

        station.status = 'offline'
        station.date_update = now_dt
        db.session.commit()

        if _redis_is_ready():
            redis_client.setex(_offline_since_key(station.id), 86400, str(int(time.time())))

        log_nhatky_event(
            username=station.username,
            action='STATION_OFFLINE',
            loai='Station',
            status='SUCCESS',
            source='SERVER',
            station_id=station.id,
            detail=reason,
            payload={'online_seconds': online_seconds},
        )

    if not _redis_is_ready():
        _mark_station_offline_if_needed('Redis không sẵn sàng')
        return default_data

    raw = redis_client.get(_telemetry_key(station.id))
    if not raw:
        _mark_station_offline_if_needed('Không có telemetry mới')
        return default_data

    try:
        data = json.loads(raw)
        ts = int(data.get('ts', 0) or 0)
        stale_timeout = _telemetry_timeout_for_state(data.get('state', ''))
        if not ts or (int(time.time()) - ts) > stale_timeout:
            _mark_station_offline_if_needed('Telemetry quá hạn')
            return default_data
        return {**default_data, **data}
    except Exception:
        _mark_station_offline_if_needed('Telemetry lỗi định dạng')
        return default_data


def get_latest_station_telemetry(username: str) -> Optional[dict]:
    """Lấy telemetry mới nhất còn hạn của tất cả trạm thuộc user."""
    if not _redis_is_ready():
        return None

    stations = Station.query.filter_by(username=username).all()
    now_ts = int(time.time())
    latest = None

    for station in stations:
        raw = redis_client.get(_telemetry_key(station.id))
        if not raw:
            continue
        try:
            data = json.loads(raw)
            ts = int(data.get('ts', 0) or 0)
            stale_timeout = _telemetry_timeout_for_state(data.get('state', ''))
            if not ts or (now_ts - ts) > stale_timeout:
                continue
            item = _default_station_telemetry(station.id, station.name, ts=ts, **data)
            if latest is None or int(item.get('ts', 0) or 0) > int(latest.get('ts', 0) or 0):
                latest = item
        except Exception:
            continue

    return latest


def queue_station_command(username: str, station_id: int, action: str) -> None:
    station = Station.query.filter_by(id=station_id, username=username).first()
    if not station:
        log_nhatky_event(
            username=username,
            action='DASHBOARD_COMMAND',
            loai='Station',
            status='FAILED',
            source='WEB',
            station_id=station_id,
            detail='Trạm sạc không tồn tại',
            payload={'command': action},
        )
        raise Exception('Trạm sạc không tồn tại')
    if not _redis_is_ready():
        log_nhatky_event(
            username=username,
            action='DASHBOARD_COMMAND',
            loai='Station',
            status='FAILED',
            source='WEB',
            station_id=station.id,
            detail='Redis chưa sẵn sàng',
            payload={'command': action},
        )
        raise Exception('Redis chưa sẵn sàng')

    payload = {
        'action': action,
        'by': username,
        'ts': int(time.time()),
        'attempt': 1,  # Track retry attempts
    }
    
    command_ttl = 300
    redis_client.setex(_command_key(station.id), command_ttl, json.dumps(payload, ensure_ascii=False))

    # Audit log: single capped list per station (max 50 entries, TTL 24h)
    # Tránh tạo key mới mỗi giây — gây tích lũy hàng nghìn key trong Redis.
    audit_key = f"sc:command_audit:{station.id}"
    pipe = redis_client.pipeline()
    pipe.lpush(audit_key, json.dumps(payload, ensure_ascii=False))
    pipe.ltrim(audit_key, 0, 49)   # giữ tối đa 50 bản ghi gần nhất
    pipe.expire(audit_key, 86400)
    pipe.execute()

    log_nhatky_event(
        username=username,
        action='DASHBOARD_COMMAND',
        loai='Station',
        status='SUCCESS',
        source='WEB',
        station_id=station.id,
        detail='Đẩy lệnh xuống trạm',
        payload={'command': action, 'ttl_seconds': command_ttl},
    )


def get_station_config_by_token(token: str) -> dict:
    station = Station.query.filter_by(token=token).first()
    if not station:
        log_nhatky_event(
            username='station_unknown',
            action='DEVICE_GET_CONFIG',
            loai='Device',
            status='FAILED',
            source='STATION',
            detail='Token không hợp lệ',
        )
        raise Exception('Token không hợp lệ')

    cfg = get_station_config(station.username, station.id)
    user = User.query.filter_by(username=station.username).first()
    tg_token = str(getattr(user, 'api_telegram', '') or '').strip()
    tg_chat_id = str(getattr(user, 'id_client_telegram', '') or '').strip()
    command = None

    if _redis_is_ready():
        raw_cmd = redis_client.get(_command_key(station.id))
        if raw_cmd:
            try:
                command = json.loads(raw_cmd)
                redis_client.delete(_command_key(station.id))
            except Exception:
                command = None

    log_nhatky_event(
        username=station.username,
        action='DEVICE_GET_CONFIG',
        loai='Device',
        status='SUCCESS',
        source='STATION',
        station_id=station.id,
        detail='Trạm lấy cấu hình',
        payload={'has_command': bool(command)},
    )

    return {
        'station_id': station.id,
        'station_name': station.name,
        'config': cfg,
        'telegram': {
            'enabled': bool(tg_token and tg_chat_id),
            'token': tg_token,
            'chat_id': tg_chat_id,
        },
        'command': command,
        'server_ts': int(time.time()),
    }


def inject_demo_telemetry(username: str, station_id: int) -> dict:
    """Ghi dữ liệu giả lập vào Redis để demo dashboard mà không cần thiết bị thật."""
    station = Station.query.filter_by(id=station_id, username=username).first()
    if not station:
        raise ValueError('Không tìm thấy trạm hoặc không có quyền truy cập')

    if not _redis_is_ready():
        raise RuntimeError('Redis chưa sẵn sàng')

    now = int(time.time())
    p = round(random.uniform(78.0, 115.0), 1)
    i_val = round(p / 220.0, 2)
    charge_min = random.randint(10, 55)
    max_h = 8
    est = max(0, max_h * 60 - charge_min)
    energy_session = round(p * charge_min / 60000.0, 3)

    demo = {
        'station_id': station.id,
        'station_name': station.name,
        'p': p,
        'v': round(random.uniform(218.0, 224.0), 1),
        'i': i_val,
        'ds': round(random.uniform(30.0, 42.0), 1),
        'mlx': round(random.uniform(33.0, 46.0), 1),
        'dht_t': round(random.uniform(27.0, 33.0), 1),
        'env_t': round(random.uniform(27.0, 33.0), 1),
        'dht_h': round(random.uniform(55.0, 75.0), 1),
        'hum': round(random.uniform(55.0, 75.0), 1),
        'state': 'DANG_SAC',
        'state_text': 'Đang sạc (Demo)',
        'charge_time_min': charge_min,
        'est': est,
        'relay_on': True,
        'charging': True,
        'auto_enabled': True,
        'lockout': False,
        'probe': False,
        'wifi_rssi': random.randint(-70, -40),
        'sensor_ds_ok': True,
        'sensor_mlx_ok': True,
        'sensor_pzem_ok': True,
        'wait_min': 0,
        'measure_sec': 0,
        'full_w': 10.0,
        'p_max': 200.0,
        'max_charge_h': max_h,
        'next_start_sec': 0,
        'full_hold_sec': 0,
        'energy_session_kwh': energy_session,
        'energy_last_full_kwh': round(random.uniform(0.20, 0.85), 3),
        'energy_total_kwh': round(random.uniform(1.0, 20.0), 3),
        'monthly_energy_kwh': round(random.uniform(0.5, 6.0), 3),
        'monthly_avg_power_w': round(random.uniform(60.0, 105.0), 1),
        'ts': now,
    }

    redis_client.setex(
        _telemetry_key(station.id),
        TELEMETRY_TIMEOUT_SECONDS,
        json.dumps(demo, ensure_ascii=False),
    )
    return demo
