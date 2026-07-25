#ifndef CONTROL_MODULE_H
#define CONTROL_MODULE_H

static void handleButtonsTask() {
  static bool btn3LastHigh = true;
  static unsigned long btn3LastMs = 0;
  bool btn3High = (digitalRead(btn_pin) == HIGH);
  if (btn3LastHigh && !btn3High) {
    unsigned long now = millis();
    if (now - btn3LastMs > 150UL) {
      btn3LastMs = now;

      // Manual button press should always have priority over init sequence.
      if (initState != INIT_DONE) {
        initState = INIT_DONE;
      }

      if (charging || probeState != PROBE_IDLE || digitalRead(relay_pin) == HIGH) {
        safety_warning_msg[0] = '\0';
        stopAll("BTN3");
        tgSend("Da dung sac bang nut BTN3");
      } else {
        safety_warning_msg[0] = '\0';
        startChargingNow("BTN3");
        tgSend("Da bat sac ngay bang nut BTN3");
      }
    }
  }
  btn3LastHigh = btn3High;

  if (btn_restore_pin != relay_pin) {
    static unsigned long rstDown = 0;
    bool rst = (digitalRead(btn_restore_pin) == LOW);
    if (rst) {
      if (rstDown == 0) rstDown = millis();
      if (millis() - rstDown > 1500) {
        prefs.clear();
        delay(150);
        ESP.restart();
      }
    } else {
      rstDown = 0;
    }
  }
}

static void chargingControlTask() {
  if (initState != INIT_DONE || millis() - lastControlMs < CONTROL_MS) return;
  lastControlMs = millis();

  SensorFrame s = readSensorFrame();
  float pwr = s.pwr;

  dsBadCount = dsControlEnabled() ? (s.dsOk ? 0 : (uint8_t)(dsBadCount + 1)) : 0;
  mlxBadCount = mlxControlEnabled() ? (s.mlxOk ? 0 : (uint8_t)(mlxBadCount + 1)) : 0;
  pzemBadCount = isnan(pwr) ? (uint8_t)(pzemBadCount + 1) : 0;

  if (dsBadCount > 200) dsBadCount = 200;
  if (mlxBadCount > 200) mlxBadCount = 200;
  if (pzemBadCount > 200) pzemBadCount = 200;

  bool hot = isHotNow(s);
  bool humid = isHumidNow(s);

  bool dsFault = dsControlEnabled() && (dsBadCount >= DS_BAD_LIMIT);
  bool mlxFault = mlxControlEnabled() && (mlxBadCount >= MLX_BAD_LIMIT);

  if (probeState != PROBE_IDLE) {
    if (dsFault || mlxFault || pzemBadCount >= PZEM_BAD_LIMIT) {
      cancelProbe();
      stopWithReason(CS_SENSOR_ERROR, "❌ Loi cam bien/PZEM -> ngat sac");
    } else if (maxPowerEnabled() && !isnan(pwr) && pwr > max_power_w) {
      cancelProbe();
      stopWithReason(CS_STOP_POWER, "⚡ Cong suat vuot nguong toi da -> ngat sac");
    } else if (hot) {
      cancelProbe();
      stopWithReason(CS_STOP_TEMP, "🔥 Qua nhiet -> ngat sac");
    } else if (humid) {
      cancelProbe();
      stopWithReason(CS_STOP_HUMID, "💧 Do am cao -> ngat sac");
    } else if ((long)(millis() - (probeT0 + PROBE_ON_MS)) >= 0) {
      cancelProbe();
      relaySet(false);

      if (isnan(pwr)) {
        stopWithReason(CS_SENSOR_ERROR, "❌ Loi PZEM -> ngat sac");
      } else if (!fullDetectEnabled() || pwr > full_power_threshold) {
        startChargingNow("AUTO");
      } else {
        stopWithReason(CS_STOP_FULL, nullptr);
      }
    }
    return;
  }

  if (charging) {
    if (max_charge_hours > 0 && chargeElapsedMs() >= (unsigned long)max_charge_hours * 3600000UL) {
      stopWithReason(CS_STOP_TIMEOUT, "⏰ Qua thoi gian sac (gioi han) -> ngat sac");
    } else if (dsFault || mlxFault) {
      stopWithReason(CS_SENSOR_ERROR, "❌ Loi cam bien nhiet -> ngat sac");
    } else if (pzemBadCount >= PZEM_BAD_LIMIT) {
      stopWithReason(CS_SENSOR_ERROR, "❌ Loi PZEM -> ngat sac");
    } else if (maxPowerEnabled() && !isnan(pwr) && pwr > max_power_w) {
      stopWithReason(CS_STOP_POWER, "⚡ Cong suat vuot nguong toi da -> ngat sac");
    } else if (hot) {
      stopWithReason(CS_STOP_TEMP, "🔥 Qua nhiet -> ngat sac");
    } else if (humid) {
      stopWithReason(CS_STOP_HUMID, "💧 Do am cao -> ngat sac");
    } else if (fullDetectEnabled() && !isnan(pwr) && pwr <= full_power_threshold) {
      if (fullLowStartMs == 0) fullLowStartMs = millis();
      if (millis() - fullLowStartMs >= (unsigned long)measure_seconds * 1000UL) {
        stopWithReason(CS_STOP_FULL, "✅ Pin day (P thap du thoi gian) -> ngat sac");
      }
    } else {
      fullLowStartMs = 0;
    }
    return;
  }

  if (!autoEnabled || lockout) return;

  if (nextStartMs == 0) {
    nextStartMs = (wait_minutes == 0) ? millis() : (millis() + (unsigned long)wait_minutes * 60000UL);
  }

  if ((long)(millis() - nextStartMs) < 0) return;

  if (hot || humid) {
    nextStartMs = millis() + 5UL * 60000UL;
  } else if (dsFault || mlxFault || pzemBadCount >= PZEM_BAD_LIMIT) {
    chargeState = CS_SENSOR_ERROR;
    relaySet(false);
    nextStartMs = millis() + 5UL * 60000UL;
  } else {
    beginProbe("AUTO");
  }
}

#endif
