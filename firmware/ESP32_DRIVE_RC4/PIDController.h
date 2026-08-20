
#ifndef PIDCONTROLLER_H
#define PIDCONTROLLER_H

#include <Arduino.h>

// RC4: proper PID for a balance loop.
//  - caller supplies dt (run it IMU-synchronously, not at loop rate)
//  - derivative on MEASUREMENT (no derivative kick on setpoint steps)
//    with a first-order low-pass filter (default tau = 40 ms)
//  - integral clamped to the output limits (anti-windup)
//  - output clamped to [outMin, outMax]
class PIDController {
public:
  PIDController(float kp, float ki, float kd);
  void setTunings(float newKp, float newKi, float newKd);
  void setOutputLimits(float min, float max);
  void setDerivativeTau(float tauSeconds);
  void reset();

  // setpoint/measurement in the same units; dt in seconds.
  float compute(float setpoint, float measurement, float dt);

  float getKp() const { return kp; }
  float getKi() const { return ki; }
  float getKd() const { return kd; }
  float getIntegral() const { return integral; }

private:
  float kp, ki, kd;
  float integral;
  float lastMeasurement;
  float dFiltered;
  float outMin, outMax;
  float dTau;
  bool  first;
};

#endif
