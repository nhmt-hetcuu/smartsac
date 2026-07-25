#ifndef CONFIG_MODULE_H
#define CONFIG_MODULE_H

#include "FeaturesConfig.h"
#include "PinConfig.h"

bool feat_ds = DEFAULT_ENABLE_DS18B20;
bool feat_mlx = DEFAULT_ENABLE_MLX90614;
bool feat_dht = DEFAULT_ENABLE_DHT;
bool feat_pzem = DEFAULT_ENABLE_PZEM;
bool feat_tg = DEFAULT_ENABLE_TELEGRAM;
bool feat_cloud = DEFAULT_ENABLE_CLOUD;
bool feat_ap_on = DEFAULT_ENABLE_AP_ON;
bool feat_ap_always = DEFAULT_ENABLE_AP_ALWAYS;

uint8_t pzem_rx_pin = DEFAULT_PZEM_RX_PIN;
uint8_t pzem_tx_pin = DEFAULT_PZEM_TX_PIN;
uint8_t dht_pin = DEFAULT_DHT_PIN;
uint8_t ds18b20_pin = DEFAULT_DS18B20_PIN;
uint8_t mlx_sda_pin = DEFAULT_MLX_SDA_PIN;
uint8_t mlx_scl_pin = DEFAULT_MLX_SCL_PIN;
uint8_t lcd_sda_pin = DEFAULT_LCD_SDA_PIN;
uint8_t lcd_scl_pin = DEFAULT_LCD_SCL_PIN;
uint8_t rtc_sda_pin = DEFAULT_RTC_SDA_PIN;
uint8_t rtc_scl_pin = DEFAULT_RTC_SCL_PIN;
uint8_t relay_pin = DEFAULT_RELAY_PIN;
uint8_t btn_pin = DEFAULT_BTN_PIN;
uint8_t btn_restore_pin = DEFAULT_BTN_RESTORE_PIN;

// Forward declaration from Telegram module.
static void parseTgMix(const String &mix);

const uint16_t DEFAULT_WAIT_MIN = 60;
const uint16_t DEFAULT_MEASURE_SEC = 300;
const float DEFAULT_FULL_POWER_THRESHOLD = 10.0f;
const float DEFAULT_MIN_POWER_W = DEFAULT_FULL_POWER_THRESHOLD;
const float DEFAULT_MAX_POWER_W = 0.0f;
const float DEFAULT_WARN_TEMP_DS = 45.0f;
const float DEFAULT_MAX_TEMP_DS = 55.0f;
const float DEFAULT_WARN_TEMP_MLX = 45.0f;
const float DEFAULT_MAX_TEMP_MLX = 55.0f;
const float DEFAULT_WARN_TEMP_ENV = 40.0f;
const float DEFAULT_MAX_TEMP_ENV = 45.0f;
const float DEFAULT_MAX_HUMIDITY = 85.0f;
const uint16_t DEFAULT_MAX_CHARGE_HOURS = 10;

uint16_t wait_minutes = DEFAULT_WAIT_MIN;
uint16_t measure_seconds = DEFAULT_MEASURE_SEC;
float full_power_threshold = DEFAULT_FULL_POWER_THRESHOLD;
float min_power_w = DEFAULT_MIN_POWER_W;
float max_power_w = DEFAULT_MAX_POWER_W;
float warn_temp_ds = DEFAULT_WARN_TEMP_DS;
float max_temp_ds = DEFAULT_MAX_TEMP_DS;
float warn_temp_mlx = DEFAULT_WARN_TEMP_MLX;
float max_temp_mlx = DEFAULT_MAX_TEMP_MLX;
float warn_temp_env = DEFAULT_WARN_TEMP_ENV;
float max_temp_env = DEFAULT_MAX_TEMP_ENV;
float max_humidity = DEFAULT_MAX_HUMIDITY;
uint16_t max_charge_hours = DEFAULT_MAX_CHARGE_HOURS;

char custom_station_name[33] = "";
char server_station_name[33] = "";
bool lcd_name_from_server = true;
char battery_kind_lcd[8] = "AC QUY";
uint16_t battery_capacity_ah_lcd = 20;
float output_a_lcd = 3.0f;

// Energy tracking (kWh) for different charge statuses
float total_energy_all = 0.0f;       // Total energy across all charges
float energy_on_start = 0.0f;        // Energy reading when charging starts
float total_energy_full = 0.0f;      // Energy used when stopped (FULL)
float total_energy_not_full = 0.0f;  // Energy used when stopped (not full - other reasons)
float total_energy_temp = 0.0f;      // Energy used when stopped (TEMP)
float total_energy_humid = 0.0f;     // Energy used when stopped (HUMID)
uint32_t count_full = 0;             // Count of charges stopped due to full
uint32_t count_not_full = 0;         // Count of charges stopped for other reasons

char cfg_ssid[128] = {0};
char cfg_pass[128] = {0};
char ap_ssid[33] = "SmartSac";
char ap_pass[65] = "12345678";

static const char API_BASE_URL[] = "https://khkt.hcuu.xyz";
static const char API_TELEMETRY_PATH[] = "/api/station/device/telemetry";
static const char API_CONFIG_PATH[] = "/api/station/device/config";
static const char DEFAULT_STATION_TOKEN[] = "SK_CHANGE_ME";
char station_token[128] = {0};
char safety_warning_msg[160] = {0};

unsigned long lastApiTelemetryMs = 0;
unsigned long lastApiConfigMs = 0;
const unsigned long API_TELEMETRY_INTERVAL_MS = 3000UL;
const unsigned long API_CONFIG_INTERVAL_MS = 7000UL;
const unsigned long API_RETRY_ON_FAIL_MS = 1200UL;
const unsigned long API_FULL_KEEPALIVE_MS = 30000UL;
unsigned long lastServerContactMs = 0;
const unsigned long LCD_SERVER_ONLINE_MS = API_CONFIG_INTERVAL_MS + 2UL * API_RETRY_ON_FAIL_MS + 5000UL;
bool fullTelemetrySentOnce = false;

static inline float configClampNonNegative(float value) {
  return value < 0.0f ? 0.0f : value;
}

static void loadPersistentConfig() {
  String tg_mix = prefs.getString("tg_mix", "");
  parseTgMix(tg_mix);

  String ss = prefs.getString("ssid", "");
  String pa = prefs.getString("pass", "");
  String api_key = prefs.getString("api_key", DEFAULT_STATION_TOKEN);
  String aps = prefs.getString("ap_ssid", ap_ssid);
  String app = prefs.getString("ap_pass", ap_pass);

  api_key.trim();
  if (api_key.length() == 0) api_key = DEFAULT_STATION_TOKEN;

  ss.toCharArray(cfg_ssid, sizeof(cfg_ssid));
  pa.toCharArray(cfg_pass, sizeof(cfg_pass));
  api_key.toCharArray(station_token, sizeof(station_token));
  aps.toCharArray(ap_ssid, sizeof(ap_ssid));
  app.toCharArray(ap_pass, sizeof(ap_pass));

  if (strlen(ap_ssid) == 0) {
    strlcpy(ap_ssid, "Smart Sac", sizeof(ap_ssid));
  }
  if (strlen(ap_pass) < 8) {
    strlcpy(ap_pass, "12345678", sizeof(ap_pass));
  }

  wait_minutes = prefs.getInt("wait", DEFAULT_WAIT_MIN);
  measure_seconds = prefs.getInt("measure", DEFAULT_MEASURE_SEC);
  min_power_w = prefs.getFloat("p_min", prefs.getFloat("full", DEFAULT_MIN_POWER_W));
  if (min_power_w < 0.0f) min_power_w = 0.0f;
  full_power_threshold = min_power_w;
  max_power_w = prefs.getFloat("p_max", DEFAULT_MAX_POWER_W);
  if (max_power_w < 0.0f) max_power_w = 0.0f;
  max_temp_ds = configClampNonNegative(prefs.getFloat("t_ds", DEFAULT_MAX_TEMP_DS));
  max_temp_mlx = configClampNonNegative(prefs.getFloat("t_mlx", DEFAULT_MAX_TEMP_MLX));
  max_temp_env = configClampNonNegative(prefs.getFloat("t_env", DEFAULT_MAX_TEMP_ENV));
  max_humidity = configClampNonNegative(prefs.getFloat("h_max", DEFAULT_MAX_HUMIDITY));

  warn_temp_ds = configClampNonNegative(prefs.getFloat("tw_ds", DEFAULT_WARN_TEMP_DS));
  warn_temp_mlx = configClampNonNegative(prefs.getFloat("tw_mlx", DEFAULT_WARN_TEMP_MLX));
  warn_temp_env = configClampNonNegative(prefs.getFloat("tw_env", DEFAULT_WARN_TEMP_ENV));
  max_charge_hours = (uint16_t)prefs.getInt("max_h", DEFAULT_MAX_CHARGE_HOURS);

  String savedLcdName = prefs.getString("lcd_name", "");
  strlcpy(custom_station_name, savedLcdName.c_str(), sizeof(custom_station_name));
  String savedServerName = prefs.getString("server_name", "");
  strlcpy(server_station_name, savedServerName.c_str(), sizeof(server_station_name));
  lcd_name_from_server = prefs.getInt("lcd_srv", 1) == 1;

  String savedBatKind = prefs.getString("bat_kind", "AC QUY");
  strlcpy(battery_kind_lcd, savedBatKind.c_str(), sizeof(battery_kind_lcd));
  if (strcmp(battery_kind_lcd, "AC QUY") != 0 && strcmp(battery_kind_lcd, "PIN") != 0) {
    strlcpy(battery_kind_lcd, "AC QUY", sizeof(battery_kind_lcd));
  }

  int savedBatAh = prefs.getInt("bat_ah", 20);
  if (savedBatAh < 1) savedBatAh = 20;
  if (savedBatAh > 1000) savedBatAh = 1000;
  battery_capacity_ah_lcd = (uint16_t)savedBatAh;

  output_a_lcd = prefs.getFloat("out_a_lcd", 3.0f);
  if (output_a_lcd < 0.0f) output_a_lcd = 0.0f;
  if (output_a_lcd > 100.0f) output_a_lcd = 100.0f;

  feat_ds = prefs.getBool("feat_ds", DEFAULT_ENABLE_DS18B20);
  feat_mlx = prefs.getBool("feat_mlx", DEFAULT_ENABLE_MLX90614);
  feat_dht = prefs.getBool("feat_dht", DEFAULT_ENABLE_DHT);
  feat_pzem = prefs.getBool("feat_pzem", DEFAULT_ENABLE_PZEM);
  feat_tg = prefs.getBool("feat_tg", DEFAULT_ENABLE_TELEGRAM);
  feat_cloud = prefs.getBool("feat_cloud", DEFAULT_ENABLE_CLOUD);
  feat_ap_on = prefs.getBool("feat_ap_on", DEFAULT_ENABLE_AP_ON);
  feat_ap_always = prefs.getBool("feat_ap_alw", DEFAULT_ENABLE_AP_ALWAYS);

  pzem_rx_pin = prefs.getUChar("p_rx", DEFAULT_PZEM_RX_PIN);
  pzem_tx_pin = prefs.getUChar("p_tx", DEFAULT_PZEM_TX_PIN);
  dht_pin = prefs.getUChar("d_pin", DEFAULT_DHT_PIN);
  ds18b20_pin = prefs.getUChar("ds_pin", DEFAULT_DS18B20_PIN);
  mlx_sda_pin = prefs.getUChar("m_sda", DEFAULT_MLX_SDA_PIN);
  mlx_scl_pin = prefs.getUChar("m_scl", DEFAULT_MLX_SCL_PIN);
  lcd_sda_pin = prefs.getUChar("l_sda", DEFAULT_LCD_SDA_PIN);
  lcd_scl_pin = prefs.getUChar("l_scl", DEFAULT_LCD_SCL_PIN);
  rtc_sda_pin = prefs.getUChar("r_sda", DEFAULT_RTC_SDA_PIN);
  rtc_scl_pin = prefs.getUChar("r_scl", DEFAULT_RTC_SCL_PIN);
  relay_pin = prefs.getUChar("rel_pin", DEFAULT_RELAY_PIN);
  btn_pin = prefs.getUChar("btn_pin", DEFAULT_BTN_PIN);
  btn_restore_pin = prefs.getUChar("btn_rst", DEFAULT_BTN_RESTORE_PIN);
}

#endif
