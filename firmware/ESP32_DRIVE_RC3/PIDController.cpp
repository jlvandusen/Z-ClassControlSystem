
#include "PIDController.h"

PIDController::PIDController(float kp, float ki, float kd)
  : kp(kp), ki(ki), kd(kd), integral(0), lastError(0), lastUpdate(0) {}

void PIDController::setTunings(float newKp, float newKi, float newKd) {
  kp = newKp;
  ki = newKi;
  kd = newKd;
}

void PIDController::reset() {
  integral = 0;
  lastError = 0;
  lastUpdate = millis();
}

float PIDController::compute(float error) {
  unsigned long now = millis();
  float dt = (now - lastUpdate) / 1000.0f;
  if (dt <= 0) dt = 0.01f;
  lastUpdate = now;

  integral += error * dt;
  float derivative = (error - lastError) / dt;
  lastError = error;

  return (kp * error) + (ki * integral) + (kd * derivative);
}
