<<<<<<< HEAD
#ifndef SETUP_MODULE_H
#define SETUP_MODULE_H

static void initBootServices() {
  tgClient.setInsecure();
  delay(50);

  prefs.begin("cfg", false);
}

static void initPinsAndSensors() {
  pinMode(relay_pin, OUTPUT);
  pinMode(btn_pin, INPUT_PULLUP);
  if (btn_restore_pin != relay_pin) {
    pinMode(btn_restore_pin, INPUT_PULLUP);
  }
  relaySet(false);

  oneWire = new OneWire(ds18b20_pin);
  dallas = new DallasTemperature(oneWire);
  dallas->begin();
  Wire.begin(mlx_sda_pin, mlx_scl_pin);
  mlx.begin();

  initRtc();

  selectAuxBus(AUX_BUS_LCD);
  lcdInit();
  lcdClear();
  lcdSetCursor(0, 0);
  lcdPrint("Smart Charger");
  lcdSetCursor(0, 1);
  lcdPrint("Initializing...");

  dht = new DHT(dht_pin, DHT_TYPE);
  dht->begin();
  pzem = new PZEM004Tv30(&PZEMSerial, pzem_rx_pin, pzem_tx_pin);
  PZEMSerial.begin(9600, SERIAL_8N1, pzem_rx_pin, pzem_tx_pin);
}

static void initWiFiStack() {
  WiFi.mode(WIFI_AP_STA);
  WiFi.setAutoReconnect(true);
  WiFi.persistent(false);
  WiFi.setSleep(false);
  if (feat_ap_on) {
    WiFi.softAP(ap_ssid, ap_pass);
  } else {
    WiFi.mode(WIFI_STA);
  }

  initT0 = millis();
  if (cfg_ssid[0] != '\0') {
    WiFi.begin(cfg_ssid, cfg_pass);
    initState = INIT_WIFI_CONNECT;
  } else {
    initState = INIT_WAIT_BEFORE_CHARGE;
    initT0 = millis();
  }
}

static bool checkWebAuth() {
  if (WiFi.status() == WL_CONNECTED && server.client().localIP() == WiFi.localIP()) {
    server.send(403, "text/plain", "Forbidden: Access only allowed via AP WiFi");
    return false;
  }
  const char* pass = (cfg_pass[0] != '\0') ? cfg_pass : ap_pass;
  if (pass[0] != '\0') {
    if (!server.authenticate("admin", pass)) {
      server.requestAuthentication();
      return false;
    }
  }
  return true;
}

static void initWebServerRoutes() {
  server.on("/", []() {
    if (!checkWebAuth()) return;
    handleRoot();
  });
  server.on("/setting", []() {
    if (!checkWebAuth()) return;
    handleSetting();
  });
  server.on("/save", HTTP_POST, []() {
    if (!checkWebAuth()) return;
    handleSave();
  });

  server.on("/reset", []() {
    if (!checkWebAuth()) return;
    prefs.clear();
    server.send(200, "text/plain", "OK");
    delay(200);
    ESP.restart();
  });
  server.on("/reboot", []() {
    if (!checkWebAuth()) return;
    server.send(200, "text/plain", "OK");
    delay(200);
    ESP.restart();
  });

  server.on("/data", []() {
    if (!checkWebAuth()) return;
    handleData();
  });
  server.on("/charge_wait", HTTP_POST, []() {
    if (!checkWebAuth()) return;
    safety_warning_msg[0] = '\0';
    setWaitMode("WEB");
    server.send(200, "text/plain", "WAIT");
  });
  server.on("/charge_now", HTTP_POST, []() {
    if (!checkWebAuth()) return;
    safety_warning_msg[0] = '\0';
    startChargingNow("WEB");
    server.send(200, "text/plain", "NOW");
  });
  server.on("/charge_stop", HTTP_POST, []() {
    if (!checkWebAuth()) return;
    safety_warning_msg[0] = '\0';
    stopAll("WEB");
    server.send(200, "text/plain", "STOP");
  });

  server.begin();
}

static void initCloudTimers() {
  unsigned long now = millis();
  lastApiTelemetryMs = now - API_TELEMETRY_INTERVAL_MS;
  lastApiConfigMs = now - API_CONFIG_INTERVAL_MS;
}

#endif
=======
#ifndef SETUP_MODULE_H
#define SETUP_MODULE_H

static void initBootServices() {
  tgClient.setInsecure();
  delay(50);

  prefs.begin("cfg", false);
}

static void initPinsAndSensors() {
  pinMode(relay_pin, OUTPUT);
  pinMode(btn_pin, INPUT_PULLUP);
  if (btn_restore_pin != relay_pin) {
    pinMode(btn_restore_pin, INPUT_PULLUP);
  }
  relaySet(false);

  oneWire = new OneWire(ds18b20_pin);
  dallas = new DallasTemperature(oneWire);
  dallas->begin();
  Wire.begin(mlx_sda_pin, mlx_scl_pin);
  mlx.begin();

  initRtc();

  selectAuxBus(AUX_BUS_LCD);
  lcdInit();
  lcdClear();
  lcdSetCursor(0, 0);
  lcdPrint("Smart Charger");
  lcdSetCursor(0, 1);
  lcdPrint("Initializing...");

  dht = new DHT(dht_pin, DHT_TYPE);
  dht->begin();
  pzem = new PZEM004Tv30(&PZEMSerial, pzem_rx_pin, pzem_tx_pin);
  PZEMSerial.begin(9600, SERIAL_8N1, pzem_rx_pin, pzem_tx_pin);
}

static void initWiFiStack() {
  WiFi.mode(WIFI_AP_STA);
  WiFi.setAutoReconnect(true);
  WiFi.persistent(false);
  WiFi.setSleep(false);
  if (feat_ap_on) {
    WiFi.softAP(ap_ssid, ap_pass);
  } else {
    WiFi.mode(WIFI_STA);
  }

  initT0 = millis();
  if (cfg_ssid[0] != '\0') {
    WiFi.begin(cfg_ssid, cfg_pass);
    initState = INIT_WIFI_CONNECT;
  } else {
    initState = INIT_WAIT_BEFORE_CHARGE;
    initT0 = millis();
  }
}

static bool checkWebAuth() {
  if (WiFi.status() == WL_CONNECTED && server.client().localIP() == WiFi.localIP()) {
    server.send(403, "text/plain", "Forbidden: Access only allowed via AP WiFi");
    return false;
  }
  const char* pass = (cfg_pass[0] != '\0') ? cfg_pass : ap_pass;
  if (pass[0] != '\0') {
    if (!server.authenticate("admin", pass)) {
      server.requestAuthentication();
      return false;
    }
  }
  return true;
}

static void initWebServerRoutes() {
  server.on("/", []() {
    if (!checkWebAuth()) return;
    handleRoot();
  });
  server.on("/setting", []() {
    if (!checkWebAuth()) return;
    handleSetting();
  });
  server.on("/save", HTTP_POST, []() {
    if (!checkWebAuth()) return;
    handleSave();
  });

  server.on("/reset", []() {
    if (!checkWebAuth()) return;
    prefs.clear();
    server.send(200, "text/plain", "OK");
    delay(200);
    ESP.restart();
  });
  server.on("/reboot", []() {
    if (!checkWebAuth()) return;
    server.send(200, "text/plain", "OK");
    delay(200);
    ESP.restart();
  });

  server.on("/data", []() {
    if (!checkWebAuth()) return;
    handleData();
  });
  server.on("/charge_wait", HTTP_POST, []() {
    if (!checkWebAuth()) return;
    safety_warning_msg[0] = '\0';
    setWaitMode("WEB");
    server.send(200, "text/plain", "WAIT");
  });
  server.on("/charge_now", HTTP_POST, []() {
    if (!checkWebAuth()) return;
    safety_warning_msg[0] = '\0';
    startChargingNow("WEB");
    server.send(200, "text/plain", "NOW");
  });
  server.on("/charge_stop", HTTP_POST, []() {
    if (!checkWebAuth()) return;
    safety_warning_msg[0] = '\0';
    stopAll("WEB");
    server.send(200, "text/plain", "STOP");
  });

  server.begin();
}

static void initCloudTimers() {
  unsigned long now = millis();
  lastApiTelemetryMs = now - API_TELEMETRY_INTERVAL_MS;
  lastApiConfigMs = now - API_CONFIG_INTERVAL_MS;
}

#endif
>>>>>>> c33c85add95660613ecc968467e618cd78c9a62f
