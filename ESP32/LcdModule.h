#ifndef LCD_MODULE_H
#define LCD_MODULE_H

static uint8_t lcd_backlight = 0x08;

static void lcdExpWrite(uint8_t data) {
  selectAuxBus(AUX_BUS_LCD);
  I2CAux.beginTransmission(LCD_ADDR);
  I2CAux.write(data | lcd_backlight);
  I2CAux.endTransmission();
}

static void lcdPulseEnable(uint8_t data) {
  lcdExpWrite(data | 0x04);
  delayMicroseconds(1);
  lcdExpWrite(data & ~0x04);
  delayMicroseconds(50);
}

static void lcdWrite4(uint8_t nibble, uint8_t mode) {
  uint8_t data = (nibble & 0xF0) | mode;
  lcdExpWrite(data);
  lcdPulseEnable(data);
}

static void lcdSend(uint8_t value, uint8_t mode) {
  lcdWrite4(value & 0xF0, mode);
  lcdWrite4((value << 4) & 0xF0, mode);
}

static void lcdCommand(uint8_t cmd) { lcdSend(cmd, 0x00); }
static void lcdChar(char c) { lcdSend((uint8_t)c, 0x01); }

static void lcdClear() {
  lcdCommand(0x01);
  delayMicroseconds(2000);
}

static void lcdSetCursor(uint8_t col, uint8_t row) {
  static const uint8_t row_offsets[] = {0x00, 0x40, 0x14, 0x54};
  if (row >= LCD_ROWS) row = LCD_ROWS - 1;
  lcdCommand(0x80 | (col + row_offsets[row]));
}

static void lcdPrint(const char *s) {
  while (*s) lcdChar(*s++);
}

static void lcdInit() {
  delay(50);
  lcdExpWrite(0x00);
  delay(10);

  lcdWrite4(0x30, 0x00);
  delayMicroseconds(4500);
  lcdWrite4(0x30, 0x00);
  delayMicroseconds(4500);
  lcdWrite4(0x30, 0x00);
  delayMicroseconds(150);
  lcdWrite4(0x20, 0x00);

  lcdCommand(0x28);
  lcdCommand(0x08);
  lcdClear();
  lcdCommand(0x06);
  lcdCommand(0x0C);
}

static const char *stateLcdText() {
  if (!feat_ds && !feat_mlx && !feat_dht && !feat_pzem) {
    return "DANG HEN GIO";
  }
  switch (chargeState) {
    case CS_CHARGING: return "DANG SAC";
    case CS_PROBING: return "DANG DO";
    case CS_STOP_TEMP: return "NGAT NHIET";
    case CS_STOP_FULL: return "NGAT DAY";
    case CS_STOP_HUMID: return "NGAT DO AM";
    case CS_STOP_TIMEOUT: return "NGAT QUA GIO";
    case CS_STOP_POWER: return "NGAT QUA P";
    case CS_SENSOR_ERROR: return "LOI CAM BIEN";
    default: return "CHO SAC";
  }
}

static void lcdPrintCenteredRow(uint8_t row, const char *text) {
  if (row >= LCD_ROWS) return;

  char line[21];
  memset(line, ' ', LCD_COLS);
  line[LCD_COLS] = '\0';

  int textLen = (int)strlen(text);
  if (textLen > LCD_COLS) textLen = LCD_COLS;

  int start = (LCD_COLS - textLen) / 2;
  if (start < 0) start = 0;

  memcpy(line + start, text, textLen);
  lcdSetCursor(0, row);
  lcdPrint(line);
}

static void lcdTask() {
  if (millis() - lastLcdMs < LCD_UPDATE_MS) return;
  lastLcdMs = millis();

  if (safety_warning_msg[0] != '\0') {
    char line[21];
    for (uint8_t r = 0; r < LCD_ROWS; r++) {
      int offset = r * LCD_COLS;
      int len = (int)strlen(safety_warning_msg);
      if (offset < len) {
        int copyLen = len - offset;
        if (copyLen > LCD_COLS) copyLen = LCD_COLS;
        memcpy(line, safety_warning_msg + offset, copyLen);
        line[copyLen] = '\0';
        lcdPrintCenteredRow(r, line);
      } else {
        lcdPrintCenteredRow(r, "");
      }
    }
    return;
  }

  float tenv = dht.readTemperature();
  float hum = dht.readHumidity();

  char l1[21];
  char l2[21];
  char l3[21];
  char l4[21];

  snprintf(l1, sizeof(l1), "%s", lcdStationName());
  
  // Line 2: Show electricity meter if status is FULL, otherwise show status
  if (chargeState == CS_STOP_FULL) {
    // Display total electricity meter on line 2
    if (feat_pzem) {
      snprintf(l2, sizeof(l2), "Dien:%5.2f kWh", total_energy_all);
    } else {
      snprintf(l2, sizeof(l2), "Dien:N/A");
    }
  } else {
    snprintf(l2, sizeof(l2), "TT:%s", stateLcdText());
  }

  unsigned long min = chargeElapsedMs() / 60000UL;
  char mt_str[10];
  char da_str[10];
  if (feat_dht) {
    snprintf(mt_str, sizeof(mt_str), "%4.1f", isnan(tenv) ? 0.0f : tenv);
    snprintf(da_str, sizeof(da_str), "%2.0f", isnan(hum) ? 0.0f : hum);
  } else {
    strlcpy(mt_str, "N/A", sizeof(mt_str));
    strlcpy(da_str, "N/A", sizeof(da_str));
  }
  snprintf(l3, sizeof(l3), "T:%lum MT:%s DA:%s", (unsigned long)min, mt_str, da_str);

  if (serverConnectedForLcd()) {
    snprintf(l4, sizeof(l4), "%s %uAh", batteryKindForLcd(), (unsigned int)battery_capacity_ah_lcd);
  } else {
    l4[0] = '\0';
  }

  lcdPrintCenteredRow(0, l1);

  lcdSetCursor(0, 1);
  lcdPrint("                    ");
  lcdSetCursor(0, 1);
  lcdPrint(l2);

  if (LCD_ROWS > 2) {
    lcdSetCursor(0, 2);
    lcdPrint("                    ");
    lcdSetCursor(0, 2);
    lcdPrint(l3);

    lcdSetCursor(0, 3);
    lcdPrint("                    ");
    lcdSetCursor(0, 3);
    lcdPrint(l4);
  }
}

#endif
