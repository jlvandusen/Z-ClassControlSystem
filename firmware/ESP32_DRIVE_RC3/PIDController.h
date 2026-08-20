
#ifndef PIDCONTROLLER_H
#define PIDCONTROLLER_H

#include <Arduino.h>

class PIDController {
public:
  PIDController(float kp, float ki, float kd);
  void setTunings(float newKp, float newKi, float newKd);
  void reset();
  float compute(float error);

  float getKp() const {
    return kp;
  }
  float getKi() const {
    return ki;
  }
  float getKd() const {
    return kd;
  }


private:
  float kp, ki, kd;
  float integral;
  float lastError;
  unsigned long lastUpdate;
};

#endif
