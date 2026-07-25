#ifndef SENSOR_FRAME_H
#define SENSOR_FRAME_H

struct SensorFrame {
  float tds;
  float tmlx;
  float tenv;
  float hum;
  float voltage;
  float current;
  float pwr;
  bool dsOk;
  bool mlxOk;
};

#endif

