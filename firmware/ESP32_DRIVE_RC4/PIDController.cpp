
#include "PIDController.h"

PIDController::PIDController(float kp, float ki, float kd)
  : kp(kp), ki(ki), kd(kd), integral(0), lastMeasurement(0),
    dFiltered(0), outMin(-255.0f), outMax(255.0f), dTau(0.04f), first(true) {}

void PIDController::setTunings(float newKp, float newKi, float newKd) {
  kp = newKp;
  ki = newKi;
  kd = newKd;
}

void PIDController::setOutputLimits(float min, float max) {
  outMin = min;
  outMax = max;
  if (integral > outMax) integral = outMax;
  if (integral < outMin) integral = outMin;
}

void PIDController::setDerivativeTau(float tauSeconds) {
  dTau = tauSeconds;
}

void PIDController::reset() {
  integral = 0;
  dFiltered = 0;
  first = true;
}

float PIDController::compute(float setpoint, float measurement, float dt) {
  if (dt <= 0.0f) dt = 0.001f;
  if (dt > 0.2f)  dt = 0.2f;   // stale-data guard

  float error = setpoint - measurement;

  if (first) {
    first = false;
    lastMeasurement = measurement;
  }

  // Integral with clamping (anti-windup)
  integral += ki * error * dt;
  if (integral > outMax) integral = outMax;
  if (integral < outMin) integral = outMin;

  // Derivative on measurement, low-pass filtered
  float dRaw = -(measurement - lastMeasurement) / dt;
  lastMeasurement = measurement;
  float alpha = dt / (dTau + dt);
  dFiltered += alpha * (dRaw - dFiltered);

  float out = (kp * error) + integral + (kd * dFiltered);
  if (out > outMax) out = outMax;
  if (out < outMin) out = outMin;
  return out;
}
