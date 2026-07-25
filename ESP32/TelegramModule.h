<<<<<<< HEAD
#ifndef TELEGRAM_MODULE_H
#define TELEGRAM_MODULE_H

bool tgDrainPendingOnBoot = true;

static void parseTgMix(const String &mix) {
  memset(tg_bot_token, 0, sizeof(tg_bot_token));
  memset(tg_chat_id, 0, sizeof(tg_chat_id));
  tgEnabled = false;

  String raw = mix;
  raw.trim();
  if (raw.length() == 0) return;

  int p = raw.indexOf(':');
  if (p <= 0) {
    p = raw.indexOf('|');
  }
  if (p <= 0) {
    p = raw.indexOf(',');
  }
  if (p <= 0) return;

  String chat = raw.substring(0, p);
  String token = raw.substring(p + 1);
  chat.trim();
  token.trim();
  if (chat.length() == 0 || token.length() == 0) return;

  // Telegram bot token always contains ':' (e.g. 123456:ABC...).
  if (token.indexOf(':') < 0) return;

  chat.toCharArray(tg_chat_id, sizeof(tg_chat_id));
  token.toCharArray(tg_bot_token, sizeof(tg_bot_token));

  bot.updateToken(String(tg_bot_token));
  tgEnabled = true;
  tgDrainPendingOnBoot = true;
}

static bool tgAuthorized(const String &chat_id) {
  if (!tgEnabled) return false;
  if (tg_chat_id[0] == '\0') return false;
  return chat_id == String(tg_chat_id);
}

static void tgSend(const String &msg) {
  if (!feat_tg) return;
  if (!tgEnabled) return;
  if (WiFi.status() != WL_CONNECTED) return;
  if (tg_chat_id[0] == '\0') return;
  bot.sendMessage(String(tg_chat_id), msg, "");
}

static String stateToText() {
  if (!feat_ds && !feat_mlx && !feat_dht && !feat_pzem) {
    return "Đang hẹn giờ";
  }
  switch (chargeState) {
    case CS_PROBING:       return "Đang kiểm tra (đo công suất)";
    case CS_CHARGING:      return "Đang sạc";
    case CS_STOP_TEMP:     return "Ngắt: quá nhiệt";
    case CS_STOP_FULL:     return "Ngắt: pin đầy";
    case CS_STOP_HUMID:    return "Ngắt: độ ẩm cao";
    case CS_STOP_TIMEOUT:  return "Ngắt: quá thời gian";
    case CS_STOP_POWER:    return "Ngắt: quá công suất";
    case CS_SENSOR_ERROR:  return "Ngắt: lỗi cảm biến";
    default:               return "Chờ sạc";
  }
}

static void sendConfigToTg() {
  String msg;
  msg.reserve(380);
  msg += "Cấu hình hiện tại:\n";
  msg += "wait=" + String(wait_minutes) + " phút\n";
  msg += "xac_nhan_day=" + String(measure_seconds) + " giây\n";
  msg += "power_min=" + String(min_power_w, 1) + " W\n";
  msg += "power_max=" + String(max_power_w, 1) + " W\n";
  msg += "nguong_day=" + String(full_power_threshold, 1) + " W\n";
  msg += "canh_bao_DS=" + String(warn_temp_ds, 1) + " C\n";
  msg += "ngat_DS=" + String(max_temp_ds, 1) + " C\n";
  msg += "canh_bao_MLX=" + String(warn_temp_mlx, 1) + " C\n";
  msg += "ngat_MLX=" + String(max_temp_mlx, 1) + " C\n";
  msg += "ngat_ENV=" + String(max_temp_env, 1) + " C\n";
  msg += "h_max=" + String(max_humidity, 0) + " %\n";
  msg += "max_gio=" + String(max_charge_hours) + " giờ\n";
  msg += "state=" + stateToText();
  tgSend(msg);
}

static void sendChargeTimeToTg() {
  unsigned long ms = chargeElapsedMs();
  float minf = ms / 60000.0f;
  unsigned long sec = (ms / 1000UL) % 60UL;

  String msg;
  msg.reserve(180);
  msg += charging ? "Đang sạc\n" : "Không sạc\n";
  msg += "Đã sạc: " + String(minf, 1) + " phút (" + String(sec) + " s)";
  tgSend(msg);
}

static String fmtFloat(float v, uint8_t digits=1, bool enabled=true) {
  if (!enabled) return "N/A";
  if (isnan(v)) return "--";
  return String(v, (unsigned int)digits);
}

static bool parseArgAfterSpace(const String &text, String &outArg) {
  int sp = text.indexOf(' ');
  if (sp < 0) return false;
  outArg = text.substring(sp + 1);
  outArg.trim();
  return outArg.length() > 0;
}

static void telegramPoll() {
  if (!feat_tg) return;
  if (!tgEnabled) return;
  if (WiFi.status() != WL_CONNECTED) return;

  if (millis() - lastTgCheck < TG_POLL_MS) return;
  lastTgCheck = millis();

  if (tgDrainPendingOnBoot) {
    // Drop stale commands from downtime to avoid handling old /reboot again.
    for (uint8_t i = 0; i < 3; i++) {
      int pending = bot.getUpdates(bot.last_message_received + 1);
      if (pending <= 0) break;
    }
    tgDrainPendingOnBoot = false;
    return;
  }

  int n = bot.getUpdates(bot.last_message_received + 1);
  for (int i = 0; i < n; i++) {
    String text = bot.messages[i].text; text.trim();
    String chat_id = bot.messages[i].chat_id;

    if (!tgAuthorized(chat_id)) continue;

    if (text == "/start" || text == "/help" || text == "/tro_giup") {
      String msg;
      msg.reserve(600);
      msg += "ESP32 Smart Charger\n\n";
      msg += "Lệnh nhanh (Tiếng Việt):\n";
      msg += "/xem           - Xem trạng thái và số liệu\n";
      msg += "/sac_ngay      - Bật sạc ngay (bỏ qua chờ)\n";
      msg += "/doi_cho       - Chế độ chờ (tự động theo thời gian chờ)\n";
      msg += "/dung_sac      - Dừng sạc và tắt tự động\n";
      msg += "/xem_cauhinh   - Xem cấu hình\n";
      msg += "/xem_thoigian  - Thời gian đã sạc\n";
      msg += "/khoi_dong_lai - Khởi động lại ESP32\n";
      msg += "/khoi_phuc     - Khôi phục mặc định và khởi động lại\n";
      msg += "/he_thong      - Xem thông tin hệ thống (IP, RSSI, flags)\n";
      msg += "/wifi [S] [P]  - Thiết lập WiFi SSID và PASSWORD\n";
      msg += "/ap [S] [P]    - Thiết lập AP SSID và PASSWORD\n";
      msg += "/token [T]     - Thiết lập API Station Token\n";
      msg += "\nCảm biến & Tính năng:\n";
      msg += "/bat_ds / /tat_ds - Bật/Tắt cảm biến DS18B20\n";
      msg += "/bat_mlx / /tat_mlx - Bật/Tắt cảm biến MLX90614\n";
      msg += "/bat_dht / /tat_dht - Bật/Tắt cảm biến DHT\n";
      msg += "/bat_pzem / /tat_pzem - Bật/Tắt cảm biến PZEM\n";
      msg += "/bat_telegram / /tat_telegram - Bật/Tắt Telegram\n";
      msg += "/bat_cloud / /tat_cloud - Bật/Tắt đồng bộ Cloud\n";
      msg += "/bat_ap / /tat_ap - Bật/Tắt phát AP WiFi\n";
      msg += "/bat_ap_luon / /tat_ap_luon - Bật/Tắt luôn phát AP WiFi (không giới hạn 5 phút)\n";

      msg += "Cài đặt nhanh (ví dụ):\n";
      msg += "/dat_cho 60       (phút chờ trước khi sạc)\n";
      msg += "/dat_p_day 10     (W - công suất min/ngưỡng đầy, 0=tắt)\n";
      msg += "/dat_p_max 500    (W - công suất tối đa, 0=tắt)\n";
      msg += "/dat_xacnhan 300  (giây - xác nhận đầy, 0=tắt)\n";
      msg += "/dat_cb_ds 45     (C - cảnh báo sớm DS18B20)\n";
      msg += "/dat_t_ds 55      (C - ngắt DS18B20)\n";
      msg += "/dat_t_mlx 55     (C - ngắt MLX)\n";
      msg += "/dat_t_mt 45      (C - ngắt môi trường)\n";
      msg += "/dat_doam 85      (% - ngắt độ ẩm)\n";
      msg += "/dat_gio_max 10   (giờ - giới hạn sạc)";
      tgSend(msg);

    } else if (text == "/status" || text == "/xem") {
      SensorFrame s = readSensorFrame();

      String msg;
      msg.reserve(420);
      msg += "Trạng thái: " + stateToText();
      msg += "\nRelay: " + String(digitalRead(relay_pin) ? "ON" : "OFF");
      msg += "\n\nNhiệt bộ sạc (DS): " + fmtFloat(s.tds, 1, feat_ds) + " C";
      msg += "\nNhiệt vỏ bình (MLX): " + fmtFloat(s.tmlx, 1, feat_mlx) + " C";
      msg += "\nNhiệt môi trường: " + fmtFloat(s.tenv, 1, feat_dht) + " C";
      msg += "\nĐộ ẩm: " + fmtFloat(s.hum, 0, feat_dht) + " %";
      msg += "\n\nĐiện áp: " + fmtFloat(s.voltage, 1, feat_pzem) + " V";
      msg += "\nDòng: " + fmtFloat(s.current, 2, feat_pzem) + " A";
      msg += "\nCông suất: " + fmtFloat(s.pwr, 1, feat_pzem) + " W";
      msg += "\n\nĐã sạc: " + String(chargeElapsedMs() / 60000.0f, 1) + " phút";
      tgSend(msg);

    } else if (text == "/sac_lien" || text == "/bat" || text == "/on" || text == "/sac_ngay") {
      startChargingNow("TG");
      tgSend("Đã bật sạc ngay");

    } else if (text == "/sac_cho" || text == "/cho" || text == "/wait" || text == "/doi_cho") {
      setWaitMode("TG");
      tgSend("Đã chuyển sang CHỜ sạc (tự động)");

    } else if (text == "/stop" || text == "/dung" || text == "/stop_sac" || text == "/tat" || text == "/off" || text == "/dung_sac") {
      stopAll("TG");
      tgSend("Đã dừng sạc và tắt tự động");

    } else if (text == "/get_config" || text == "/config" || text == "/xem_cauhinh") {
      sendConfigToTg();

    } else if (text == "/get_time" || text == "/time" || text == "/xem_thoigian") {
      sendChargeTimeToTg();

    } else if (text.startsWith("/set_wait") || text.startsWith("/dat_cho")) {
      String arg;
      if (parseArgAfterSpace(text, arg)) {
        long v = arg.toInt();
        if (v < 0) v = 0;
        if (v > 65535) v = 65535;
        wait_minutes = (uint16_t)v;
        prefs.putInt("wait", wait_minutes);
        tgSend("Đã đặt thời gian chờ=" + String(wait_minutes) + " phút");
      }

    } else if (text.startsWith("/set_hold") || text.startsWith("/set_measure") || text.startsWith("/dat_xacnhan")) {
      String arg;
      if (parseArgAfterSpace(text, arg)) {
        long v = arg.toInt();
        if (v < 0) v = 0;
        if (v > 3600) v = 3600;
        measure_seconds = (uint16_t)v;
        prefs.putInt("measure", measure_seconds);
        tgSend("Đã đặt xác nhận đầy=" + String(measure_seconds) + " giây (0=tắt)");
      }

    } else if (text.startsWith("/set_twarn") || text.startsWith("/dat_cb_ds")) {
      String arg;
      if (parseArgAfterSpace(text, arg)) {
        warn_temp_ds = clampNonNegative(arg.toFloat());
        prefs.putFloat("tw_ds", warn_temp_ds);
        tgSend("Đã đặt cảnh báo sớm (DS)=" + String(warn_temp_ds, 1) + " C (0=tắt)");
      }

    } else if (text.startsWith("/set_maxh") || text.startsWith("/dat_gio_max")) {
      String arg;
      if (parseArgAfterSpace(text, arg)) {
        long v = arg.toInt();
        if (v < 0) v = 0;
        if (v > 48) v = 48;
        max_charge_hours = (uint16_t)v;
        prefs.putInt("max_h", max_charge_hours);
        tgSend("Đã đặt giới hạn sạc=" + String(max_charge_hours) + " giờ (0=tắt giới hạn)");
      }

    } else if (text.startsWith("/set_full") || text.startsWith("/dat_p_day")) {
      String arg;
      if (parseArgAfterSpace(text, arg)) {
        min_power_w = arg.toFloat();
        if (min_power_w < 0.0f) min_power_w = 0.0f;
        full_power_threshold = min_power_w;
        prefs.putFloat("p_min", min_power_w);
        prefs.putFloat("full", full_power_threshold);
        tgSend("Đã đặt công suất min/ngưỡng đầy=" + String(full_power_threshold, 1) + " W (0=tắt)");
      }

    } else if (text.startsWith("/set_pmax") || text.startsWith("/dat_p_max")) {
      String arg;
      if (parseArgAfterSpace(text, arg)) {
        max_power_w = arg.toFloat();
        if (max_power_w < 0.0f) max_power_w = 0.0f;
        prefs.putFloat("p_max", max_power_w);
        tgSend("Đã đặt công suất tối đa=" + String(max_power_w, 1) + " W (0=tắt)");
      }

    } else if (text.startsWith("/set_tds") || text.startsWith("/set_tmax") || text.startsWith("/dat_t_ds")) {
      String arg;
      if (parseArgAfterSpace(text, arg)) {
        max_temp_ds = clampNonNegative(arg.toFloat());
        prefs.putFloat("t_ds", max_temp_ds);
        tgSend("Đã đặt nhiệt NGẮT (DS)=" + String(max_temp_ds, 1) + " C (0=tắt)");
      }

    } else if (text.startsWith("/set_tmlx") || text.startsWith("/dat_t_mlx")) {
      String arg;
      if (parseArgAfterSpace(text, arg)) {
        max_temp_mlx = clampNonNegative(arg.toFloat());
        prefs.putFloat("t_mlx", max_temp_mlx);
        tgSend("Đã đặt nhiệt NGẮT (MLX)=" + String(max_temp_mlx, 1) + " C (0=tắt)");
      }

    } else if (text.startsWith("/set_tenv") || text.startsWith("/dat_t_mt")) {
      String arg;
      if (parseArgAfterSpace(text, arg)) {
        max_temp_env = clampNonNegative(arg.toFloat());
        prefs.putFloat("t_env", max_temp_env);
        tgSend("Đã đặt nhiệt NGẮT (ENV)=" + String(max_temp_env, 1) + " C (0=tắt)");
      }

    } else if (text.startsWith("/set_hum") || text.startsWith("/dat_doam")) {
      String arg;
      if (parseArgAfterSpace(text, arg)) {
        max_humidity = clampNonNegative(arg.toFloat());
        prefs.putFloat("h_max", max_humidity);
        tgSend("Đã đặt độ ẩm NGẮT=" + String(max_humidity, 0) + " %");
      }

    } else if (text == "/reboot" || text == "/restart" || text == "/khoi_dong_lai") {
      tgSend("Đang khởi động lại ESP32...");
      delay(250);
      ESP.restart();

    } else if (text == "/reset_config" || text == "/khoi_phuc") {
      tgSend("Đang khôi phục mặc định...\nThiết bị sẽ khởi động lại");
      delay(250);
      prefs.clear();
      delay(150);
      ESP.restart();

    } else if (text.startsWith("/wifi")) {
      String arg;
      if (parseArgAfterSpace(text, arg)) {
        int sp = arg.indexOf(' ');
        if (sp > 0) {
          String s = arg.substring(0, sp);
          String p = arg.substring(sp + 1);
          s.trim(); p.trim();
          prefs.putString("ssid", s);
          prefs.putString("pass", p);
          tgSend("Đã lưu WiFi SSID: " + s + ". Khởi động lại thiết bị...");
          delay(1000);
          ESP.restart();
        } else {
          tgSend("Cú pháp: /wifi [SSID] [PASSWORD]");
        }
      } else {
        tgSend("Cú pháp: /wifi [SSID] [PASSWORD]");
      }

    } else if (text.startsWith("/ap")) {
      String arg;
      if (parseArgAfterSpace(text, arg)) {
        int sp = arg.indexOf(' ');
        if (sp > 0) {
          String s = arg.substring(0, sp);
          String p = arg.substring(sp + 1);
          s.trim(); p.trim();
          if (p.length() >= 8) {
            prefs.putString("ap_ssid", s);
            prefs.putString("ap_pass", p);
            tgSend("Đã lưu AP SSID: " + s + ". Khởi động lại thiết bị...");
            delay(1000);
            ESP.restart();
          } else {
            tgSend("Mật khẩu AP phải >= 8 ký tự!");
          }
        } else {
          tgSend("Cú pháp: /ap [SSID] [PASSWORD]");
        }
      } else {
        tgSend("Cú pháp: /ap [SSID] [PASSWORD]");
      }

    } else if (text.startsWith("/token")) {
      String arg;
      if (parseArgAfterSpace(text, arg)) {
        prefs.putString("api_key", arg);
        tgSend("Đã lưu Station Token: " + arg + ". Khởi động lại thiết bị...");
        delay(1000);
        ESP.restart();
      } else {
        tgSend("Cú pháp: /token [STATION_TOKEN]");
      }

    } else if (text == "/he_thong" || text == "/system") {
      String msg;
      msg.reserve(320);
      msg += "Hệ thống Smart Charger:\n";
      msg += "IP địa phương: " + WiFi.localIP().toString() + "\n";
      msg += "IP SoftAP: " + WiFi.softAPIP().toString() + "\n";
      msg += "WiFi Status: " + String(WiFi.status() == WL_CONNECTED ? "Đã kết nối" : "Chưa kết nối") + "\n";
      msg += "WiFi RSSI: " + String(WiFi.RSSI()) + " dBm\n";
      msg += "Tính năng active:\n";
      msg += "- DS: " + String(feat_ds ? "BẬT" : "TẮT") + "\n";
      msg += "- MLX: " + String(feat_mlx ? "BẬT" : "TẮT") + "\n";
      msg += "- DHT: " + String(feat_dht ? "BẬT" : "TẮT") + "\n";
      msg += "- PZEM: " + String(feat_pzem ? "BẬT" : "TẮT") + "\n";
      msg += "- Telegram: " + String(feat_tg ? "BẬT" : "TẮT") + "\n";
      msg += "- Cloud: " + String(feat_cloud ? "BẬT" : "TẮT") + "\n";
      msg += "- Phát AP: " + String(feat_ap_on ? "BẬT" : "TẮT") + "\n";
      msg += "- Luôn phát AP: " + String(feat_ap_always ? "BẬT" : "TẮT") + "\n";
      tgSend(msg);

    } else if (text == "/bat_ds") {
      feat_ds = true;
      prefs.putBool("feat_ds", true);
      tgSend("Đã BẬT cảm biến DS18B20");
    } else if (text == "/tat_ds") {
      feat_ds = false;
      prefs.putBool("feat_ds", false);
      tgSend("Đã TẮT cảm biến DS18B20");
    } else if (text == "/bat_mlx") {
      feat_mlx = true;
      prefs.putBool("feat_mlx", true);
      tgSend("Đã BẬT cảm biến MLX90614");
    } else if (text == "/tat_mlx") {
      feat_mlx = false;
      prefs.putBool("feat_mlx", false);
      tgSend("Đã TẮT cảm biến MLX90614");
    } else if (text == "/bat_dht") {
      feat_dht = true;
      prefs.putBool("feat_dht", true);
      tgSend("Đã BẬT cảm biến DHT");
    } else if (text == "/tat_dht") {
      feat_dht = false;
      prefs.putBool("feat_dht", false);
      tgSend("Đã TẮT cảm biến DHT");
    } else if (text == "/bat_pzem") {
      feat_pzem = true;
      prefs.putBool("feat_pzem", true);
      tgSend("Đã BẬT cảm biến PZEM");
    } else if (text == "/tat_pzem") {
      feat_pzem = false;
      prefs.putBool("feat_pzem", false);
      tgSend("Đã TẮT cảm biến PZEM");
    } else if (text == "/bat_telegram") {
      feat_tg = true;
      prefs.putBool("feat_tg", true);
      tgSend("Đã BẬT Telegram");
    } else if (text == "/tat_telegram") {
      tgSend("Đang TẮT Telegram...");
      delay(200);
      feat_tg = false;
      prefs.putBool("feat_tg", false);
    } else if (text == "/bat_cloud") {
      feat_cloud = true;
      prefs.putBool("feat_cloud", true);
      tgSend("Đã BẬT đồng bộ Cloud");
    } else if (text == "/tat_cloud") {
      feat_cloud = false;
      prefs.putBool("feat_cloud", false);
      tgSend("Đã TẮT đồng bộ Cloud");
    } else if (text == "/bat_ap") {
      feat_ap_on = true;
      prefs.putBool("feat_ap_on", true);
      tgSend("Đã BẬT phát WiFi AP. Hãy reboot để áp dụng.");
    } else if (text == "/tat_ap") {
      feat_ap_on = false;
      prefs.putBool("feat_ap_on", false);
      tgSend("Đã TẮT phát WiFi AP. Hãy reboot để áp dụng.");
    } else if (text == "/bat_ap_luon") {
      feat_ap_always = true;
      prefs.putBool("feat_ap_alw", true);
      tgSend("Đã BẬT luôn phát AP WiFi (không giới hạn 5 phút).");
    } else if (text == "/tat_ap_luon") {
      feat_ap_always = false;
      prefs.putBool("feat_ap_alw", false);
      tgSend("Đã TẮT luôn phát AP WiFi (AP tự tắt sau 5 phút).");

    } else {
      tgSend("Lệnh không hợp lệ. Gửi /help hoặc /tro_giup để xem danh sách lệnh.");
    }
  }
}

static void telegramAlertTask() {
  if (!feat_tg) return;
  if (!tgEnabled) return;
  if (WiFi.status() != WL_CONNECTED) return;

  static unsigned long lastSample = 0;
  if (millis() - lastSample < 5000) return;
  lastSample = millis();

  SensorFrame s = readSensorFrame();
  float tds = s.tds;
  float tmlx = s.tmlx;
  float tenv = s.tenv;
  float hum = s.hum;

  bool dsOk = s.dsOk;
  bool mlxOk = s.mlxOk;

  bool warnTemp = (dsControlEnabled() && warn_temp_ds > 0.0f && dsOk && tds >= warn_temp_ds && tds < max_temp_ds) ||
                  (mlxControlEnabled() && warn_temp_mlx > 0.0f && mlxOk && tmlx >= warn_temp_mlx && tmlx < max_temp_mlx) ||
                  (envControlEnabled() && warn_temp_env > 0.0f && !isnan(tenv) && tenv >= warn_temp_env && tenv < max_temp_env);

  bool hot = isHotNow(s);
  bool humid = isHumidNow(s);

  unsigned long now = millis();
  static bool tgWarnSent = false;

  if (hot && !tgAlertSentTemp && (now - lastTgAlertMs >= TG_ALERT_COOLDOWN_MS)) {
    String msg;
    msg.reserve(320);
    msg += "CẢNH BÁO: QUÁ NHIỆT\n";
    msg += "Trạng thái: " + stateToText();
    msg += "\nDS: " + fmtFloat(tds,1) + " C (ngắt " + String(max_temp_ds,1) + ")";
    msg += "\nMLX: " + fmtFloat(tmlx,1) + " C (ngắt " + String(max_temp_mlx,1) + ")";
    msg += "\nENV: " + fmtFloat(tenv,1) + " C (ngắt " + String(max_temp_env,1) + ")";
    tgSend(msg);
    tgAlertSentTemp = true;
    tgWarnSent = true;
    lastTgAlertMs = now;
  }
  if (!hot) tgAlertSentTemp = false;

  if (warnTemp && !tgWarnSent && !hot && (now - lastTgAlertMs >= TG_ALERT_COOLDOWN_MS)) {
    String msg;
    msg.reserve(420);
    msg += "CẢNH BÁO SỚM: Bộ sạc đang nóng dần (chưa đến mức ngắt)\n";
    msg += "\nNhiệt bộ sạc (DS): " + fmtFloat(tds,1) + " C";
    msg += "\n- Mức cảnh báo: " + String(warn_temp_ds,1) + " C";
    msg += "\n- Mức ngắt: " + String(max_temp_ds,1) + " C";
    msg += "\n\nNhiệt vỏ bình (MLX): " + fmtFloat(tmlx,1) + " C";
    msg += "\nNhiệt môi trường (ENV): " + fmtFloat(tenv,1) + " C";
    msg += "\n\nGợi ý: Kiểm tra tải sạc/quạt tản nhiệt để tránh bị ngắt.";
    tgSend(msg);
    tgWarnSent = true;
    lastTgAlertMs = now;
  }
  if (!warnTemp) tgWarnSent = false;

  if (humid && !tgAlertSentHum && (now - lastTgAlertMs >= TG_ALERT_COOLDOWN_MS)) {
    String msg;
    msg.reserve(220);
    msg += "CẢNH BÁO: ĐỘ ẨM CAO\n";
    msg += "H: " + fmtFloat(hum,0) + " % (ngắt " + String(max_humidity,0) + ")";
    msg += "\nTrạng thái: " + stateToText();
    tgSend(msg);
    tgAlertSentHum = true;
    lastTgAlertMs = now;
  }
  if (!humid) tgAlertSentHum = false;
}

#endif
=======
#ifndef TELEGRAM_MODULE_H
#define TELEGRAM_MODULE_H

bool tgDrainPendingOnBoot = true;

static void parseTgMix(const String &mix) {
  memset(tg_bot_token, 0, sizeof(tg_bot_token));
  memset(tg_chat_id, 0, sizeof(tg_chat_id));
  tgEnabled = false;

  String raw = mix;
  raw.trim();
  if (raw.length() == 0) return;

  int p = raw.indexOf(':');
  if (p <= 0) {
    p = raw.indexOf('|');
  }
  if (p <= 0) {
    p = raw.indexOf(',');
  }
  if (p <= 0) return;

  String chat = raw.substring(0, p);
  String token = raw.substring(p + 1);
  chat.trim();
  token.trim();
  if (chat.length() == 0 || token.length() == 0) return;

  // Telegram bot token always contains ':' (e.g. 123456:ABC...).
  if (token.indexOf(':') < 0) return;

  chat.toCharArray(tg_chat_id, sizeof(tg_chat_id));
  token.toCharArray(tg_bot_token, sizeof(tg_bot_token));

  bot.updateToken(String(tg_bot_token));
  tgEnabled = true;
  tgDrainPendingOnBoot = true;
}

static bool tgAuthorized(const String &chat_id) {
  if (!tgEnabled) return false;
  if (tg_chat_id[0] == '\0') return false;
  return chat_id == String(tg_chat_id);
}

static void tgSend(const String &msg) {
  if (!feat_tg) return;
  if (!tgEnabled) return;
  if (WiFi.status() != WL_CONNECTED) return;
  if (tg_chat_id[0] == '\0') return;
  bot.sendMessage(String(tg_chat_id), msg, "");
}

static String stateToText() {
  if (!feat_ds && !feat_mlx && !feat_dht && !feat_pzem) {
    return "Đang hẹn giờ";
  }
  switch (chargeState) {
    case CS_PROBING:       return "Đang kiểm tra (đo công suất)";
    case CS_CHARGING:      return "Đang sạc";
    case CS_STOP_TEMP:     return "Ngắt: quá nhiệt";
    case CS_STOP_FULL:     return "Ngắt: pin đầy";
    case CS_STOP_HUMID:    return "Ngắt: độ ẩm cao";
    case CS_STOP_TIMEOUT:  return "Ngắt: quá thời gian";
    case CS_STOP_POWER:    return "Ngắt: quá công suất";
    case CS_SENSOR_ERROR:  return "Ngắt: lỗi cảm biến";
    default:               return "Chờ sạc";
  }
}

static void sendConfigToTg() {
  String msg;
  msg.reserve(380);
  msg += "Cấu hình hiện tại:\n";
  msg += "wait=" + String(wait_minutes) + " phút\n";
  msg += "xac_nhan_day=" + String(measure_seconds) + " giây\n";
  msg += "power_min=" + String(min_power_w, 1) + " W\n";
  msg += "power_max=" + String(max_power_w, 1) + " W\n";
  msg += "nguong_day=" + String(full_power_threshold, 1) + " W\n";
  msg += "canh_bao_DS=" + String(warn_temp_ds, 1) + " C\n";
  msg += "ngat_DS=" + String(max_temp_ds, 1) + " C\n";
  msg += "canh_bao_MLX=" + String(warn_temp_mlx, 1) + " C\n";
  msg += "ngat_MLX=" + String(max_temp_mlx, 1) + " C\n";
  msg += "ngat_ENV=" + String(max_temp_env, 1) + " C\n";
  msg += "h_max=" + String(max_humidity, 0) + " %\n";
  msg += "max_gio=" + String(max_charge_hours) + " giờ\n";
  msg += "state=" + stateToText();
  tgSend(msg);
}

static void sendChargeTimeToTg() {
  unsigned long ms = chargeElapsedMs();
  float minf = ms / 60000.0f;
  unsigned long sec = (ms / 1000UL) % 60UL;

  String msg;
  msg.reserve(180);
  msg += charging ? "Đang sạc\n" : "Không sạc\n";
  msg += "Đã sạc: " + String(minf, 1) + " phút (" + String(sec) + " s)";
  tgSend(msg);
}

static String fmtFloat(float v, uint8_t digits=1, bool enabled=true) {
  if (!enabled) return "N/A";
  if (isnan(v)) return "--";
  return String(v, (unsigned int)digits);
}

static bool parseArgAfterSpace(const String &text, String &outArg) {
  int sp = text.indexOf(' ');
  if (sp < 0) return false;
  outArg = text.substring(sp + 1);
  outArg.trim();
  return outArg.length() > 0;
}

static void telegramPoll() {
  if (!feat_tg) return;
  if (!tgEnabled) return;
  if (WiFi.status() != WL_CONNECTED) return;

  if (millis() - lastTgCheck < TG_POLL_MS) return;
  lastTgCheck = millis();

  if (tgDrainPendingOnBoot) {
    // Drop stale commands from downtime to avoid handling old /reboot again.
    for (uint8_t i = 0; i < 3; i++) {
      int pending = bot.getUpdates(bot.last_message_received + 1);
      if (pending <= 0) break;
    }
    tgDrainPendingOnBoot = false;
    return;
  }

  int n = bot.getUpdates(bot.last_message_received + 1);
  for (int i = 0; i < n; i++) {
    String text = bot.messages[i].text; text.trim();
    String chat_id = bot.messages[i].chat_id;

    if (!tgAuthorized(chat_id)) continue;

    if (text == "/start" || text == "/help" || text == "/tro_giup") {
      String msg;
      msg.reserve(600);
      msg += "ESP32 Smart Charger\n\n";
      msg += "Lệnh nhanh (Tiếng Việt):\n";
      msg += "/xem           - Xem trạng thái và số liệu\n";
      msg += "/sac_ngay      - Bật sạc ngay (bỏ qua chờ)\n";
      msg += "/doi_cho       - Chế độ chờ (tự động theo thời gian chờ)\n";
      msg += "/dung_sac      - Dừng sạc và tắt tự động\n";
      msg += "/xem_cauhinh   - Xem cấu hình\n";
      msg += "/xem_thoigian  - Thời gian đã sạc\n";
      msg += "/khoi_dong_lai - Khởi động lại ESP32\n";
      msg += "/khoi_phuc     - Khôi phục mặc định và khởi động lại\n";
      msg += "/he_thong      - Xem thông tin hệ thống (IP, RSSI, flags)\n";
      msg += "/wifi [S] [P]  - Thiết lập WiFi SSID và PASSWORD\n";
      msg += "/ap [S] [P]    - Thiết lập AP SSID và PASSWORD\n";
      msg += "/token [T]     - Thiết lập API Station Token\n";
      msg += "\nCảm biến & Tính năng:\n";
      msg += "/bat_ds / /tat_ds - Bật/Tắt cảm biến DS18B20\n";
      msg += "/bat_mlx / /tat_mlx - Bật/Tắt cảm biến MLX90614\n";
      msg += "/bat_dht / /tat_dht - Bật/Tắt cảm biến DHT\n";
      msg += "/bat_pzem / /tat_pzem - Bật/Tắt cảm biến PZEM\n";
      msg += "/bat_telegram / /tat_telegram - Bật/Tắt Telegram\n";
      msg += "/bat_cloud / /tat_cloud - Bật/Tắt đồng bộ Cloud\n";
      msg += "/bat_ap / /tat_ap - Bật/Tắt phát AP WiFi\n";
      msg += "/bat_ap_luon / /tat_ap_luon - Bật/Tắt luôn phát AP WiFi (không giới hạn 5 phút)\n";

      msg += "Cài đặt nhanh (ví dụ):\n";
      msg += "/dat_cho 60       (phút chờ trước khi sạc)\n";
      msg += "/dat_p_day 10     (W - công suất min/ngưỡng đầy, 0=tắt)\n";
      msg += "/dat_p_max 500    (W - công suất tối đa, 0=tắt)\n";
      msg += "/dat_xacnhan 300  (giây - xác nhận đầy, 0=tắt)\n";
      msg += "/dat_cb_ds 45     (C - cảnh báo sớm DS18B20)\n";
      msg += "/dat_t_ds 55      (C - ngắt DS18B20)\n";
      msg += "/dat_t_mlx 55     (C - ngắt MLX)\n";
      msg += "/dat_t_mt 45      (C - ngắt môi trường)\n";
      msg += "/dat_doam 85      (% - ngắt độ ẩm)\n";
      msg += "/dat_gio_max 10   (giờ - giới hạn sạc)";
      tgSend(msg);

    } else if (text == "/status" || text == "/xem") {
      SensorFrame s = readSensorFrame();

      String msg;
      msg.reserve(420);
      msg += "Trạng thái: " + stateToText();
      msg += "\nRelay: " + String(digitalRead(relay_pin) ? "ON" : "OFF");
      msg += "\n\nNhiệt bộ sạc (DS): " + fmtFloat(s.tds, 1, feat_ds) + " C";
      msg += "\nNhiệt vỏ bình (MLX): " + fmtFloat(s.tmlx, 1, feat_mlx) + " C";
      msg += "\nNhiệt môi trường: " + fmtFloat(s.tenv, 1, feat_dht) + " C";
      msg += "\nĐộ ẩm: " + fmtFloat(s.hum, 0, feat_dht) + " %";
      msg += "\n\nĐiện áp: " + fmtFloat(s.voltage, 1, feat_pzem) + " V";
      msg += "\nDòng: " + fmtFloat(s.current, 2, feat_pzem) + " A";
      msg += "\nCông suất: " + fmtFloat(s.pwr, 1, feat_pzem) + " W";
      msg += "\n\nĐã sạc: " + String(chargeElapsedMs() / 60000.0f, 1) + " phút";
      tgSend(msg);

    } else if (text == "/sac_lien" || text == "/bat" || text == "/on" || text == "/sac_ngay") {
      startChargingNow("TG");
      tgSend("Đã bật sạc ngay");

    } else if (text == "/sac_cho" || text == "/cho" || text == "/wait" || text == "/doi_cho") {
      setWaitMode("TG");
      tgSend("Đã chuyển sang CHỜ sạc (tự động)");

    } else if (text == "/stop" || text == "/dung" || text == "/stop_sac" || text == "/tat" || text == "/off" || text == "/dung_sac") {
      stopAll("TG");
      tgSend("Đã dừng sạc và tắt tự động");

    } else if (text == "/get_config" || text == "/config" || text == "/xem_cauhinh") {
      sendConfigToTg();

    } else if (text == "/get_time" || text == "/time" || text == "/xem_thoigian") {
      sendChargeTimeToTg();

    } else if (text.startsWith("/set_wait") || text.startsWith("/dat_cho")) {
      String arg;
      if (parseArgAfterSpace(text, arg)) {
        long v = arg.toInt();
        if (v < 0) v = 0;
        if (v > 65535) v = 65535;
        wait_minutes = (uint16_t)v;
        prefs.putInt("wait", wait_minutes);
        tgSend("Đã đặt thời gian chờ=" + String(wait_minutes) + " phút");
      }

    } else if (text.startsWith("/set_hold") || text.startsWith("/set_measure") || text.startsWith("/dat_xacnhan")) {
      String arg;
      if (parseArgAfterSpace(text, arg)) {
        long v = arg.toInt();
        if (v < 0) v = 0;
        if (v > 3600) v = 3600;
        measure_seconds = (uint16_t)v;
        prefs.putInt("measure", measure_seconds);
        tgSend("Đã đặt xác nhận đầy=" + String(measure_seconds) + " giây (0=tắt)");
      }

    } else if (text.startsWith("/set_twarn") || text.startsWith("/dat_cb_ds")) {
      String arg;
      if (parseArgAfterSpace(text, arg)) {
        warn_temp_ds = clampNonNegative(arg.toFloat());
        prefs.putFloat("tw_ds", warn_temp_ds);
        tgSend("Đã đặt cảnh báo sớm (DS)=" + String(warn_temp_ds, 1) + " C (0=tắt)");
      }

    } else if (text.startsWith("/set_maxh") || text.startsWith("/dat_gio_max")) {
      String arg;
      if (parseArgAfterSpace(text, arg)) {
        long v = arg.toInt();
        if (v < 0) v = 0;
        if (v > 48) v = 48;
        max_charge_hours = (uint16_t)v;
        prefs.putInt("max_h", max_charge_hours);
        tgSend("Đã đặt giới hạn sạc=" + String(max_charge_hours) + " giờ (0=tắt giới hạn)");
      }

    } else if (text.startsWith("/set_full") || text.startsWith("/dat_p_day")) {
      String arg;
      if (parseArgAfterSpace(text, arg)) {
        min_power_w = arg.toFloat();
        if (min_power_w < 0.0f) min_power_w = 0.0f;
        full_power_threshold = min_power_w;
        prefs.putFloat("p_min", min_power_w);
        prefs.putFloat("full", full_power_threshold);
        tgSend("Đã đặt công suất min/ngưỡng đầy=" + String(full_power_threshold, 1) + " W (0=tắt)");
      }

    } else if (text.startsWith("/set_pmax") || text.startsWith("/dat_p_max")) {
      String arg;
      if (parseArgAfterSpace(text, arg)) {
        max_power_w = arg.toFloat();
        if (max_power_w < 0.0f) max_power_w = 0.0f;
        prefs.putFloat("p_max", max_power_w);
        tgSend("Đã đặt công suất tối đa=" + String(max_power_w, 1) + " W (0=tắt)");
      }

    } else if (text.startsWith("/set_tds") || text.startsWith("/set_tmax") || text.startsWith("/dat_t_ds")) {
      String arg;
      if (parseArgAfterSpace(text, arg)) {
        max_temp_ds = clampNonNegative(arg.toFloat());
        prefs.putFloat("t_ds", max_temp_ds);
        tgSend("Đã đặt nhiệt NGẮT (DS)=" + String(max_temp_ds, 1) + " C (0=tắt)");
      }

    } else if (text.startsWith("/set_tmlx") || text.startsWith("/dat_t_mlx")) {
      String arg;
      if (parseArgAfterSpace(text, arg)) {
        max_temp_mlx = clampNonNegative(arg.toFloat());
        prefs.putFloat("t_mlx", max_temp_mlx);
        tgSend("Đã đặt nhiệt NGẮT (MLX)=" + String(max_temp_mlx, 1) + " C (0=tắt)");
      }

    } else if (text.startsWith("/set_tenv") || text.startsWith("/dat_t_mt")) {
      String arg;
      if (parseArgAfterSpace(text, arg)) {
        max_temp_env = clampNonNegative(arg.toFloat());
        prefs.putFloat("t_env", max_temp_env);
        tgSend("Đã đặt nhiệt NGẮT (ENV)=" + String(max_temp_env, 1) + " C (0=tắt)");
      }

    } else if (text.startsWith("/set_hum") || text.startsWith("/dat_doam")) {
      String arg;
      if (parseArgAfterSpace(text, arg)) {
        max_humidity = clampNonNegative(arg.toFloat());
        prefs.putFloat("h_max", max_humidity);
        tgSend("Đã đặt độ ẩm NGẮT=" + String(max_humidity, 0) + " %");
      }

    } else if (text == "/reboot" || text == "/restart" || text == "/khoi_dong_lai") {
      tgSend("Đang khởi động lại ESP32...");
      delay(250);
      ESP.restart();

    } else if (text == "/reset_config" || text == "/khoi_phuc") {
      tgSend("Đang khôi phục mặc định...\nThiết bị sẽ khởi động lại");
      delay(250);
      prefs.clear();
      delay(150);
      ESP.restart();

    } else if (text.startsWith("/wifi")) {
      String arg;
      if (parseArgAfterSpace(text, arg)) {
        int sp = arg.indexOf(' ');
        if (sp > 0) {
          String s = arg.substring(0, sp);
          String p = arg.substring(sp + 1);
          s.trim(); p.trim();
          prefs.putString("ssid", s);
          prefs.putString("pass", p);
          tgSend("Đã lưu WiFi SSID: " + s + ". Khởi động lại thiết bị...");
          delay(1000);
          ESP.restart();
        } else {
          tgSend("Cú pháp: /wifi [SSID] [PASSWORD]");
        }
      } else {
        tgSend("Cú pháp: /wifi [SSID] [PASSWORD]");
      }

    } else if (text.startsWith("/ap")) {
      String arg;
      if (parseArgAfterSpace(text, arg)) {
        int sp = arg.indexOf(' ');
        if (sp > 0) {
          String s = arg.substring(0, sp);
          String p = arg.substring(sp + 1);
          s.trim(); p.trim();
          if (p.length() >= 8) {
            prefs.putString("ap_ssid", s);
            prefs.putString("ap_pass", p);
            tgSend("Đã lưu AP SSID: " + s + ". Khởi động lại thiết bị...");
            delay(1000);
            ESP.restart();
          } else {
            tgSend("Mật khẩu AP phải >= 8 ký tự!");
          }
        } else {
          tgSend("Cú pháp: /ap [SSID] [PASSWORD]");
        }
      } else {
        tgSend("Cú pháp: /ap [SSID] [PASSWORD]");
      }

    } else if (text.startsWith("/token")) {
      String arg;
      if (parseArgAfterSpace(text, arg)) {
        prefs.putString("api_key", arg);
        tgSend("Đã lưu Station Token: " + arg + ". Khởi động lại thiết bị...");
        delay(1000);
        ESP.restart();
      } else {
        tgSend("Cú pháp: /token [STATION_TOKEN]");
      }

    } else if (text == "/he_thong" || text == "/system") {
      String msg;
      msg.reserve(320);
      msg += "Hệ thống Smart Charger:\n";
      msg += "IP địa phương: " + WiFi.localIP().toString() + "\n";
      msg += "IP SoftAP: " + WiFi.softAPIP().toString() + "\n";
      msg += "WiFi Status: " + String(WiFi.status() == WL_CONNECTED ? "Đã kết nối" : "Chưa kết nối") + "\n";
      msg += "WiFi RSSI: " + String(WiFi.RSSI()) + " dBm\n";
      msg += "Tính năng active:\n";
      msg += "- DS: " + String(feat_ds ? "BẬT" : "TẮT") + "\n";
      msg += "- MLX: " + String(feat_mlx ? "BẬT" : "TẮT") + "\n";
      msg += "- DHT: " + String(feat_dht ? "BẬT" : "TẮT") + "\n";
      msg += "- PZEM: " + String(feat_pzem ? "BẬT" : "TẮT") + "\n";
      msg += "- Telegram: " + String(feat_tg ? "BẬT" : "TẮT") + "\n";
      msg += "- Cloud: " + String(feat_cloud ? "BẬT" : "TẮT") + "\n";
      msg += "- Phát AP: " + String(feat_ap_on ? "BẬT" : "TẮT") + "\n";
      msg += "- Luôn phát AP: " + String(feat_ap_always ? "BẬT" : "TẮT") + "\n";
      tgSend(msg);

    } else if (text == "/bat_ds") {
      feat_ds = true;
      prefs.putBool("feat_ds", true);
      tgSend("Đã BẬT cảm biến DS18B20");
    } else if (text == "/tat_ds") {
      feat_ds = false;
      prefs.putBool("feat_ds", false);
      tgSend("Đã TẮT cảm biến DS18B20");
    } else if (text == "/bat_mlx") {
      feat_mlx = true;
      prefs.putBool("feat_mlx", true);
      tgSend("Đã BẬT cảm biến MLX90614");
    } else if (text == "/tat_mlx") {
      feat_mlx = false;
      prefs.putBool("feat_mlx", false);
      tgSend("Đã TẮT cảm biến MLX90614");
    } else if (text == "/bat_dht") {
      feat_dht = true;
      prefs.putBool("feat_dht", true);
      tgSend("Đã BẬT cảm biến DHT");
    } else if (text == "/tat_dht") {
      feat_dht = false;
      prefs.putBool("feat_dht", false);
      tgSend("Đã TẮT cảm biến DHT");
    } else if (text == "/bat_pzem") {
      feat_pzem = true;
      prefs.putBool("feat_pzem", true);
      tgSend("Đã BẬT cảm biến PZEM");
    } else if (text == "/tat_pzem") {
      feat_pzem = false;
      prefs.putBool("feat_pzem", false);
      tgSend("Đã TẮT cảm biến PZEM");
    } else if (text == "/bat_telegram") {
      feat_tg = true;
      prefs.putBool("feat_tg", true);
      tgSend("Đã BẬT Telegram");
    } else if (text == "/tat_telegram") {
      tgSend("Đang TẮT Telegram...");
      delay(200);
      feat_tg = false;
      prefs.putBool("feat_tg", false);
    } else if (text == "/bat_cloud") {
      feat_cloud = true;
      prefs.putBool("feat_cloud", true);
      tgSend("Đã BẬT đồng bộ Cloud");
    } else if (text == "/tat_cloud") {
      feat_cloud = false;
      prefs.putBool("feat_cloud", false);
      tgSend("Đã TẮT đồng bộ Cloud");
    } else if (text == "/bat_ap") {
      feat_ap_on = true;
      prefs.putBool("feat_ap_on", true);
      tgSend("Đã BẬT phát WiFi AP. Hãy reboot để áp dụng.");
    } else if (text == "/tat_ap") {
      feat_ap_on = false;
      prefs.putBool("feat_ap_on", false);
      tgSend("Đã TẮT phát WiFi AP. Hãy reboot để áp dụng.");
    } else if (text == "/bat_ap_luon") {
      feat_ap_always = true;
      prefs.putBool("feat_ap_alw", true);
      tgSend("Đã BẬT luôn phát AP WiFi (không giới hạn 5 phút).");
    } else if (text == "/tat_ap_luon") {
      feat_ap_always = false;
      prefs.putBool("feat_ap_alw", false);
      tgSend("Đã TẮT luôn phát AP WiFi (AP tự tắt sau 5 phút).");

    } else {
      tgSend("Lệnh không hợp lệ. Gửi /help hoặc /tro_giup để xem danh sách lệnh.");
    }
  }
}

static void telegramAlertTask() {
  if (!feat_tg) return;
  if (!tgEnabled) return;
  if (WiFi.status() != WL_CONNECTED) return;

  static unsigned long lastSample = 0;
  if (millis() - lastSample < 5000) return;
  lastSample = millis();

  SensorFrame s = readSensorFrame();
  float tds = s.tds;
  float tmlx = s.tmlx;
  float tenv = s.tenv;
  float hum = s.hum;

  bool dsOk = s.dsOk;
  bool mlxOk = s.mlxOk;

  bool warnTemp = (dsControlEnabled() && warn_temp_ds > 0.0f && dsOk && tds >= warn_temp_ds && tds < max_temp_ds) ||
                  (mlxControlEnabled() && warn_temp_mlx > 0.0f && mlxOk && tmlx >= warn_temp_mlx && tmlx < max_temp_mlx) ||
                  (envControlEnabled() && warn_temp_env > 0.0f && !isnan(tenv) && tenv >= warn_temp_env && tenv < max_temp_env);

  bool hot = isHotNow(s);
  bool humid = isHumidNow(s);

  unsigned long now = millis();
  static bool tgWarnSent = false;

  if (hot && !tgAlertSentTemp && (now - lastTgAlertMs >= TG_ALERT_COOLDOWN_MS)) {
    String msg;
    msg.reserve(320);
    msg += "CẢNH BÁO: QUÁ NHIỆT\n";
    msg += "Trạng thái: " + stateToText();
    msg += "\nDS: " + fmtFloat(tds,1) + " C (ngắt " + String(max_temp_ds,1) + ")";
    msg += "\nMLX: " + fmtFloat(tmlx,1) + " C (ngắt " + String(max_temp_mlx,1) + ")";
    msg += "\nENV: " + fmtFloat(tenv,1) + " C (ngắt " + String(max_temp_env,1) + ")";
    tgSend(msg);
    tgAlertSentTemp = true;
    tgWarnSent = true;
    lastTgAlertMs = now;
  }
  if (!hot) tgAlertSentTemp = false;

  if (warnTemp && !tgWarnSent && !hot && (now - lastTgAlertMs >= TG_ALERT_COOLDOWN_MS)) {
    String msg;
    msg.reserve(420);
    msg += "CẢNH BÁO SỚM: Bộ sạc đang nóng dần (chưa đến mức ngắt)\n";
    msg += "\nNhiệt bộ sạc (DS): " + fmtFloat(tds,1) + " C";
    msg += "\n- Mức cảnh báo: " + String(warn_temp_ds,1) + " C";
    msg += "\n- Mức ngắt: " + String(max_temp_ds,1) + " C";
    msg += "\n\nNhiệt vỏ bình (MLX): " + fmtFloat(tmlx,1) + " C";
    msg += "\nNhiệt môi trường (ENV): " + fmtFloat(tenv,1) + " C";
    msg += "\n\nGợi ý: Kiểm tra tải sạc/quạt tản nhiệt để tránh bị ngắt.";
    tgSend(msg);
    tgWarnSent = true;
    lastTgAlertMs = now;
  }
  if (!warnTemp) tgWarnSent = false;

  if (humid && !tgAlertSentHum && (now - lastTgAlertMs >= TG_ALERT_COOLDOWN_MS)) {
    String msg;
    msg.reserve(220);
    msg += "CẢNH BÁO: ĐỘ ẨM CAO\n";
    msg += "H: " + fmtFloat(hum,0) + " % (ngắt " + String(max_humidity,0) + ")";
    msg += "\nTrạng thái: " + stateToText();
    tgSend(msg);
    tgAlertSentHum = true;
    lastTgAlertMs = now;
  }
  if (!humid) tgAlertSentHum = false;
}

#endif
>>>>>>> c33c85add95660613ecc968467e618cd78c9a62f
