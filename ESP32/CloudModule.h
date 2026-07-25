<<<<<<< HEAD
#ifndef CLOUD_MODULE_H
#define CLOUD_MODULE_H

static const char *stateToApiCode();
static void applyServerConfigFromJson(JsonVariantConst cfg);
static void applyServerStationMeta(JsonVariantConst data);
static void applyServerTelegramFromJson(JsonVariantConst tg);
static void applyServerCommand(const String &action);
static bool apiPostTelemetry();
static bool apiPullConfigAndCommand();
static void cloudSyncTask();

static const char *stateToApiCode() {
  if (!feat_ds && !feat_mlx && !feat_dht && !feat_pzem) {
    return "SAC_CHO";
  }
  switch (chargeState) {
    case CS_CHARGING: return "DANG_SAC";
    case CS_PROBING: return "DANG_SAC";
    case CS_WAIT: return "SAC_CHO";
    case CS_STOP_FULL: return "FULL";
    case CS_STOP_TIMEOUT: return "DUNG";
    case CS_STOP_POWER: return "LOI";
    case CS_STOP_TEMP: return "LOI";
    case CS_STOP_HUMID: return "LOI";
    case CS_SENSOR_ERROR: return "LOI";
    default: return "MAT_KET_NOI";
  }
}

static void removeAccents(const char* src, char* dst, int maxLen) {
  int i = 0, j = 0;
  while (src[i] != '\0' && j < maxLen - 1) {
    unsigned char c = src[i];
    if (c < 128) {
      if (c == 0xC2 && (unsigned char)src[i+1] == 0xB0) {
        dst[j++] = 'C';
        i += 2;
      } else {
        dst[j++] = c;
        i++;
      }
    } else {
      if (c == 0xC3) {
        unsigned char c2 = src[i+1];
        if (c2 == 0xA1 || c2 == 0xA0 || c2 == 0xA2 || c2 == 0xA3 || c2 == 0xA4 || c2 == 0xA5) dst[j++] = 'a';
        else if (c2 == 0x81 || c2 == 0x80 || c2 == 0x82 || c2 == 0x83) dst[j++] = 'A';
        else if (c2 == 0xA9 || c2 == 0xA8 || c2 == 0xAA || c2 == 0xAB) dst[j++] = 'e';
        else if (c2 == 0x89 || c2 == 0x88 || c2 == 0x8A) dst[j++] = 'E';
        else if (c2 == 0xAD || c2 == 0xAC || c2 == 0xAE || c2 == 0xAF) dst[j++] = 'i';
        else if (c2 == 0x8D || c2 == 0x8C) dst[j++] = 'I';
        else if (c2 == 0xB3 || c2 == 0xB2 || c2 == 0xB4 || c2 == 0xB5 || c2 == 0xB6 || c2 == 0xB8) dst[j++] = 'o';
        else if (c2 == 0x93 || c2 == 0x92 || c2 == 0x94 || c2 == 0x95) dst[j++] = 'O';
        else if (c2 == 0xBA || c2 == 0xB9 || c2 == 0xBB || c2 == 0xBC) dst[j++] = 'u';
        else if (c2 == 0x9A || c2 == 0x99) dst[j++] = 'U';
        else if (c2 == 0xBD) dst[j++] = 'y';
        else if (c2 == 0x9D) dst[j++] = 'Y';
        else dst[j++] = '?';
        i += 2;
      } else if (c == 0xC4 || c == 0xC5) {
        unsigned char c2 = src[i+1];
        if (c == 0xC4 && c2 == 0x83) dst[j++] = 'a';
        else if (c == 0xC4 && c2 == 0x82) dst[j++] = 'A';
        else if (c == 0xC4 && c2 == 0x91) dst[j++] = 'd';
        else if (c == 0xC4 && c2 == 0x90) dst[j++] = 'D';
        else if (c == 0xC4 && c2 == 0x93) dst[j++] = 'e';
        else if (c == 0xC5 && c2 == 0xa1) dst[j++] = 's';
        else if (c == 0xC5 && c2 == 0xa0) dst[j++] = 'S';
        else dst[j++] = '?';
        i += 2;
      } else if (c == 0xE1) {
        unsigned char c2 = src[i+1];
        unsigned char c3 = src[i+2];
        if (c2 == 0xBB) {
          if (c3 >= 0x82 && c3 <= 0x97) dst[j++] = (c3 % 2 == 0) ? 'A' : 'a';
          else if (c3 >= 0x98 && c3 <= 0xA7) dst[j++] = (c3 % 2 == 0) ? 'E' : 'e';
          else if (c3 >= 0xA8 && c3 <= 0xAB) dst[j++] = (c3 % 2 == 0) ? 'I' : 'i';
          else if (c3 >= 0xAC && c3 <= 0xC5) dst[j++] = (c3 % 2 == 0) ? 'O' : 'o';
          else if (c3 >= 0xC6 && c3 <= 0xD7) dst[j++] = (c3 % 2 == 0) ? 'U' : 'u';
          else if (c3 >= 0xD8 && c3 <= 0xDF) dst[j++] = (c3 % 2 == 0) ? 'Y' : 'y';
          else dst[j++] = '?';
        } else {
          dst[j++] = '?';
        }
        i += 3;
      } else {
        dst[j++] = '?';
        i++;
      }
    }
  }
  dst[j] = '\0';
}

static void applyServerConfigFromJson(JsonVariantConst cfg) {
  if (!cfg.is<JsonObjectConst>()) return;

  if (!cfg["wait_time"].isNull()) {
    long v = cfg["wait_time"].as<long>();
    if (v < 0) v = 0;
    if (v > 65535) v = 65535;
    if (wait_minutes != (uint16_t)v) {
      wait_minutes = (uint16_t)v;
      prefs.putInt("wait", wait_minutes);
    }
  }

  if (!cfg["measure_interval"].isNull()) {
    long v = cfg["measure_interval"].as<long>();
    if (v < 0) v = 0;
    if (v > 3600) v = 3600;
    if (measure_seconds != (uint16_t)v) {
      measure_seconds = (uint16_t)v;
      prefs.putInt("measure", measure_seconds);
    }
  }

  if (!cfg["threshold_w"].isNull()) {
    min_power_w = clampNonNegative(cfg["threshold_w"].as<float>());
    if (min_power_w < 0.0f) min_power_w = 0.0f;
    if (fabsf(full_power_threshold - min_power_w) > 0.05f) {
      full_power_threshold = min_power_w;
      prefs.putFloat("p_min", min_power_w);
      prefs.putFloat("full", full_power_threshold);
    }
  }
  if (!cfg["limit_input_w"].isNull()) {
    float v = clampNonNegative(cfg["limit_input_w"].as<float>());
    if (fabsf(max_power_w - v) > 0.05f) {
      max_power_w = v;
      prefs.putFloat("p_max", max_power_w);
    }
  }
  if (!cfg["max_temp_charger"].isNull()) {
    float v = clampNonNegative(cfg["max_temp_charger"].as<float>());
    if (fabsf(max_temp_ds - v) > 0.05f) {
      max_temp_ds = v;
      prefs.putFloat("t_ds", max_temp_ds);
    }
  }
  if (!cfg["max_temp_battery"].isNull()) {
    float v = clampNonNegative(cfg["max_temp_battery"].as<float>());
    if (fabsf(max_temp_mlx - v) > 0.05f) {
      max_temp_mlx = v;
      prefs.putFloat("t_mlx", max_temp_mlx);
    }
  }
  if (!cfg["max_temp_env"].isNull()) {
    float v = clampNonNegative(cfg["max_temp_env"].as<float>());
    if (fabsf(max_temp_env - v) > 0.05f) {
      max_temp_env = v;
      prefs.putFloat("t_env", max_temp_env);
    }
  }
  if (!cfg["max_humidity"].isNull()) {
    float v = clampNonNegative(cfg["max_humidity"].as<float>());
    if (fabsf(max_humidity - v) > 0.05f) {
      max_humidity = v;
      prefs.putFloat("h_max", max_humidity);
    }
  }
  if (!cfg["max_time_h"].isNull()) {
    long v = cfg["max_time_h"].as<long>();
    if (v < 0) v = 0;
    if (v > 72) v = 72;
    if (max_charge_hours != (uint16_t)v) {
      max_charge_hours = (uint16_t)v;
      prefs.putInt("max_h", max_charge_hours);
    }
  }

  if (!cfg["battery_type"].isNull()) {
    String typeVal = cfg["battery_type"].as<String>();
    setBatteryKindFromType(typeVal);
    prefs.putString("bat_kind", String(battery_kind_lcd));
  }
  if (!cfg["capacity_ah"].isNull()) {
    long ah = cfg["capacity_ah"].as<long>();
    if (ah < 1) ah = 1;
    if (ah > 1000) ah = 1000;
    battery_capacity_ah_lcd = (uint16_t)ah;
    prefs.putInt("bat_ah", (int)battery_capacity_ah_lcd);
  }
  if (!cfg["output_a"].isNull()) {
    float oa = cfg["output_a"].as<float>();
    if (oa < 0.0f) oa = 0.0f;
    if (oa > 100.0f) oa = 100.0f;
    output_a_lcd = oa;
    prefs.putFloat("out_a_lcd", output_a_lcd);
  }
}

static void applyServerStationMeta(JsonVariantConst data) {
  const char *name = data["station_name"] | "";
  if (!name || name[0] == '\0') return;

  char tmp[33];
  memset(tmp, 0, sizeof(tmp));
  strlcpy(tmp, name, sizeof(tmp));
  if (strncmp(server_station_name, tmp, sizeof(server_station_name)) != 0) {
    strlcpy(server_station_name, tmp, sizeof(server_station_name));
    prefs.putString("server_name", String(server_station_name));
  }
}

static void applyServerTelegramFromJson(JsonVariantConst tg) {
  if (!tg.is<JsonObjectConst>()) return;

  bool hasEnabledField = !tg["enabled"].isNull();
  bool hasTokenField = !tg["token"].isNull();
  bool hasChatField = !tg["chat_id"].isNull();
  if (!hasEnabledField && !hasTokenField && !hasChatField) return;

  bool enabled = tg["enabled"] | false;
  String token = tg["token"] | "";
  String chatId = tg["chat_id"] | "";
  token.trim();
  chatId.trim();

  if (enabled && token.length() > 0 && chatId.length() > 0) {
    token.toCharArray(tg_bot_token, sizeof(tg_bot_token));
    chatId.toCharArray(tg_chat_id, sizeof(tg_chat_id));
    bot.updateToken(String(tg_bot_token));
    tgEnabled = true;
    return;
  }

  // Keep current local Telegram credentials if server payload is incomplete
  // or disabled, to avoid accidental runtime lockout from cloud defaults.
}

static void applyServerCommand(const String &action) {
  if (action == "charge_now") {
    // Ignore duplicate charge-now when already charging steadily.
    if (!(charging && probeState == PROBE_IDLE && chargeState == CS_CHARGING)) {
      safety_warning_msg[0] = '\0';
      startChargingNow("API");
    }
  } else if (action == "charge_wait") {
    // Prevent server polling from repeatedly resetting nextStartMs.
    bool alreadyWaiting = autoEnabled && !charging && probeState == PROBE_IDLE &&
                          chargeState == CS_WAIT && nextStartMs != 0 &&
                          digitalRead(relay_pin) == LOW;
    if (!alreadyWaiting) {
      safety_warning_msg[0] = '\0';
      setWaitMode("API");
    }
  } else if (action == "charge_stop") {
    bool alreadyStopped = !autoEnabled && !charging && probeState == PROBE_IDLE &&
                          chargeState == CS_WAIT && digitalRead(relay_pin) == LOW;
    if (!alreadyStopped) {
      stopAll("API");
    }
  }
}

static bool apiPostTelemetry() {
  if (WiFi.status() != WL_CONNECTED) return false;
  if (station_token[0] == '\0') return false;

  SensorFrame s = readSensorFrame();
  unsigned long elapsedMin = chargeElapsedMs() / 60000UL;
  long nextStartSec = 0;
  if (autoEnabled && nextStartMs != 0 && (long)(nextStartMs - millis()) > 0) {
    nextStartSec = (long)((nextStartMs - millis()) / 1000UL);
  }

  long fullHoldSec = 0;
  if (fullLowStartMs != 0 && measure_seconds > 0 && !isnan(s.pwr) && s.pwr <= full_power_threshold) {
    fullHoldSec = (long)((millis() - fullLowStartMs) / 1000UL);
  }

  StaticJsonDocument<1024> doc;
  if (feat_pzem) {
    doc["p"] = safeReading(s.pwr);
    doc["v"] = safeReading(s.voltage);
    doc["i"] = safeReading(s.current);
  } else {
    doc["p"] = "N/A";
    doc["v"] = "N/A";
    doc["i"] = "N/A";
  }

  if (feat_ds) {
    doc["ds"] = safeReading(s.tds);
  } else {
    doc["ds"] = "N/A";
  }

  if (feat_mlx) {
    doc["mlx"] = safeReading(s.tmlx);
  } else {
    doc["mlx"] = "N/A";
  }

  if (feat_dht) {
    doc["dht_t"] = safeReading(s.tenv);
    doc["env_t"] = safeReading(s.tenv);
    doc["dht_h"] = safeReading(s.hum);
    doc["hum"] = safeReading(s.hum);
  } else {
    doc["dht_t"] = "N/A";
    doc["env_t"] = "N/A";
    doc["dht_h"] = "N/A";
    doc["hum"] = "N/A";
  }
  doc["state"] = stateToApiCode();
  doc["state_text"] = stateWebText();
  doc["charge_time_min"] = (int)elapsedMin;
  doc["relay_on"] = digitalRead(relay_pin) == HIGH;
  doc["charging"] = charging;
  doc["auto_enabled"] = autoEnabled;
  doc["lockout"] = lockout;
  doc["probe"] = (probeState != PROBE_IDLE);
  doc["wifi_rssi"] = (WiFi.status() == WL_CONNECTED) ? WiFi.RSSI() : 0;
  doc["sensor_ds_ok"] = s.dsOk;
  doc["sensor_mlx_ok"] = s.mlxOk;
  doc["sensor_pzem_ok"] = !isnan(s.pwr) && !isnan(s.voltage) && !isnan(s.current);
  doc["wait_min"] = wait_minutes;
  doc["measure_sec"] = measure_seconds;
  doc["full_w"] = full_power_threshold;
  doc["p_max"] = max_power_w;
  doc["max_charge_h"] = max_charge_hours;
  doc["next_start_sec"] = nextStartSec > 0 ? nextStartSec : 0;
  doc["full_hold_sec"] = fullHoldSec > 0 ? fullHoldSec : 0;

  if (charging && max_charge_hours > 0) {
    long remain = (long)max_charge_hours * 60L - (long)elapsedMin;
    if (remain < 0) remain = 0;
    doc["est"] = remain;
  } else {
    doc["est"] = "--";
  }

  String body;
  serializeJson(doc, body);

  HTTPClient http;
  String url = String(API_BASE_URL) + API_TELEMETRY_PATH;
  http.begin(url);
  http.addHeader("Content-Type", "application/json");
  http.addHeader("Authorization", String("Bearer ") + station_token);
  http.addHeader("X-Station-Token", String(station_token));
  int code = http.POST(body);
  String resp = http.getString();
  http.end();

  if (code >= 200 && code < 300) {
    lastServerContactMs = millis();
  } else if (code == 400 || (code > 0 && resp.length() > 0)) {
    StaticJsonDocument<512> respDoc;
    DeserializationError respErr = deserializeJson(respDoc, resp);
    if (!respErr) {
      const char* status = respDoc["status"] | "";
      const char* action = respDoc["action"] | "";
      if (strcmp(status, "error") == 0 && strcmp(action, "charge_stop") == 0) {
        const char* msg = respDoc["message"] | "";
        if (msg && msg[0] != '\0') {
          removeAccents(msg, safety_warning_msg, sizeof(safety_warning_msg));
        } else {
          strlcpy(safety_warning_msg, "SAFETY RISK DETECTED", sizeof(safety_warning_msg));
        }
        stopWithReason(CS_SENSOR_ERROR, nullptr);
      }
    }
  }

  return code >= 200 && code < 300;
}

static bool apiPullConfigAndCommand() {
  if (WiFi.status() != WL_CONNECTED) return false;
  if (station_token[0] == '\0') return false;

  HTTPClient http;
  String url = String(API_BASE_URL) + API_CONFIG_PATH;
  http.begin(url);
  http.addHeader("Authorization", String("Bearer ") + station_token);
  http.addHeader("X-Station-Token", String(station_token));

  int code = http.GET();
  if (code < 200 || code >= 300) {
    String resp = http.getString();
    http.end();
    return false;
  }

  String body = http.getString();
  http.end();

  StaticJsonDocument<3072> doc;
  DeserializationError err = deserializeJson(doc, body);
  if (err) {
    return false;
  }

  JsonVariantConst data = doc["data"];
  if (data.isNull()) return false;

  applyServerStationMeta(data);
  applyServerConfigFromJson(data["config"]);
  applyServerTelegramFromJson(data["telegram"]);

  const char *action = data["command"]["action"] | "";
  if (action && action[0] != '\0') {
    applyServerCommand(String(action));
  }

  lastServerContactMs = millis();

  return true;
}

static void cloudSyncTask() {
  if (!feat_cloud) return;
  if (WiFi.status() != WL_CONNECTED) return;

  unsigned long now = millis();
  bool isFullState = strcmp(stateToApiCode(), "FULL") == 0;
  unsigned long telemetryInterval = isFullState ? API_FULL_KEEPALIVE_MS : API_TELEMETRY_INTERVAL_MS;

  if (!isFullState) {
    fullTelemetrySentOnce = false;
  }

  if ((long)(now - lastApiTelemetryMs) >= (long)telemetryInterval) {
    if (apiPostTelemetry()) {
      lastApiTelemetryMs = now;
      if (isFullState) fullTelemetrySentOnce = true;
    } else {
      lastApiTelemetryMs = now - (telemetryInterval - API_RETRY_ON_FAIL_MS);
    }
  }

  if ((long)(now - lastApiConfigMs) >= (long)API_CONFIG_INTERVAL_MS) {
    if (apiPullConfigAndCommand()) {
      lastApiConfigMs = now;
    } else {
      lastApiConfigMs = now - (API_CONFIG_INTERVAL_MS - API_RETRY_ON_FAIL_MS);
    }
  }
}

#endif
=======
#ifndef CLOUD_MODULE_H
#define CLOUD_MODULE_H

static const char *stateToApiCode();
static void applyServerConfigFromJson(JsonVariantConst cfg);
static void applyServerStationMeta(JsonVariantConst data);
static void applyServerTelegramFromJson(JsonVariantConst tg);
static void applyServerCommand(const String &action);
static bool apiPostTelemetry();
static bool apiPullConfigAndCommand();
static void cloudSyncTask();

static const char *stateToApiCode() {
  if (!feat_ds && !feat_mlx && !feat_dht && !feat_pzem) {
    return "SAC_CHO";
  }
  switch (chargeState) {
    case CS_CHARGING: return "DANG_SAC";
    case CS_PROBING: return "DANG_SAC";
    case CS_WAIT: return "SAC_CHO";
    case CS_STOP_FULL: return "FULL";
    case CS_STOP_TIMEOUT: return "DUNG";
    case CS_STOP_POWER: return "LOI";
    case CS_STOP_TEMP: return "LOI";
    case CS_STOP_HUMID: return "LOI";
    case CS_SENSOR_ERROR: return "LOI";
    default: return "MAT_KET_NOI";
  }
}

static void removeAccents(const char* src, char* dst, int maxLen) {
  int i = 0, j = 0;
  while (src[i] != '\0' && j < maxLen - 1) {
    unsigned char c = src[i];
    if (c < 128) {
      if (c == 0xC2 && (unsigned char)src[i+1] == 0xB0) {
        dst[j++] = 'C';
        i += 2;
      } else {
        dst[j++] = c;
        i++;
      }
    } else {
      if (c == 0xC3) {
        unsigned char c2 = src[i+1];
        if (c2 == 0xA1 || c2 == 0xA0 || c2 == 0xA2 || c2 == 0xA3 || c2 == 0xA4 || c2 == 0xA5) dst[j++] = 'a';
        else if (c2 == 0x81 || c2 == 0x80 || c2 == 0x82 || c2 == 0x83) dst[j++] = 'A';
        else if (c2 == 0xA9 || c2 == 0xA8 || c2 == 0xAA || c2 == 0xAB) dst[j++] = 'e';
        else if (c2 == 0x89 || c2 == 0x88 || c2 == 0x8A) dst[j++] = 'E';
        else if (c2 == 0xAD || c2 == 0xAC || c2 == 0xAE || c2 == 0xAF) dst[j++] = 'i';
        else if (c2 == 0x8D || c2 == 0x8C) dst[j++] = 'I';
        else if (c2 == 0xB3 || c2 == 0xB2 || c2 == 0xB4 || c2 == 0xB5 || c2 == 0xB6 || c2 == 0xB8) dst[j++] = 'o';
        else if (c2 == 0x93 || c2 == 0x92 || c2 == 0x94 || c2 == 0x95) dst[j++] = 'O';
        else if (c2 == 0xBA || c2 == 0xB9 || c2 == 0xBB || c2 == 0xBC) dst[j++] = 'u';
        else if (c2 == 0x9A || c2 == 0x99) dst[j++] = 'U';
        else if (c2 == 0xBD) dst[j++] = 'y';
        else if (c2 == 0x9D) dst[j++] = 'Y';
        else dst[j++] = '?';
        i += 2;
      } else if (c == 0xC4 || c == 0xC5) {
        unsigned char c2 = src[i+1];
        if (c == 0xC4 && c2 == 0x83) dst[j++] = 'a';
        else if (c == 0xC4 && c2 == 0x82) dst[j++] = 'A';
        else if (c == 0xC4 && c2 == 0x91) dst[j++] = 'd';
        else if (c == 0xC4 && c2 == 0x90) dst[j++] = 'D';
        else if (c == 0xC4 && c2 == 0x93) dst[j++] = 'e';
        else if (c == 0xC5 && c2 == 0xa1) dst[j++] = 's';
        else if (c == 0xC5 && c2 == 0xa0) dst[j++] = 'S';
        else dst[j++] = '?';
        i += 2;
      } else if (c == 0xE1) {
        unsigned char c2 = src[i+1];
        unsigned char c3 = src[i+2];
        if (c2 == 0xBB) {
          if (c3 >= 0x82 && c3 <= 0x97) dst[j++] = (c3 % 2 == 0) ? 'A' : 'a';
          else if (c3 >= 0x98 && c3 <= 0xA7) dst[j++] = (c3 % 2 == 0) ? 'E' : 'e';
          else if (c3 >= 0xA8 && c3 <= 0xAB) dst[j++] = (c3 % 2 == 0) ? 'I' : 'i';
          else if (c3 >= 0xAC && c3 <= 0xC5) dst[j++] = (c3 % 2 == 0) ? 'O' : 'o';
          else if (c3 >= 0xC6 && c3 <= 0xD7) dst[j++] = (c3 % 2 == 0) ? 'U' : 'u';
          else if (c3 >= 0xD8 && c3 <= 0xDF) dst[j++] = (c3 % 2 == 0) ? 'Y' : 'y';
          else dst[j++] = '?';
        } else {
          dst[j++] = '?';
        }
        i += 3;
      } else {
        dst[j++] = '?';
        i++;
      }
    }
  }
  dst[j] = '\0';
}

static void applyServerConfigFromJson(JsonVariantConst cfg) {
  if (!cfg.is<JsonObjectConst>()) return;

  if (!cfg["wait_time"].isNull()) {
    long v = cfg["wait_time"].as<long>();
    if (v < 0) v = 0;
    if (v > 65535) v = 65535;
    if (wait_minutes != (uint16_t)v) {
      wait_minutes = (uint16_t)v;
      prefs.putInt("wait", wait_minutes);
    }
  }

  if (!cfg["measure_interval"].isNull()) {
    long v = cfg["measure_interval"].as<long>();
    if (v < 0) v = 0;
    if (v > 3600) v = 3600;
    if (measure_seconds != (uint16_t)v) {
      measure_seconds = (uint16_t)v;
      prefs.putInt("measure", measure_seconds);
    }
  }

  if (!cfg["threshold_w"].isNull()) {
    min_power_w = clampNonNegative(cfg["threshold_w"].as<float>());
    if (min_power_w < 0.0f) min_power_w = 0.0f;
    if (fabsf(full_power_threshold - min_power_w) > 0.05f) {
      full_power_threshold = min_power_w;
      prefs.putFloat("p_min", min_power_w);
      prefs.putFloat("full", full_power_threshold);
    }
  }
  if (!cfg["limit_input_w"].isNull()) {
    float v = clampNonNegative(cfg["limit_input_w"].as<float>());
    if (fabsf(max_power_w - v) > 0.05f) {
      max_power_w = v;
      prefs.putFloat("p_max", max_power_w);
    }
  }
  if (!cfg["max_temp_charger"].isNull()) {
    float v = clampNonNegative(cfg["max_temp_charger"].as<float>());
    if (fabsf(max_temp_ds - v) > 0.05f) {
      max_temp_ds = v;
      prefs.putFloat("t_ds", max_temp_ds);
    }
  }
  if (!cfg["max_temp_battery"].isNull()) {
    float v = clampNonNegative(cfg["max_temp_battery"].as<float>());
    if (fabsf(max_temp_mlx - v) > 0.05f) {
      max_temp_mlx = v;
      prefs.putFloat("t_mlx", max_temp_mlx);
    }
  }
  if (!cfg["max_temp_env"].isNull()) {
    float v = clampNonNegative(cfg["max_temp_env"].as<float>());
    if (fabsf(max_temp_env - v) > 0.05f) {
      max_temp_env = v;
      prefs.putFloat("t_env", max_temp_env);
    }
  }
  if (!cfg["max_humidity"].isNull()) {
    float v = clampNonNegative(cfg["max_humidity"].as<float>());
    if (fabsf(max_humidity - v) > 0.05f) {
      max_humidity = v;
      prefs.putFloat("h_max", max_humidity);
    }
  }
  if (!cfg["max_time_h"].isNull()) {
    long v = cfg["max_time_h"].as<long>();
    if (v < 0) v = 0;
    if (v > 72) v = 72;
    if (max_charge_hours != (uint16_t)v) {
      max_charge_hours = (uint16_t)v;
      prefs.putInt("max_h", max_charge_hours);
    }
  }

  if (!cfg["battery_type"].isNull()) {
    String typeVal = cfg["battery_type"].as<String>();
    setBatteryKindFromType(typeVal);
    prefs.putString("bat_kind", String(battery_kind_lcd));
  }
  if (!cfg["capacity_ah"].isNull()) {
    long ah = cfg["capacity_ah"].as<long>();
    if (ah < 1) ah = 1;
    if (ah > 1000) ah = 1000;
    battery_capacity_ah_lcd = (uint16_t)ah;
    prefs.putInt("bat_ah", (int)battery_capacity_ah_lcd);
  }
  if (!cfg["output_a"].isNull()) {
    float oa = cfg["output_a"].as<float>();
    if (oa < 0.0f) oa = 0.0f;
    if (oa > 100.0f) oa = 100.0f;
    output_a_lcd = oa;
    prefs.putFloat("out_a_lcd", output_a_lcd);
  }
}

static void applyServerStationMeta(JsonVariantConst data) {
  const char *name = data["station_name"] | "";
  if (!name || name[0] == '\0') return;

  char tmp[33];
  memset(tmp, 0, sizeof(tmp));
  strlcpy(tmp, name, sizeof(tmp));
  if (strncmp(server_station_name, tmp, sizeof(server_station_name)) != 0) {
    strlcpy(server_station_name, tmp, sizeof(server_station_name));
    prefs.putString("server_name", String(server_station_name));
  }
}

static void applyServerTelegramFromJson(JsonVariantConst tg) {
  if (!tg.is<JsonObjectConst>()) return;

  bool hasEnabledField = !tg["enabled"].isNull();
  bool hasTokenField = !tg["token"].isNull();
  bool hasChatField = !tg["chat_id"].isNull();
  if (!hasEnabledField && !hasTokenField && !hasChatField) return;

  bool enabled = tg["enabled"] | false;
  String token = tg["token"] | "";
  String chatId = tg["chat_id"] | "";
  token.trim();
  chatId.trim();

  if (enabled && token.length() > 0 && chatId.length() > 0) {
    token.toCharArray(tg_bot_token, sizeof(tg_bot_token));
    chatId.toCharArray(tg_chat_id, sizeof(tg_chat_id));
    bot.updateToken(String(tg_bot_token));
    tgEnabled = true;
    return;
  }

  // Keep current local Telegram credentials if server payload is incomplete
  // or disabled, to avoid accidental runtime lockout from cloud defaults.
}

static void applyServerCommand(const String &action) {
  if (action == "charge_now") {
    // Ignore duplicate charge-now when already charging steadily.
    if (!(charging && probeState == PROBE_IDLE && chargeState == CS_CHARGING)) {
      safety_warning_msg[0] = '\0';
      startChargingNow("API");
    }
  } else if (action == "charge_wait") {
    // Prevent server polling from repeatedly resetting nextStartMs.
    bool alreadyWaiting = autoEnabled && !charging && probeState == PROBE_IDLE &&
                          chargeState == CS_WAIT && nextStartMs != 0 &&
                          digitalRead(relay_pin) == LOW;
    if (!alreadyWaiting) {
      safety_warning_msg[0] = '\0';
      setWaitMode("API");
    }
  } else if (action == "charge_stop") {
    bool alreadyStopped = !autoEnabled && !charging && probeState == PROBE_IDLE &&
                          chargeState == CS_WAIT && digitalRead(relay_pin) == LOW;
    if (!alreadyStopped) {
      stopAll("API");
    }
  }
}

static bool apiPostTelemetry() {
  if (WiFi.status() != WL_CONNECTED) return false;
  if (station_token[0] == '\0') return false;

  SensorFrame s = readSensorFrame();
  unsigned long elapsedMin = chargeElapsedMs() / 60000UL;
  long nextStartSec = 0;
  if (autoEnabled && nextStartMs != 0 && (long)(nextStartMs - millis()) > 0) {
    nextStartSec = (long)((nextStartMs - millis()) / 1000UL);
  }

  long fullHoldSec = 0;
  if (fullLowStartMs != 0 && measure_seconds > 0 && !isnan(s.pwr) && s.pwr <= full_power_threshold) {
    fullHoldSec = (long)((millis() - fullLowStartMs) / 1000UL);
  }

  StaticJsonDocument<1024> doc;
  if (feat_pzem) {
    doc["p"] = safeReading(s.pwr);
    doc["v"] = safeReading(s.voltage);
    doc["i"] = safeReading(s.current);
  } else {
    doc["p"] = "N/A";
    doc["v"] = "N/A";
    doc["i"] = "N/A";
  }

  if (feat_ds) {
    doc["ds"] = safeReading(s.tds);
  } else {
    doc["ds"] = "N/A";
  }

  if (feat_mlx) {
    doc["mlx"] = safeReading(s.tmlx);
  } else {
    doc["mlx"] = "N/A";
  }

  if (feat_dht) {
    doc["dht_t"] = safeReading(s.tenv);
    doc["env_t"] = safeReading(s.tenv);
    doc["dht_h"] = safeReading(s.hum);
    doc["hum"] = safeReading(s.hum);
  } else {
    doc["dht_t"] = "N/A";
    doc["env_t"] = "N/A";
    doc["dht_h"] = "N/A";
    doc["hum"] = "N/A";
  }
  doc["state"] = stateToApiCode();
  doc["state_text"] = stateWebText();
  doc["charge_time_min"] = (int)elapsedMin;
  doc["relay_on"] = digitalRead(relay_pin) == HIGH;
  doc["charging"] = charging;
  doc["auto_enabled"] = autoEnabled;
  doc["lockout"] = lockout;
  doc["probe"] = (probeState != PROBE_IDLE);
  doc["wifi_rssi"] = (WiFi.status() == WL_CONNECTED) ? WiFi.RSSI() : 0;
  doc["sensor_ds_ok"] = s.dsOk;
  doc["sensor_mlx_ok"] = s.mlxOk;
  doc["sensor_pzem_ok"] = !isnan(s.pwr) && !isnan(s.voltage) && !isnan(s.current);
  doc["wait_min"] = wait_minutes;
  doc["measure_sec"] = measure_seconds;
  doc["full_w"] = full_power_threshold;
  doc["p_max"] = max_power_w;
  doc["max_charge_h"] = max_charge_hours;
  doc["next_start_sec"] = nextStartSec > 0 ? nextStartSec : 0;
  doc["full_hold_sec"] = fullHoldSec > 0 ? fullHoldSec : 0;

  if (charging && max_charge_hours > 0) {
    long remain = (long)max_charge_hours * 60L - (long)elapsedMin;
    if (remain < 0) remain = 0;
    doc["est"] = remain;
  } else {
    doc["est"] = "--";
  }

  String body;
  serializeJson(doc, body);

  HTTPClient http;
  String url = String(API_BASE_URL) + API_TELEMETRY_PATH;
  http.begin(url);
  http.addHeader("Content-Type", "application/json");
  http.addHeader("Authorization", String("Bearer ") + station_token);
  http.addHeader("X-Station-Token", String(station_token));
  int code = http.POST(body);
  String resp = http.getString();
  http.end();

  if (code >= 200 && code < 300) {
    lastServerContactMs = millis();
  } else if (code == 400 || (code > 0 && resp.length() > 0)) {
    StaticJsonDocument<512> respDoc;
    DeserializationError respErr = deserializeJson(respDoc, resp);
    if (!respErr) {
      const char* status = respDoc["status"] | "";
      const char* action = respDoc["action"] | "";
      if (strcmp(status, "error") == 0 && strcmp(action, "charge_stop") == 0) {
        const char* msg = respDoc["message"] | "";
        if (msg && msg[0] != '\0') {
          removeAccents(msg, safety_warning_msg, sizeof(safety_warning_msg));
        } else {
          strlcpy(safety_warning_msg, "SAFETY RISK DETECTED", sizeof(safety_warning_msg));
        }
        stopWithReason(CS_SENSOR_ERROR, nullptr);
      }
    }
  }

  return code >= 200 && code < 300;
}

static bool apiPullConfigAndCommand() {
  if (WiFi.status() != WL_CONNECTED) return false;
  if (station_token[0] == '\0') return false;

  HTTPClient http;
  String url = String(API_BASE_URL) + API_CONFIG_PATH;
  http.begin(url);
  http.addHeader("Authorization", String("Bearer ") + station_token);
  http.addHeader("X-Station-Token", String(station_token));

  int code = http.GET();
  if (code < 200 || code >= 300) {
    String resp = http.getString();
    http.end();
    return false;
  }

  String body = http.getString();
  http.end();

  StaticJsonDocument<3072> doc;
  DeserializationError err = deserializeJson(doc, body);
  if (err) {
    return false;
  }

  JsonVariantConst data = doc["data"];
  if (data.isNull()) return false;

  applyServerStationMeta(data);
  applyServerConfigFromJson(data["config"]);
  applyServerTelegramFromJson(data["telegram"]);

  const char *action = data["command"]["action"] | "";
  if (action && action[0] != '\0') {
    applyServerCommand(String(action));
  }

  lastServerContactMs = millis();

  return true;
}

static void cloudSyncTask() {
  if (!feat_cloud) return;
  if (WiFi.status() != WL_CONNECTED) return;

  unsigned long now = millis();
  bool isFullState = strcmp(stateToApiCode(), "FULL") == 0;
  unsigned long telemetryInterval = isFullState ? API_FULL_KEEPALIVE_MS : API_TELEMETRY_INTERVAL_MS;

  if (!isFullState) {
    fullTelemetrySentOnce = false;
  }

  if ((long)(now - lastApiTelemetryMs) >= (long)telemetryInterval) {
    if (apiPostTelemetry()) {
      lastApiTelemetryMs = now;
      if (isFullState) fullTelemetrySentOnce = true;
    } else {
      lastApiTelemetryMs = now - (telemetryInterval - API_RETRY_ON_FAIL_MS);
    }
  }

  if ((long)(now - lastApiConfigMs) >= (long)API_CONFIG_INTERVAL_MS) {
    if (apiPullConfigAndCommand()) {
      lastApiConfigMs = now;
    } else {
      lastApiConfigMs = now - (API_CONFIG_INTERVAL_MS - API_RETRY_ON_FAIL_MS);
    }
  }
}

#endif
>>>>>>> c33c85add95660613ecc968467e618cd78c9a62f
