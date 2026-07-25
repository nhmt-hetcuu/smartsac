// v8
#include <WiFi.h>
#include <WebServer.h>
#include <HTTPClient.h>
#include <Preferences.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>

#include <OneWire.h>
#include <DallasTemperature.h>
#include <PZEM004Tv30.h>
#include <Wire.h>
#include <Adafruit_MLX90614.h>
#include <DHT.h>
#include <RTClib.h>

#include <UniversalTelegramBot.h>
#include "SensorFrame.h"

#define PZEM_RX_PIN 16
#define PZEM_TX_PIN 17
#define DHT_PIN 4
#define DS18B20_PIN 5
#define MLX_SDA_PIN 7
#define MLX_SCL_PIN 8
#define LCD_SDA_PIN 10
#define LCD_SCL_PIN 11
#define RTC_SDA_PIN 12
#define RTC_SCL_PIN 13

#define RELAY_PIN 36
#define LCD_ADDR 0x27
#define LCD_COLS 20
#define LCD_ROWS 4

#define BTN_PIN 3
#define BTN_RESTORE_PIN 38
#define DHT_TYPE DHT21

enum ChargeState : uint8_t {
  CS_WAIT = 0,
  CS_PROBING,
  CS_CHARGING,
  CS_STOP_TEMP,
  CS_STOP_FULL,
  CS_STOP_HUMID,
  CS_STOP_TIMEOUT,
  CS_STOP_POWER,
  CS_SENSOR_ERROR
};

ChargeState chargeState = CS_WAIT;
ChargeState lastChargeState = CS_WAIT;

WebServer server(80);
Preferences prefs;

WiFiClientSecure tgClient;
UniversalTelegramBot bot("", tgClient);

char tg_bot_token[128] = {0};
char tg_chat_id[32] = {0};
bool tgEnabled = false;

bool tgAlertSentTemp = false;
bool tgAlertSentHum = false;
unsigned long lastTgCheck = 0;
const unsigned long TG_POLL_MS = 2000;
unsigned long lastTgAlertMs = 0;
const unsigned long TG_ALERT_COOLDOWN_MS = 60000;

OneWire* oneWire = nullptr;
DallasTemperature* dallas = nullptr;
Adafruit_MLX90614 mlx;
RTC_DS3231 rtc;

TwoWire I2CAux = TwoWire(1);
enum AuxBus : uint8_t { AUX_BUS_NONE = 0, AUX_BUS_LCD, AUX_BUS_RTC };
AuxBus activeAuxBus = AUX_BUS_NONE;
bool rtcReady = false;
unsigned long lastLcdMs = 0;
const unsigned long LCD_UPDATE_MS = 500;

HardwareSerial PZEMSerial(2);
PZEM004Tv30* pzem = nullptr;

DHT* dht = nullptr;

#include "ConfigModule.h"

bool charging = false;
unsigned long chargeStartMs = 0;
unsigned long lastChargeDurMs = 0;

static inline unsigned long chargeElapsedMs() {
  if (charging && chargeStartMs != 0) return millis() - chargeStartMs;
  return lastChargeDurMs;
}

const unsigned long PROBE_ON_MS = 8000UL;

enum ProbeState : uint8_t { PROBE_IDLE = 0, PROBE_WAIT };
ProbeState probeState = PROBE_IDLE;
unsigned long probeT0 = 0;

uint8_t dsBadCount = 0;
uint8_t mlxBadCount = 0;
uint8_t pzemBadCount = 0;
const uint8_t DS_BAD_LIMIT = 3;
const uint8_t MLX_BAD_LIMIT = 3;
const uint8_t PZEM_BAD_LIMIT = 10;

static inline bool validTempDS(float t) {
  return !isnan(t) && t > -50.0f && t < 125.0f;
}

static inline bool validTempMLX(float t) {
  return !isnan(t) && t > -70.0f && t < 380.0f;
}

static inline float clampNonNegative(float value) {
  return value < 0.0f ? 0.0f : value;
}

static inline float safeReading(float value) {
  return isnan(value) ? 0.0f : value;
}

static inline bool dsControlEnabled();
static inline bool mlxControlEnabled();
static inline bool envControlEnabled();
static inline bool humControlEnabled();

static SensorFrame readSensorFrame() {
  SensorFrame s;
  if (feat_ds && dallas) {
    dallas->requestTemperatures();
    s.tds = dallas->getTempCByIndex(0);
    s.dsOk = validTempDS(s.tds);
    if (!s.dsOk) {
      feat_ds = false;
      prefs.putBool("feat_ds", false);
      s.tds = NAN;
    }
  } else {
    s.tds = NAN;
    s.dsOk = false;
  }

  if (feat_mlx) {
    s.tmlx = mlx.readObjectTempC();
    s.mlxOk = validTempMLX(s.tmlx);
    if (!s.mlxOk) {
      feat_mlx = false;
      prefs.putBool("feat_mlx", false);
      s.tmlx = NAN;
    }
  } else {
    s.tmlx = NAN;
    s.mlxOk = false;
  }

  if (feat_dht && dht) {
    s.tenv = dht->readTemperature();
    s.hum = dht->readHumidity();
    if (isnan(s.tenv) || isnan(s.hum)) {
      feat_dht = false;
      prefs.putBool("feat_dht", false);
      s.tenv = NAN;
      s.hum = NAN;
    }
  } else {
    s.tenv = NAN;
    s.hum = NAN;
  }

  if (feat_pzem && pzem) {
    s.voltage = pzem->voltage();
    s.current = pzem->current();
    s.pwr = pzem->power();
    if (isnan(s.voltage) || isnan(s.current) || isnan(s.pwr)) {
      feat_pzem = false;
      prefs.putBool("feat_pzem", false);
      s.voltage = NAN;
      s.current = NAN;
      s.pwr = NAN;
    }
  } else {
    s.voltage = NAN;
    s.current = NAN;
    s.pwr = NAN;
  }
  return s;
}

static bool isHotNow(const SensorFrame &s) {
  return (dsControlEnabled() && s.dsOk && s.tds >= max_temp_ds) ||
         (mlxControlEnabled() && s.mlxOk && s.tmlx >= max_temp_mlx) ||
         (envControlEnabled() && !isnan(s.tenv) && s.tenv >= max_temp_env);
}

static bool isHumidNow(const SensorFrame &s) {
  return humControlEnabled() && !isnan(s.hum) && s.hum >= max_humidity;
}

bool autoEnabled = true;
unsigned long nextStartMs = 0;
unsigned long fullLowStartMs = 0;
unsigned long lastControlMs = 0;
const unsigned long CONTROL_MS = 1000;

bool lockout = false;

unsigned long lastWiFiCheck = 0;
unsigned long wifiBackoffMs = 5000;

static void wifiAutoReconnect() {
  if (cfg_ssid[0] == '\0') return;

  if (WiFi.status() == WL_CONNECTED) {
    wifiBackoffMs = 5000;
    return;
  }

  unsigned long now = millis();
  if (now - lastWiFiCheck < wifiBackoffMs) return;
  lastWiFiCheck = now;

  WiFi.begin(cfg_ssid, cfg_pass);

  if (wifiBackoffMs < 60000UL) wifiBackoffMs *= 2;
  if (wifiBackoffMs > 60000UL) wifiBackoffMs = 60000UL;
}

static inline bool dsControlEnabled() { return feat_ds && max_temp_ds > 0.0f; }
static inline bool mlxControlEnabled() { return feat_mlx && max_temp_mlx > 0.0f; }
static inline bool envControlEnabled() { return feat_dht && max_temp_env > 0.0f; }
static inline bool humControlEnabled() { return feat_dht && max_humidity > 0.0f; }
static inline bool maxPowerEnabled() { return feat_pzem && max_power_w > 0.0f; }
static inline bool fullDetectEnabled() { return feat_pzem && full_power_threshold > 0.0f && measure_seconds > 0; }

static const char *lcdStationName() {
  if (lcd_name_from_server && server_station_name[0] != '\0') return server_station_name;
  if (custom_station_name[0] != '\0') return custom_station_name;
  if (server_station_name[0] != '\0') return server_station_name;
  return "SmartSac";
}

static bool serverConnectedForLcd() {
  if (WiFi.status() != WL_CONNECTED) return false;
  if (station_token[0] == '\0') return false;
  if (lastServerContactMs == 0) return false;
  return (millis() - lastServerContactMs) <= LCD_SERVER_ONLINE_MS;
}

static const char *batteryKindForLcd() {
  const char *kind = battery_kind_lcd;
  const char *colon = strrchr(kind, ':');
  if (!colon) return kind;

  colon++;
  while (*colon == ' ') colon++;
  return *colon ? colon : kind;
}

static void setBatteryKindFromType(const String &typeRaw) {
  String t = typeRaw;
  t.trim();
  t.toLowerCase();
  if (t == "lead" || t.indexOf("acquy") >= 0 || t.indexOf("aq") >= 0) {
    strlcpy(battery_kind_lcd, "AC QUY", sizeof(battery_kind_lcd));
  } else {
    strlcpy(battery_kind_lcd, "PIN", sizeof(battery_kind_lcd));
  }
}

static void selectAuxBus(AuxBus target) {
  if (activeAuxBus == target) return;

  if (activeAuxBus != AUX_BUS_NONE) {
    I2CAux.end();
    delayMicroseconds(200);
  }

  if (target == AUX_BUS_LCD) {
    I2CAux.begin(lcd_sda_pin, lcd_scl_pin, 100000);
  } else if (target == AUX_BUS_RTC) {
    I2CAux.begin(rtc_sda_pin, rtc_scl_pin, 100000);
  } else {
    activeAuxBus = AUX_BUS_NONE;
    return;
  }

  activeAuxBus = target;
  delayMicroseconds(200);
}

static void initRtc() {
  selectAuxBus(AUX_BUS_RTC);
  rtcReady = rtc.begin(&I2CAux);
  if (!rtcReady) return;

  if (rtc.lostPower()) {
    rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
  }

  DateTime now = rtc.now();
  if (!now.isValid()) {
    rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
    now = rtc.now();
  }

  rtcReady = now.isValid();
}

static void tgSend(const String &msg);

static inline void relaySet(bool on) {
  digitalWrite(relay_pin, on ? HIGH : LOW);
}

static inline void cancelProbe() {
  probeState = PROBE_IDLE;
  probeT0 = 0;
}

static void beginProbe(const char *src) {
  (void)src;
  if (probeState != PROBE_IDLE) return;
  probeState = PROBE_WAIT;
  probeT0 = millis();
  charging = false;
  chargeState = CS_PROBING;
  fullLowStartMs = 0;
  relaySet(true);
}

static void stopAll(const char *src) {
  (void)src;
  cancelProbe();
  if (chargeStartMs != 0) lastChargeDurMs = millis() - chargeStartMs;
  chargeStartMs = 0;

  autoEnabled = false;
  charging = false;
  if (safety_warning_msg[0] == '\0') {
    lockout = false;
    chargeState = CS_WAIT;
  }
  relaySet(false);
  fullLowStartMs = 0;
  nextStartMs = 0;
}

static void setWaitMode(const char *src) {
  (void)src;
  cancelProbe();
  if (chargeStartMs != 0) lastChargeDurMs = millis() - chargeStartMs;
  chargeStartMs = 0;

  autoEnabled = true;
  charging = false;
  if (safety_warning_msg[0] == '\0') {
    lockout = false;
  }
  if (chargeState == CS_CHARGING) chargeState = CS_WAIT;
  relaySet(false);
  fullLowStartMs = 0;
  nextStartMs = (wait_minutes == 0) ? millis() : (millis() + (unsigned long)wait_minutes * 60000UL);
}

static void startChargingNow(const char *src) {
  (void)src;
  if (charging && probeState == PROBE_IDLE && chargeState == CS_CHARGING) {
    relaySet(true);
    return;
  }

  cancelProbe();
  autoEnabled = true;
  lockout = false;
  charging = true;
  chargeState = CS_CHARGING;
  chargeStartMs = millis();
  lastChargeDurMs = 0;
  fullLowStartMs = 0;
  nextStartMs = 0;
  energy_on_start = pzem ? pzem->energy() : 0.0f;  // Record energy at start of charging
  relaySet(true);
}

static void stopWithReason(ChargeState reason, const char *tgMsg) {
  // Track energy usage
  if (charging || probeState != PROBE_IDLE) {
    float energy_now = pzem ? pzem->energy() : 0.0f;
    float energy_used = energy_now - energy_on_start;
    if (energy_used < 0.0f) energy_used = 0.0f;  // Guard against overflow
    total_energy_all += energy_used;
    
    if (reason == CS_STOP_FULL) {
      total_energy_full += energy_used;
      count_full++;
    } else if (reason == CS_STOP_TEMP) {
      total_energy_temp += energy_used;
      count_not_full++;
    } else if (reason == CS_STOP_HUMID) {
      total_energy_humid += energy_used;
      count_not_full++;
    } else {
      total_energy_not_full += energy_used;
      count_not_full++;
    }
  }
  
  cancelProbe();
  if (chargeStartMs != 0) lastChargeDurMs = millis() - chargeStartMs;
  chargeStartMs = 0;

  charging = false;
  chargeState = reason;
  relaySet(false);

  if (reason == CS_STOP_TEMP || reason == CS_STOP_HUMID || reason == CS_STOP_TIMEOUT || reason == CS_SENSOR_ERROR) {
    lockout = true;
  }

  fullLowStartMs = 0;

  if (autoEnabled && !lockout) {
    nextStartMs = (wait_minutes == 0) ? millis() : (millis() + (unsigned long)wait_minutes * 60000UL);
  } else {
    nextStartMs = 0;
  }

  if (tgMsg && tgMsg[0] != '\0') tgSend(String(tgMsg));
}

static const char *stateWebText() {
  if (!feat_ds && !feat_mlx && !feat_dht && !feat_pzem) {
    return "DANG HEN GIO";
  }
  switch (chargeState) {
    case CS_CHARGING: return "DANG SAC";
    case CS_PROBING: return "DANG DO";
    case CS_STOP_TEMP: return "NGAT DO NHIET";
    case CS_STOP_FULL: return "NGAT DO DAY";
    case CS_STOP_HUMID: return "NGAT DO AM";
    case CS_STOP_TIMEOUT: return "NGAT DO QUA GIO";
    case CS_STOP_POWER: return "NGAT DO QUA CONG SUAT";
    case CS_SENSOR_ERROR: return "NGAT DO LOI CAM BIEN";
    default: return "CHO SAC";
  }
}

#include "LcdModule.h"
#include "TelegramModule.h"
#include "CloudModule.h"
#include "WebModule.h"

enum InitState {
  INIT_WIFI_CONNECT = 0,
  INIT_WAIT_BEFORE_CHARGE,
  INIT_PREMEASURE,
  INIT_DECIDE,
  INIT_DONE
};

InitState initState = INIT_WIFI_CONNECT;
unsigned long initT0 = 0;
unsigned long initWaitMs = 0;
unsigned long initMeasureMs = 0;

#include "ControlModule.h"
#include "SetupModule.h"

static void initTask() {
  switch (initState) {
    case INIT_WIFI_CONNECT: {
      if (WiFi.status() == WL_CONNECTED) {
        initState = INIT_WAIT_BEFORE_CHARGE;
        initT0 = millis();
      } else if (millis() - initT0 >= 10000UL) {
        initState = INIT_WAIT_BEFORE_CHARGE;
        initT0 = millis();
      }
      break;
    }

    case INIT_WAIT_BEFORE_CHARGE: {
      if (initWaitMs == 0 || (millis() - initT0 >= initWaitMs)) {
        initState = INIT_PREMEASURE;
        initT0 = millis();
        relaySet(true);
        chargeState = CS_PROBING;
      }
      break;
    }

    case INIT_PREMEASURE: {
      if (millis() - initT0 >= initMeasureMs) {
        initState = INIT_DECIDE;
      }
      break;
    }

    case INIT_DECIDE: {
      SensorFrame s = readSensorFrame();
      float pwr = s.pwr;

      bool hot = isHotNow(s);
      bool humid = isHumidNow(s);

      bool dsFault = dsControlEnabled() && !s.dsOk;
      bool mlxFault = mlxControlEnabled() && !s.mlxOk;

      dsBadCount = dsControlEnabled() ? (s.dsOk ? 0 : DS_BAD_LIMIT) : 0;
      mlxBadCount = mlxControlEnabled() ? (s.mlxOk ? 0 : MLX_BAD_LIMIT) : 0;
      pzemBadCount = isnan(pwr) ? PZEM_BAD_LIMIT : 0;

      if (dsFault || mlxFault) {
        stopWithReason(CS_SENSOR_ERROR, nullptr);
      } else if (isnan(pwr)) {
        stopWithReason(CS_SENSOR_ERROR, nullptr);
      } else if (maxPowerEnabled() && pwr > max_power_w) {
        stopWithReason(CS_STOP_POWER, nullptr);
      } else if (hot) {
        stopWithReason(CS_STOP_TEMP, nullptr);
      } else if (humid) {
        stopWithReason(CS_STOP_HUMID, nullptr);
      } else if (fullDetectEnabled() && pwr <= full_power_threshold) {
        stopWithReason(CS_STOP_FULL, nullptr);
      } else {
        startChargingNow("INIT");
      }

      initState = INIT_DONE;
      break;
    }

    case INIT_DONE:
    default:
      break;
  }
}

void setup() {
  initBootServices();
  loadPersistentConfig();
  initWaitMs = (unsigned long)wait_minutes * 60000UL;
  initMeasureMs = PROBE_ON_MS;
  initPinsAndSensors();
  initWiFiStack();
  initWebServerRoutes();
  initCloudTimers();
}

static void apTimeoutTask() {
  static bool apTurnedOff = false;
  if (apTurnedOff) return;
  if (feat_ap_on && !feat_ap_always) {
    if (millis() >= 300000UL) { // 5 minutes
      WiFi.softAPdisconnect(true);
      apTurnedOff = true;
    }
  }
}

void loop() {
  server.handleClient();
  wifiAutoReconnect();
  cloudSyncTask();

  initTask();

  telegramPoll();
  telegramAlertTask();
  apTimeoutTask();

  if (chargeState != lastChargeState) {
    lastChargeState = chargeState;
  }
  lcdTask();

  handleButtonsTask();
  chargingControlTask();
}
