import requests as _requests

from Flask_Web import app, login, redis_client
from flask import (
    render_template, render_template_string, request, redirect,
    url_for, session, Response, jsonify, send_from_directory, abort, g,
)
from flask_login import login_user, logout_user, current_user
from werkzeug.middleware.proxy_fix import ProxyFix
from . import utils
from .security import rate_limit, require_station_ownership, get_real_client_ip

# Hệ thống đi qua 2 lớp proxy: Cloudflare (lớp 1) + Nginx (lớp 2).
# x_for=2 → Werkzeug bỏ qua 2 proxy cuối trong X-Forwarded-For khi tính remote_addr,
# x_proto/x_host=1 → dùng scheme và host từ proxy đầu tiên tin cậy.
app.wsgi_app = ProxyFix(app.wsgi_app, x_for=2, x_proto=1, x_host=1, x_prefix=1)


# ─────────────────────────────────────────────────────────────────────────────
# TRANG LỖI 429 — Giao diện HTML cho trình duyệt (phong cách SmartSac)
# ─────────────────────────────────────────────────────────────────────────────

_HTML_429 = """\
<!DOCTYPE html>
<html lang="vi">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>Quá nhiều yêu cầu – SmartSac</title>
    <link rel="preconnect" href="https://fonts.googleapis.com">
    <link href="https://fonts.googleapis.com/css2?family=Playfair+Display:wght@400;600;700&display=swap" rel="stylesheet">
    <style>
        *, *::before, *::after { box-sizing: border-box; margin: 0; padding: 0; }
        body {
            font-family: 'Playfair Display', Georgia, 'Times New Roman', serif;
            background: #ffffff;
            color: #111111;
            display: flex;
            align-items: center;
            justify-content: center;
            min-height: 100vh;
            padding: 2rem;
        }
        .container {
            text-align: center;
            max-width: 500px;
            width: 100%;
        }
        .code {
            font-size: 7rem;
            font-weight: 700;
            line-height: 1;
            letter-spacing: -0.03em;
            margin-bottom: 1.25rem;
        }
        .divider {
            width: 2.5rem;
            height: 2px;
            background: #111111;
            margin: 1.25rem auto;
        }
        h1 {
            font-size: 1.6rem;
            font-weight: 600;
            margin-bottom: 1rem;
            letter-spacing: 0.01em;
        }
        p {
            font-size: 1rem;
            line-height: 1.75;
            color: #555555;
            margin-bottom: 2.5rem;
        }
        .countdown {
            font-weight: 700;
            color: #111111;
        }
        .btn {
            display: inline-block;
            background: #111111;
            color: #ffffff;
            text-decoration: none;
            padding: 0.8rem 2.25rem;
            font-family: inherit;
            font-size: 0.9rem;
            font-weight: 600;
            letter-spacing: 0.08em;
            text-transform: uppercase;
            border: 2px solid #111111;
            transition: background 0.2s ease, color 0.2s ease;
        }
        .btn:hover { background: #ffffff; color: #111111; }
    </style>
</head>
<body>
    <div class="container">
        <div class="code">429</div>
        <div class="divider"></div>
        <h1>Quá nhiều yêu cầu</h1>
        <p>
            Bạn đã gửi quá nhiều yêu cầu trong một khoảng thời gian ngắn.<br>
            {% if retry_after > 0 %}
            Vui lòng chờ thêm <span id="cd" class="countdown">{{ retry_after }}</span> giây rồi thử lại.
            {% else %}
            Vui lòng thử lại sau ít phút.
            {% endif %}
        </p>
        <a href="/" class="btn">Quay lại Trang chủ</a>
    </div>
    {% if retry_after > 0 %}
    <script>
        (function () {
            var s = {{ retry_after }};
            var el = document.getElementById('cd');
            if (!el || s <= 0) return;
            var t = setInterval(function () {
                s -= 1;
                el.textContent = s;
                if (s <= 0) { clearInterval(t); }
            }, 1000);
        }());
    </script>
    {% endif %}
</body>
</html>
"""


# ─────────────────────────────────────────────────────────────────────────────
# HELPER NỘI BỘ
# ─────────────────────────────────────────────────────────────────────────────

def _read_token_from_request():
    auth = request.headers.get('Authorization', '')
    if auth.lower().startswith('bearer '):
        return auth[7:].strip()

    token = request.headers.get('X-Station-Token', '').strip()
    if token:
        return token

    if request.is_json:
        token = (request.get_json(silent=True) or {}).get('token', '')
        if token:
            return str(token).strip()

    return request.args.get('token', '').strip() or request.form.get('token', '').strip()


def _client_ip() -> str:
    return get_real_client_ip()


_CF_SITEVERIFY = 'https://challenges.cloudflare.com/turnstile/v0/siteverify'


def validate_turnstile(token: str) -> bool:
    """
    Xác thực token Cloudflare Turnstile với Siteverify API.

    Trả về True nếu hợp lệ, False nếu không.
    Fail-open (True) khi:
      • SECRET_KEY chưa cấu hình → môi trường dev, bỏ qua validation.
      • Cloudflare API không phản hồi → tránh block toàn bộ user hợp lệ.
    Fail-closed (False) khi:
      • Token rỗng hoặc thiếu → client chưa hoàn thành widget.
      • API trả về success=false → token giả/hết hạn.
    """
    if not app.config.get('USE_CAPTCHA', True):
        return True

    secret = app.config.get('CF_TURNSTILE_SECRET_KEY', '')
    if not secret:
        return True   # Dev mode — SECRET_KEY chưa set, bỏ qua

    if not token:
        return False  # Widget chưa được hoàn thành

    try:
        resp = _requests.post(
            _CF_SITEVERIFY,
            data={
                'secret':   secret,
                'response': token,
                'remoteip': get_real_client_ip(),
            },
            timeout=5,
        )
        return bool(resp.json().get('success', False))
    except Exception:
        return True   # Fail-open: Cloudflare API không khả dụng


def _ai_disabled_response():
    """Trả về lỗi 503 khi USE_AI=false."""
    return jsonify({'status': 'error', 'message': 'Tính năng AI hiện đang được bảo trì.'}), 503


def _write_action_log(username: str, action: str, loai: str, status: str = 'SUCCESS', station_id=None, detail: str = '', payload=None, source: str = 'WEB'):
    utils.log_nhatky_event(
        username=username,
        action=action,
        loai=loai,
        status=status,
        source=source,
        station_id=station_id,
        detail=detail,
        payload={
            'path': request.path,
            'method': request.method,
            'ip': _client_ip(),
            **(payload or {}),
        },
    )


# ─────────────────────────────────────────────────────────────────────────────
# GLOBAL ERROR HANDLER — 429 Too Many Requests
# ─────────────────────────────────────────────────────────────────────────────

@app.errorhandler(429)
def ratelimit_handler(e):
    """
    Bắt toàn bộ lỗi 429 trong ứng dụng và trả về định dạng phù hợp:
      - /api/* hoặc client yêu cầu JSON  → JSON thuần (tương thích thiết bị/phần cứng)
      - Trình duyệt web                   → Trang HTML sang trọng phong cách SmartSac
    Số giây chờ được lấy từ g.retry_after do rate_limit decorator ghi vào.
    """
    retry_after = max(1, int(getattr(g, 'retry_after', 60)))

    is_api_path = request.path.startswith('/api/')
    wants_json = (
        request.accept_mimetypes.best_match(['application/json', 'text/html']) == 'application/json'
    )

    if is_api_path or wants_json:
        resp = jsonify({
            'status': 'error',
            'message': f'Quá nhiều yêu cầu. Vui lòng thử lại sau {retry_after} giây.',
        })
        resp.status_code = 429
        resp.headers['Retry-After'] = str(retry_after)
        resp.headers['X-RateLimit-Limit'] = str(getattr(g, 'ratelimit_max', ''))
        resp.headers['X-RateLimit-Window'] = str(getattr(g, 'ratelimit_window', ''))
        return resp

    html = render_template_string(_HTML_429, retry_after=retry_after)
    return html, 429


# ─────────────────────────────────────────────────────────────────────────────
# SEO / PWA
# ─────────────────────────────────────────────────────────────────────────────

@app.route('/robots.txt')
def robots_txt():
    # LƯU Ý: Đặt toàn bộ ảnh bằng khen vào /static/img/certs/ và trỏ đúng src trong thẻ <img>
    # để lệnh Disallow bên dưới có hiệu lực bảo vệ ảnh khỏi Google Images.
    content = (
        # --- Chặn bot ảnh Google trước (group riêng, đặt trước User-agent: * theo chuẩn) ---
        "# Bảo vệ thông tin cá nhân: chặn bot hình ảnh lập chỉ mục thư mục bằng khen\n"
        "User-agent: Googlebot-Image\n"
        "Disallow: /static/img/certs/\n"
        "\n"
        # --- Luật chung cho mọi bot ---
        "User-agent: *\n"
        "\n"
        "# Chặn tất cả đường dẫn theo mặc định\n"
        "Disallow: /\n"
        "\n"
        "# Mở trang chủ và tài nguyên tĩnh cần thiết để render giao diện\n"
        "Allow: /$\n"
        "Allow: /static/\n"
        "Allow: /sw.js\n"
        "\n"
        "# Chặn thư mục bằng khen — đường dẫn dài hơn /static/ nên được ưu tiên (specificity rule)\n"
        "# Ảnh sản phẩm tại /static/img/ vẫn được Google index bình thường\n"
        "Disallow: /static/img/certs/\n"
        "\n"
        "# Chặn tường minh các route nội bộ, tài khoản và API\n"
        "Disallow: /admin\n"
        "Disallow: /api/\n"
        "Disallow: /check\n"
        "Disallow: /dashboard\n"
        "Disallow: /config\n"
        "Disallow: /config-ts\n"
        "Disallow: /log\n"
        "Disallow: /clear-nk\n"
        "Disallow: /user\n"
        "Disallow: /user_logout\n"
        "Disallow: /nap_the\n"
        "Disallow: /login\n"
        "Disallow: /signup\n"
        "Disallow: /forgot-password\n"
        "Disallow: /timcaigidohamay\n"
        "\n"
        "Sitemap: https://hetcuu.com/smartsac/sitemap.xml\n"
    )
    return Response(content, mimetype='text/plain')


@app.route('/sitemap.xml')
def sitemap_xml():
    content = (
        '<?xml version="1.0" encoding="UTF-8"?>\n'
        '<urlset xmlns="http://www.sitemaps.org/schemas/sitemap/0.9">\n'
        '\n'
        '  <!-- Trang chủ -->\n'
        '  <url>\n'
        '    <loc>https://hetcuu.com/smartsac/</loc>\n'
        '    <changefreq>weekly</changefreq>\n'
        '    <priority>1.0</priority>\n'
        '  </url>\n'
        '\n'
        '  <!-- Trang tài liệu API công khai -->\n'
        '  <url>\n'
        '    <loc>https://hetcuu.com/smartsac/tailieu/api</loc>\n'
        '    <changefreq>monthly</changefreq>\n'
        '    <priority>0.8</priority>\n'
        '  </url>\n'
        '\n'
        '  <!-- Điều khoản sử dụng -->\n'
        '  <url>\n'
        '    <loc>https://hetcuu.com/smartsac/dieu-khoan</loc>\n'
        '    <changefreq>yearly</changefreq>\n'
        '    <priority>0.5</priority>\n'
        '  </url>\n'
        '\n'
        '  <!-- Chính sách bảo mật -->\n'
        '  <url>\n'
        '    <loc>https://hetcuu.com/smartsac/chinh-sach</loc>\n'
        '    <changefreq>yearly</changefreq>\n'
        '    <priority>0.5</priority>\n'
        '  </url>\n'
        '\n'
        '</urlset>\n'
    )
    return Response(content, mimetype='application/xml')


# ─────────────────────────────────────────────────────────────────────────────
# TRANG TĨNH & PWA
# ─────────────────────────────────────────────────────────────────────────────

@app.route("/")
def home():
    return render_template('index.html')


@app.route('/manifest.webmanifest')
def pwa_manifest():
    return send_from_directory(app.static_folder, 'manifest.webmanifest', mimetype='application/manifest+json')


@app.route('/favicon.ico')
def favicon():
    return send_from_directory(app.static_folder, 'img/logo.png', mimetype='image/png')


@app.route('/sw.js')
def pwa_service_worker():
    response = send_from_directory(app.static_folder, 'sw.js', mimetype='application/javascript')
    response.headers['Service-Worker-Allowed'] = '/'
    response.headers['Cache-Control'] = 'no-cache'
    return response


@app.route('/offline')
def offline_page():
    return render_template('offline.html')


@app.errorhandler(404)
def page_not_found(e):
    return render_template('404.html'), 404


@app.route("/in4")
def in4():
    return redirect("https://hetcuu.com/in4")


# ─────────────────────────────────────────────────────────────────────────────
# XÁC THỰC — Đăng nhập / Đăng ký / Quên mật khẩu
# ─────────────────────────────────────────────────────────────────────────────

@app.route("/check")
def check():
    if current_user.is_authenticated:
        return redirect(url_for('dashboard'))
    else:
        return redirect(url_for('user_login'))


@app.route("/login", methods=['GET', 'POST'])
@rate_limit('login', max_requests=10, window_seconds=900)   # 10 lần / 15 phút mỗi IP — chống brute-force
def user_login():
    if current_user.is_authenticated:
        return redirect(url_for('dashboard'))
    err_msg = ""
    if request.method == 'POST':
        ts_token = request.form.get('cf-turnstile-response', '').strip()
        if not validate_turnstile(ts_token):
            _write_action_log('anonymous', 'USER_LOGIN', 'Auth', 'FAILED',
                              detail='Turnstile thất bại',
                              payload={'ip': get_real_client_ip()})
            err_msg = 'Xác minh bảo mật thất bại. Vui lòng thử lại.'
        else:
            try:
                username = request.form.get('username')
                password = request.form.get('password')
                user = utils.login_user(username=username, password=password)
                if user:
                    login_user(user=user)
                    _write_action_log(user.username, 'USER_LOGIN', 'Auth', 'SUCCESS', detail='Đăng nhập thành công')
                    return redirect(url_for('home'))
                else:
                    _write_action_log(str(username or 'anonymous'), 'USER_LOGIN', 'Auth', 'FAILED', detail='Sai thông tin đăng nhập')
                    err_msg = 'Sai thông tin!'
            except Exception:
                _write_action_log(str(request.form.get('username') or 'anonymous'), 'USER_LOGIN', 'Auth', 'FAILED', detail='Lỗi xử lý đăng nhập')
                err_msg = "Lỗi"
    return render_template('login.html', err_msg=err_msg,
                           turnstile_site_key=app.config.get('CF_TURNSTILE_SITE_KEY', ''))


@app.route("/signup", methods=['GET', 'POST'])
@rate_limit('signup', max_requests=5, window_seconds=3600)  # 5 lần / giờ mỗi IP — chống tạo tài khoản spam
def user_register():
    if current_user.is_authenticated:
        return redirect(url_for('home'))
    err_msg = ""
    if request.method == 'POST':
        ts_token = request.form.get('cf-turnstile-response', '').strip()
        if not validate_turnstile(ts_token):
            _write_action_log('anonymous', 'USER_REGISTER', 'Auth', 'FAILED',
                              detail='Turnstile thất bại',
                              payload={'ip': get_real_client_ip()})
            err_msg = 'Xác minh bảo mật thất bại. Vui lòng thử lại.'
        else:
            username = request.form.get('username')
            password = request.form.get('password')
            email = request.form.get('email')
            try:
                if utils.add_user(username=username, password=password, email=email) is True:
                    _write_action_log(str(username or 'anonymous'), 'USER_REGISTER', 'Auth', 'SUCCESS', detail='Tạo tài khoản thành công')
                    return redirect(url_for('user_login'))
                else:
                    _write_action_log(str(username or 'anonymous'), 'USER_REGISTER', 'Auth', 'FAILED', detail='Username/Email đã tồn tại')
                    err_msg = 'Username/Email đã tồn tại!'
            except Exception:
                _write_action_log(str(username or 'anonymous'), 'USER_REGISTER', 'Auth', 'FAILED', detail='Lỗi xử lý đăng ký')
                err_msg = "Lỗi:"
    return render_template('signup.html', err_msg=err_msg,
                           turnstile_site_key=app.config.get('CF_TURNSTILE_SITE_KEY', ''))


@app.route("/forgot-password", methods=['GET', 'POST'])
@rate_limit('forgot_pwd', max_requests=5, window_seconds=3600)  # 5 lần / giờ — hàng rào IP, Redis kiểm soát per-email
def forgot_password():
    err_msg = ""
    if request.method == 'POST':
        # ── Lớp 1: Turnstile — chặn bot trước mọi xử lý ──────────────────────
        ts_token = request.form.get('cf-turnstile-response', '').strip()
        if not validate_turnstile(ts_token):
            _write_action_log('anonymous', 'FORGOT_PASSWORD', 'Auth', 'FAILED',
                              detail='Turnstile thất bại',
                              payload={'ip': get_real_client_ip()})
            err_msg = 'Xác minh bảo mật thất bại. Vui lòng thử lại.'
            return render_template('forgot-password.html', err_msg=err_msg,
                                   turnstile_site_key=app.config.get('CF_TURNSTILE_SITE_KEY', ''))
        # ──────────────────────────────────────────────────────────────────────

        email = (request.form.get('email') or '').strip().lower()

        # ── Lớp 2: Chống spam per-email qua Redis (TTL 120 giây) ──────────────
        # Chặn gửi lại email reset trong vòng 2 phút kể từ lần gửi gần nhất.
        # Fail-open: nếu Redis lỗi thì vẫn cho phép gửi để không gián đoạn dịch vụ.
        if email:
            redis_key = f"spam:forgot_password:{email}"
            try:
                if redis_client.exists(redis_key):
                    err_msg = (
                        "Yêu cầu đổi mật khẩu của bạn đang được xử lý. "
                        "Vui lòng kiểm tra hộp thư hoặc quay lại sau 2 phút."
                    )
                    return render_template('forgot-password.html', err_msg=err_msg,
                                           turnstile_site_key=app.config.get('CF_TURNSTILE_SITE_KEY', ''))
                # Khóa email này trong 120 giây trước khi gọi gửi thư
                redis_client.setex(redis_key, 120, "active")
            except Exception:
                pass  # Redis không khả dụng → fail-open
        # ──────────────────────────────────────────────────────────────────────

        try:
            err_msg = utils.mail(email=email)
            _write_action_log('anonymous', 'FORGOT_PASSWORD', 'Auth', 'SUCCESS',
                              detail='Yêu cầu quên mật khẩu', payload={'email': email})
        except Exception as ex:
            _write_action_log('anonymous', 'FORGOT_PASSWORD', 'Auth', 'FAILED',
                              detail=str(ex), payload={'email': email})
            err_msg = "Lỗi"
    return render_template('forgot-password.html', err_msg=err_msg,
                           turnstile_site_key=app.config.get('CF_TURNSTILE_SITE_KEY', ''))


# ─────────────────────────────────────────────────────────────────────────────
# NHẬT KÝ
# ─────────────────────────────────────────────────────────────────────────────

@app.route("/clear-nk")
def clear_nk():
    if current_user.is_authenticated:
        utils.clear_nk(current_user.username)
        return redirect(url_for('log'))
    else:
        return redirect(url_for('home'))


@app.route("/log")
def log():
    if current_user.is_authenticated:
        username = current_user.username
        log = utils.nhatky_user(username)
        return render_template('log.html', log=log)
    else:
        return redirect(url_for('home'))


# ─────────────────────────────────────────────────────────────────────────────
# USER LOADER & HỒ SƠ NGƯỜI DÙNG
# ─────────────────────────────────────────────────────────────────────────────

@login.user_loader
def user_load(user_id):
    return utils.get_user_by_id(user_id=user_id)


@app.route('/user', methods=['GET'])
def user_profile():
    if current_user.is_authenticated:
        return render_template('user.html')
    else:
        return redirect(url_for('user_login'))


@app.route('/user/info', methods=['GET'])
def user_info():
    if current_user.is_authenticated:
        return jsonify({
            'username': current_user.username,
            'email': current_user.email,
        })
    else:
        return jsonify({'status': 'error', 'message': 'Chưa đăng nhập'}), 401


@app.route('/user/api-config', methods=['POST'])
def user_api_config():
    if current_user.is_authenticated:
        username = current_user.username
        tg_token = request.form.get('tg_token', '')
        tg_chatid = request.form.get('tg_chatid', '')
        try:
            utils.update_user_api_config(username, tg_token, tg_chatid)
            _write_action_log(username, 'USER_UPDATE_API_CONFIG', 'User', 'SUCCESS', detail='Cập nhật API Telegram')
            return jsonify({'status': 'success', 'message': 'Cập nhật thành công!'})
        except Exception as e:
            _write_action_log(username, 'USER_UPDATE_API_CONFIG', 'User', 'FAILED', detail=str(e))
            return jsonify({'status': 'error', 'message': str(e)}), 500
    else:
        return jsonify({'status': 'error', 'message': 'Chưa đăng nhập'}), 401


@app.route('/user/update-profile', methods=['POST'])
def user_update_profile():
    if current_user.is_authenticated:
        return jsonify({'status': 'success', 'message': 'Cập nhật thành công!'})
    else:
        return jsonify({'status': 'error', 'message': 'Chưa đăng nhập'}), 401


@app.route('/user/change-password', methods=['POST'])
def user_change_password():
    if current_user.is_authenticated:
        username = current_user.username
        old_pass = request.form.get('old_pass', '')
        new_pass = request.form.get('new_pass', '')
        confirm_pass = request.form.get('confirm_pass', '')
        try:
            result = utils.change_user_password(username, old_pass, new_pass, confirm_pass)
            if result['status'] == 'success':
                _write_action_log(username, 'USER_CHANGE_PASSWORD', 'Security', 'SUCCESS', detail='Đổi mật khẩu thành công')
                return jsonify({'status': 'success', 'message': result['message']})
            else:
                _write_action_log(username, 'USER_CHANGE_PASSWORD', 'Security', 'FAILED', detail=result['message'])
                return jsonify({'status': 'error', 'message': result['message']}), 400
        except Exception as e:
            _write_action_log(username, 'USER_CHANGE_PASSWORD', 'Security', 'FAILED', detail=str(e))
            return jsonify({'status': 'error', 'message': str(e)}), 500
    else:
        return jsonify({'status': 'error', 'message': 'Chưa đăng nhập'}), 401


@app.route('/user_logout')
def user_signout():
    if current_user.is_authenticated:
        _write_action_log(current_user.username, 'USER_LOGOUT', 'Auth', 'SUCCESS', detail='Đăng xuất')
    logout_user()
    return redirect(url_for('user_login'))


# ─────────────────────────────────────────────────────────────────────────────
# CẤU HÌNH & DASHBOARD
# ─────────────────────────────────────────────────────────────────────────────

@app.route('/config-ts', methods=['GET'])
def config_ts():
    if current_user.is_authenticated:
        return render_template('config-ts.html')
    else:
        return redirect(url_for('user_login'))


@app.route('/config', methods=['GET'])
def config():
    if current_user.is_authenticated:
        return render_template('config.html')
    else:
        return redirect(url_for('user_login'))


@app.route('/dashboard', methods=['GET'])
def dashboard():
    if current_user.is_authenticated:
        return render_template('dashboard.html')
    else:
        return redirect(url_for('user_login'))


# ─────────────────────────────────────────────────────────────────────────────
# NỘI DUNG TĨNH
# ─────────────────────────────────────────────────────────────────────────────

@app.route('/tailieu/api')
def tailieu_api():
    return render_template('tailieu_api.html')


@app.route('/dieu-khoan')
def dieu_khoan():
    return render_template('terms.html')


@app.route('/chinh-sach')
def chinh_sach():
    return render_template('privacy.html')


# ─────────────────────────────────────────────────────────────────────────────
# API — Demo & AI
# ─────────────────────────────────────────────────────────────────────────────

@app.route('/api/demo', methods=['POST'])
def api_demo():
    """Inject dữ liệu giả lập vào Redis để demo dashboard mà không cần thiết bị thật."""
    if not current_user.is_authenticated:
        return jsonify({'status': 'error', 'message': 'Chưa đăng nhập'}), 401

    station_id = request.form.get('station_id', type=int)
    if not station_id:
        return jsonify({'status': 'error', 'message': 'Thiếu station_id'}), 400

    try:
        utils.inject_demo_telemetry(current_user.username, station_id)
        return jsonify({'status': 'success', 'message': '✅ Demo đã kích hoạt — dữ liệu giả lập đang hiển thị trên dashboard'})
    except ValueError as e:
        return jsonify({'status': 'error', 'message': str(e)}), 403
    except RuntimeError as e:
        return jsonify({'status': 'error', 'message': str(e)}), 503
    except Exception as e:
        return jsonify({'status': 'error', 'message': f'Lỗi nội bộ: {e}'}), 500


@app.route('/api/config/ai-analyze', methods=['POST'])
def api_config_ai_analyze():
    if not app.config.get('USE_AI'):
        return _ai_disabled_response()
    if current_user.is_authenticated:
        username = current_user.username
        station_id_raw = request.form.get('station_id', '')
        battery_type = request.form.get('type', '')
        ah = request.form.get('ah', '20')
        out_a = request.form.get('out_a', '3.0')

        station_id = 0
        try:
            station_id = int(station_id_raw or 0)
        except (TypeError, ValueError):
            station_id = 0

        if not station_id:
            active_station = utils.get_active_station(username)
            if active_station:
                station_id = active_station.id

        if not station_id:
            return jsonify({'status': 'error', 'message': 'Thiếu station_id để phân tích AI'}), 400

        try:
            result = utils.analyze_config_with_ai(
                username=username,
                station_id=station_id,
                battery_type=battery_type,
                ah=ah,
                out_a=out_a,
            )
            return jsonify({'status': 'success', 'data': result})
        except Exception as e:
            return jsonify({'status': 'error', 'message': str(e)}), 500
    else:
        return jsonify({'status': 'error', 'message': 'Chưa đăng nhập'}), 401


@app.route('/api/dashboard/ai-assistant', methods=['POST'])
def api_dashboard_ai_assistant():
    if not app.config.get('USE_AI'):
        return _ai_disabled_response()
    if not current_user.is_authenticated:
        return jsonify({'status': 'error', 'message': 'Chưa đăng nhập'}), 401

    username = current_user.username
    station_id_raw = request.form.get('station_id', '')

    if request.is_json:
        body = request.get_json(silent=True) or {}
        station_id_raw = station_id_raw or str(body.get('station_id', ''))

    station_id = 0
    try:
        station_id = int(station_id_raw or 0)
    except (TypeError, ValueError):
        station_id = 0

    if not station_id:
        active_station = utils.get_active_station(username)
        if active_station:
            station_id = active_station.id

    if not station_id:
        return jsonify({'status': 'error', 'message': 'Thiếu station_id cho AI dashboard'}), 400

    try:
        result = utils.analyze_dashboard_status_with_ai(username=username, station_id=station_id)
        return jsonify({'status': 'success', 'data': result})
    except Exception as e:
        return jsonify({'status': 'error', 'message': str(e)}), 500


# ─────────────────────────────────────────────────────────────────────────────
# API — Cấu hình trạm
# ─────────────────────────────────────────────────────────────────────────────

@app.route('/api/config/save', methods=['POST'])
def api_config_save():
    if current_user.is_authenticated:
        username = current_user.username
        station_id = request.form.get('station_id', '')
        config_data = request.form.to_dict()
        try:
            result = utils.save_station_config(username, station_id, config_data)
            _write_action_log(username, 'CONFIG_SAVE', 'Config', 'SUCCESS', station_id=station_id, detail='Lưu cấu hình trạm')
            return jsonify({'status': 'success', 'message': result})
        except Exception as e:
            _write_action_log(username, 'CONFIG_SAVE', 'Config', 'FAILED', station_id=station_id, detail=str(e))
            return jsonify({'status': 'error', 'message': str(e)}), 500
    else:
        return jsonify({'status': 'error', 'message': 'Chưa đăng nhập'}), 401


@app.route('/api/config/<int:station_id>', methods=['GET'])
@rate_limit('api_general', max_requests=120, window_seconds=900, use_user=True)
@require_station_ownership('station_id')  # IDOR: xác thực station thuộc user trước khi trả data
def api_config_get(station_id, **kwargs):
    if not current_user.is_authenticated:
        return jsonify({'status': 'error', 'message': 'Chưa đăng nhập'}), 401
    username = current_user.username
    try:
        config = utils.get_station_config(username, station_id)
        if config:
            return jsonify({'status': 'success', 'data': config})
        else:
            return jsonify({'status': 'error', 'message': 'Không tìm thấy cấu hình'}), 404
    except Exception as e:
        return jsonify({'status': 'error', 'message': str(e)}), 500


@app.route('/api/config-ts', methods=['GET'])
def api_get_stations():
    if current_user.is_authenticated:
        username = current_user.username
        stations = utils.get_stations(username)
        return jsonify({'status': 'success', 'data': stations})
    else:
        return jsonify({'status': 'error', 'message': 'Chưa đăng nhập'}), 401


# ─────────────────────────────────────────────────────────────────────────────
# API — Quản lý trạm sạc
# ─────────────────────────────────────────────────────────────────────────────

@app.route('/api/station/add', methods=['POST'])
def api_add_station():
    if current_user.is_authenticated:
        username = current_user.username
        name = request.form.get('name', '')
        try:
            result = utils.add_station(username, name)
            if result['status'] == 'success':
                station = result.get('station', {})
                _write_action_log(username, 'STATION_ADD', 'Station', 'SUCCESS', station_id=station.get('id'), detail='Thêm trạm sạc', payload={'name': name})
                return jsonify(result)
            else:
                _write_action_log(username, 'STATION_ADD', 'Station', 'FAILED', detail=result.get('message', 'Lỗi thêm trạm'), payload={'name': name})
                return jsonify(result), 400
        except Exception as e:
            _write_action_log(username, 'STATION_ADD', 'Station', 'FAILED', detail=str(e), payload={'name': name})
            return jsonify({'status': 'error', 'message': str(e)}), 500
    else:
        return jsonify({'status': 'error', 'message': 'Chưa đăng nhập'}), 401


@app.route('/api/station/delete/<int:station_id>', methods=['POST'])
@rate_limit('api_general', max_requests=120, window_seconds=900, use_user=True)
@require_station_ownership('station_id')  # IDOR: chặn xóa trạm của user khác
def api_delete_station(station_id, **kwargs):
    if current_user.is_authenticated:
        username = current_user.username
        try:
            result = utils.delete_station(username, station_id)
            if result['status'] == 'success':
                _write_action_log(username, 'STATION_DELETE', 'Station', 'SUCCESS', station_id=station_id, detail='Xóa trạm sạc')
                return jsonify(result)
            else:
                _write_action_log(username, 'STATION_DELETE', 'Station', 'FAILED', station_id=station_id, detail=result.get('message', 'Lỗi xóa trạm'))
                return jsonify(result), 400
        except Exception as e:
            _write_action_log(username, 'STATION_DELETE', 'Station', 'FAILED', station_id=station_id, detail=str(e))
            return jsonify({'status': 'error', 'message': str(e)}), 500
    else:
        return jsonify({'status': 'error', 'message': 'Chưa đăng nhập'}), 401


@app.route('/api/station/set-active/<int:station_id>', methods=['POST'])
@rate_limit('api_general', max_requests=120, window_seconds=900, use_user=True)
@require_station_ownership('station_id')  # IDOR: chặn kích hoạt trạm của user khác
def api_set_active_station(station_id, **kwargs):
    if current_user.is_authenticated:
        username = current_user.username
        try:
            result = utils.set_active_station(username, station_id)
            if result['status'] == 'success':
                _write_action_log(username, 'STATION_SET_ACTIVE', 'Station', 'SUCCESS', station_id=station_id, detail='Đặt trạm hoạt động')
                return jsonify(result)
            else:
                _write_action_log(username, 'STATION_SET_ACTIVE', 'Station', 'FAILED', station_id=station_id, detail=result.get('message', 'Lỗi đặt active'))
                return jsonify(result), 400
        except Exception as e:
            _write_action_log(username, 'STATION_SET_ACTIVE', 'Station', 'FAILED', station_id=station_id, detail=str(e))
            return jsonify({'status': 'error', 'message': str(e)}), 500
    else:
        return jsonify({'status': 'error', 'message': 'Chưa đăng nhập'}), 401


# ─────────────────────────────────────────────────────────────────────────────
# API — Dashboard (dữ liệu & lệnh điều khiển)
# ─────────────────────────────────────────────────────────────────────────────

@app.route('/api/dashboard/data', methods=['GET'])
def api_dashboard_data():
    if not current_user.is_authenticated:
        return jsonify({'status': 'error', 'message': 'Chưa đăng nhập'}), 401

    username = current_user.username
    station_id = request.args.get('station_id', type=int)

    if not station_id:
        active_station = utils.get_active_station(username)
        if active_station:
            station_id = active_station.id

    if not station_id:
        return jsonify({'status': 'error', 'message': 'Chưa có trạm sạc hoạt động'}), 400

    try:
        data = utils.get_station_telemetry(username, station_id)
        if data.get('state') == 'MAT_KET_NOI':
            latest = utils.get_latest_station_telemetry(username)
            if latest:
                data = latest
        return jsonify({'status': 'success', 'data': data})
    except Exception as e:
        return jsonify({'status': 'error', 'message': str(e)}), 500


@app.route('/api/dashboard/command', methods=['POST'])
def api_dashboard_command():
    if not current_user.is_authenticated:
        _write_action_log('anonymous', 'DASHBOARD_COMMAND', 'Dashboard', 'FAILED', detail='Chưa đăng nhập')
        return jsonify({'status': 'error', 'message': 'Chưa đăng nhập'}), 401

    username = current_user.username
    command = request.form.get('command', '')
    station_id = request.form.get('station_id', type=int)

    if request.is_json:
        body = request.get_json(silent=True) or {}
        command = command or str(body.get('command', ''))
        try:
            station_id = station_id or int(body.get('station_id', 0) or 0)
        except (TypeError, ValueError):
            station_id = 0

    if command not in {'charge_now', 'charge_wait', 'charge_stop'}:
        _write_action_log(username, 'DASHBOARD_COMMAND', 'Dashboard', 'FAILED', station_id=station_id, detail='Lệnh không hợp lệ', payload={'command': command})
        return jsonify({'status': 'error', 'message': 'Lệnh không hợp lệ'}), 400
    if not station_id:
        _write_action_log(username, 'DASHBOARD_COMMAND', 'Dashboard', 'FAILED', detail='Thiếu station_id', payload={'command': command})
        return jsonify({'status': 'error', 'message': 'Thiếu station_id'}), 400

    try:
        utils.queue_station_command(username, station_id, command)
        _write_action_log(username, 'DASHBOARD_COMMAND', 'Dashboard', 'SUCCESS', station_id=station_id, detail='Gửi lệnh từ dashboard', payload={'command': command})
        return jsonify({'status': 'success', 'message': f'Đã gửi lệnh {command} tới trạm'})
    except Exception as e:
        _write_action_log(username, 'DASHBOARD_COMMAND', 'Dashboard', 'FAILED', station_id=station_id, detail=str(e), payload={'command': command})
        return jsonify({'status': 'error', 'message': str(e)}), 500


# ─────────────────────────────────────────────────────────────────────────────
# API — Thiết bị (telemetry & config push)
# ─────────────────────────────────────────────────────────────────────────────

@app.route('/api/station/device/telemetry', methods=['POST'])
def api_station_device_telemetry():
    token = _read_token_from_request()
    if not token:
        return jsonify({'status': 'error', 'message': 'Thiếu token'}), 401

    payload = request.get_json(silent=True) if request.is_json else request.form.to_dict()
    if not payload:
        return jsonify({'status': 'error', 'message': 'Thiếu dữ liệu telemetry'}), 400

    try:
        from Flask_Web.models import Station
        station = Station.query.filter_by(token=token).first()
        if not station:
            return jsonify({'status': 'error', 'message': 'Token không hợp lệ'}), 401

        # Thực hiện kiểm tra an toàn telemetry trước khi xử lý lưu trữ
        from .security import verify_telemetry_safety
        is_safe, error_msg = verify_telemetry_safety(station, payload)
        if not is_safe:
            return jsonify({
                'status': 'error',
                'message': f'CẢNH BÁO AN TOÀN: {error_msg}',
                'error_code': 'SAFETY_RISK_DETECTED',
                'action': 'charge_stop',
                'command': 'charge_stop'
            }), 400

        station_data = utils.save_station_telemetry_by_token(token, payload)
        return jsonify({'status': 'success', 'message': 'Đã nhận telemetry', 'station': station_data})
    except Exception as e:
        return jsonify({'status': 'error', 'message': str(e)}), 400


@app.route('/api/station/device/config', methods=['GET'])
def api_station_device_config():
    token = _read_token_from_request()
    if not token:
        return jsonify({'status': 'error', 'message': 'Thiếu token'}), 401

    try:
        data = utils.get_station_config_by_token(token)
        return jsonify({'status': 'success', 'data': data})
    except Exception as e:
        return jsonify({'status': 'error', 'message': str(e)}), 400


# ─────────────────────────────────────────────────────────────────────────────
# API — Debug
# ─────────────────────────────────────────────────────────────────────────────

@app.route('/api/debug/station-check', methods=['GET'])
def api_debug_station_check():
    """Debug: kiểm tra trực tiếp Redis theo token — không cần đăng nhập."""
    token = request.args.get('token', '').strip()
    if not token:
        return jsonify({'status': 'error', 'message': 'Thiếu ?token='}), 400

    from Flask_Web.models import Station
    station = Station.query.filter_by(token=token).first()
    if not station:
        return jsonify({'status': 'error', 'message': f'Token không tìm thấy trong DB: {token}'}), 404

    import time as _time
    redis_ok = utils._redis_is_ready()
    raw = None
    if redis_ok:
        raw = utils.redis_client.get(utils._telemetry_key(station.id))

    import json as _json
    telemetry = None
    ts_age = None
    if raw:
        try:
            telemetry = _json.loads(raw)
            ts_age = int(_time.time()) - int(telemetry.get('ts', 0) or 0)
        except Exception:
            pass

    return jsonify({
        'status': 'success',
        'station': {
            'id': station.id,
            'name': station.name,
            'username': station.username,
            'is_active': station.is_active,
            'db_status': station.status,
        },
        'redis': {
            'connected': redis_ok,
            'key': utils._telemetry_key(station.id),
            'has_data': raw is not None,
            'ts_age_seconds': ts_age,
            'telemetry': telemetry,
        },
    })


if __name__ == '__main__':
    from Flask_Web import db
    with app.app_context():
        db.create_all()
    from waitress import serve
    serve(app, host='0.0.0.0', port=8888)
