#pragma once
// RC4.7: black-box flight recorder. A 25 Hz ring of the core control state
// (~30 s, ~10 KB RAM). On a safety event (pad lost, IMU stale, experiment
// abort) the ring FREEZES so the run-up to the incident survives for
// 'blackbox dump'. 'blackbox arm' resumes recording.
// "It fell over and I don't know why" -> data.

struct __attribute__((packed)) BBSample {
  uint32_t ms;
  int16_t pitch100, roll100;    // degrees x100
  int16_t pot, tgt;             // pot counts
  int16_t drv;                  // slewed drive PWM
  uint8_t flags;                // bit0 driveEnabled, bit1 autoBalance
};

extern bool driveEnabled;

namespace BlackBox {

static const uint16_t CAP = 750;       // 25 Hz x 30 s (DRAM is tight on the drive)
static BBSample buf[CAP];
static uint16_t head = 0;
static uint32_t count = 0;
static bool frozen = false;
static char reason[40] = "";
static unsigned long lastMs = 0;

inline void record(float pitch, float roll, int pot, int tgt, float drv, uint8_t flags) {
  if (frozen) return;
  unsigned long now = millis();
  if (now - lastMs < 40) return;       // 25 Hz
  lastMs = now;
  BBSample& s = buf[head];
  s.ms = now;
  s.pitch100 = (int16_t)(pitch * 100.0f);
  s.roll100  = (int16_t)(roll * 100.0f);
  s.pot = (int16_t)pot;
  s.tgt = (int16_t)tgt;
  s.drv = (int16_t)drv;
  s.flags = flags;
  head = (uint16_t)((head + 1) % CAP);
  count++;
}

inline void freeze(const char* why) {
  if (frozen) return;
  frozen = true;
  strlcpy(reason, why, sizeof(reason));
  Serial.printf("[BLACKBOX] FROZEN: %s — 'blackbox dump' to read, 'blackbox arm' to resume\n", why);
}

inline void arm() {
  frozen = false;
  reason[0] = 0;
  Serial.println(F("[BLACKBOX] recording (25 Hz, ~40 s ring)"));
}

inline void status() {
  Serial.printf("[BLACKBOX] %s — %lu samples captured%s%s\n",
                frozen ? "FROZEN" : "recording",
                (unsigned long)(count < CAP ? count : CAP),
                frozen ? ", reason: " : "", frozen ? reason : "");
}

inline void dump() {
  if (::driveEnabled) { Serial.println(F("[BLACKBOX] disable the drive first — the dump stalls the loop")); return; }
  uint16_t n = (uint16_t)(count < CAP ? count : CAP);
  uint16_t start = (count < CAP) ? 0 : head;
  Serial.printf("[BLACKBOX DUMP] %u samples%s%s\n", n, frozen ? " frozen: " : "", frozen ? reason : "");
  Serial.println(F("ms,pitch,roll,pot,tgt,drv,en,bal"));
  for (uint16_t i = 0; i < n; i++) {
    const BBSample& s = buf[(start + i) % CAP];
    Serial.printf("%lu,%.2f,%.2f,%d,%d,%d,%d,%d\n",
                  (unsigned long)s.ms, s.pitch100 / 100.0f, s.roll100 / 100.0f,
                  s.pot, s.tgt, s.drv, s.flags & 1, (s.flags >> 1) & 1);
  }
  Serial.println(F("[BLACKBOX END]"));
}

}  // namespace BlackBox

inline void blackboxFreeze(const char* why) { BlackBox::freeze(why); }
