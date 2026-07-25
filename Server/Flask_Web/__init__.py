from flask import Flask
from flask_sqlalchemy import SQLAlchemy
from flask_login import LoginManager
import os
import redis
from dotenv import load_dotenv

# Load root .env file
BASE_DIR = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
load_dotenv(os.path.join(BASE_DIR, '.env'))


def _get_int_env(name, default):
    try:
        return int(os.environ.get(name, default))
    except (TypeError, ValueError):
        return default


app = Flask(__name__)
app.secret_key = os.environ.get('SECRET_KEY', '')
app.config['SQLALCHEMY_DATABASE_URI'] = os.environ.get('DATABASE_URL', '')
app.config['SQLALCHEMY_TRACK_MODIFICATIONS'] = False

# Load API and email config from .env
app.config['GEMINI_API_KEY'] = os.environ.get('GEMINI_API_KEY', '')
app.config['EMAIL_HOST'] = os.environ.get('EMAIL_HOST', 'smtp.zoho.com')
app.config['EMAIL_PORT'] = _get_int_env('EMAIL_PORT', 587)
app.config['EMAIL_USER'] = os.environ.get('EMAIL_USER', '')
app.config['EMAIL_PASS'] = os.environ.get('EMAIL_PASS', '')

# ── Cookie / Session isolation ────────────────────────────────────────────────
# Đặt tên riêng để tránh xung đột với các service khác cùng domain.
# SESSION_COOKIE_PATH giới hạn cookie chỉ gửi khi trình duyệt truy cập
# đúng path prefix (mặc định "/" = toàn bộ domain).
_cookie_secure   = os.environ.get('SESSION_COOKIE_SECURE', 'true').strip().lower() not in ('false', '0', 'no')
_cookie_samesite = os.environ.get('SESSION_COOKIE_SAMESITE', 'Lax').strip()
_cookie_path     = os.environ.get('SESSION_COOKIE_PATH', '/').strip()
_cookie_domain   = os.environ.get('SESSION_COOKIE_DOMAIN', '').strip() or None

app.config.update(
    SESSION_COOKIE_NAME     = 'smartsac_sid',   # tên duy nhất, không đụng cookie khác
    SESSION_COOKIE_PATH     = _cookie_path,
    SESSION_COOKIE_SECURE   = _cookie_secure,   # chỉ gửi qua HTTPS
    SESSION_COOKIE_HTTPONLY = True,             # JS không đọc được cookie
    SESSION_COOKIE_SAMESITE = _cookie_samesite, # 'Lax' chặn CSRF cross-site
    SESSION_COOKIE_DOMAIN   = _cookie_domain,   # None = dùng host hiện tại
    # Flask-Login remember_me cookie — cũng cần tên riêng
    REMEMBER_COOKIE_NAME     = 'smartsac_rm',
    REMEMBER_COOKIE_PATH     = _cookie_path,
    REMEMBER_COOKIE_SECURE   = _cookie_secure,
    REMEMBER_COOKIE_HTTPONLY = True,
    REMEMBER_COOKIE_SAMESITE = _cookie_samesite,
    REMEMBER_COOKIE_DOMAIN   = _cookie_domain,
)

# Redis configuration (station telemetry/command cache)
app.config['USE_AI'] = os.environ.get('USE_AI', 'true').strip().lower() not in ('false', '0', 'no', '')

app.config['REDIS_HOST'] = os.environ.get('REDIS_HOST', '127.0.0.1')
app.config['REDIS_PORT'] = _get_int_env('REDIS_PORT', 6379)
app.config['REDIS_DB'] = _get_int_env('REDIS_DB', 0)
app.config['REDIS_PASSWORD'] = os.environ.get('REDIS_PASSWORD', '')

# Cloudflare Turnstile CAPTCHA
app.config['USE_CAPTCHA']             = os.environ.get('USE_CAPTCHA', 'true').strip().lower() not in ('false', '0', 'no', '')
app.config['CF_TURNSTILE_SITE_KEY']   = os.environ.get('CF_TURNSTILE_SITE_KEY', '')
app.config['CF_TURNSTILE_SECRET_KEY'] = os.environ.get('CF_TURNSTILE_SECRET_KEY', '')

db = SQLAlchemy(app=app)
login=LoginManager(app=app)

redis_client = redis.Redis(
    host=app.config['REDIS_HOST'],
    port=app.config['REDIS_PORT'],
    db=app.config['REDIS_DB'],
    password=app.config['REDIS_PASSWORD'] or None,
    decode_responses=True,
    socket_connect_timeout=1,
    socket_timeout=1,
    max_connections=20,          # giới hạn pool tránh tăng vô hạn khi traffic đột biến
    health_check_interval=30,    # tự động loại kết nối chết ra khỏi pool sau 30s
)
