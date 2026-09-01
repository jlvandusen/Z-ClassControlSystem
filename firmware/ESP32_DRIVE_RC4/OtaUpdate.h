#pragma once
// RC4.7: over-the-air drive updates THROUGH the dome's ESP-NOW tunnel.
// The featheresp32 default partition table already has dual OTA app slots
// (app0/app1 @ 1.25 MB + otadata), so this needs no partition change — it
// arrives like any other flash and works from then on.
//
// Protocol (all control via the normal console, so it rides TunnelCmd and
// the replies ride the TunnelOut mirror bb8 is already reading):
//   bb8 -> "ota begin <bytes>"  -> Update.begin  -> "[OTA] READY <bytes>"
//   bb8 -> OTAD lines to the DOME, which relays 192-byte TunnelOta packets
//          here; in-order chunks are Update.write()n, every chunk answered
//          with "[OTAACK] <seq>" (dupes re-acked, gaps "[OTAERR] want <n>")
//   bb8 -> "ota end"            -> Update.end    -> "[OTA] OK ..." + reboot
// Safety: refused while the drive is ENABLED (flash erases stall the loop);
// a failed/aborted transfer leaves the running app untouched (dual slots).

#include <Update.h>

struct __attribute__((packed)) TunnelOta { uint8_t type; uint32_t seq; uint8_t len; uint8_t data[192]; uint16_t checksum; };
static const uint8_t TUNNEL_OTA_TYPE = 0xC3;

extern bool driveEnabled;

namespace OtaRx {

// 2-slot single-producer (ESP-NOW RX task) / single-consumer (loop) queue.
// A full queue just drops the packet — bb8 times out and resends the chunk.
struct Slot { volatile bool full; uint32_t seq; uint8_t len; uint8_t data[192]; };
static Slot q[2];
static volatile uint8_t qw = 0;
static uint8_t qr = 0;

static bool active = false;
static uint32_t expected = 0;
static size_t total = 0, written = 0;
static bool rebootPending = false;
static unsigned long rebootAt = 0;

inline void onPacket(const TunnelOta& p) {          // RX-task context: copy only
  Slot& s = q[qw & 1];
  if (s.full) return;
  s.seq = p.seq;
  s.len = p.len;
  memcpy((void*)s.data, p.data, p.len);
  s.full = true;
  qw++;
}

inline void begin(size_t size) {
  if (driveEnabled) { Serial.println(F("[OTA] FAIL drive is ENABLED — disable first (flash writes stall the control loop)")); return; }
  if (active) { Update.abort(); active = false; }
  if (size < 1024 || size > 4 * 1024 * 1024) { Serial.println(F("[OTA] FAIL bad size")); return; }
  if (!Update.begin(size)) {
    Serial.printf("[OTA] FAIL begin: %s\n", Update.errorString());
    return;
  }
  active = true; expected = 0; total = size; written = 0;
  q[0].full = q[1].full = false; qr = qw;
  Serial.printf("[OTA] READY %u\n", (unsigned)size);
}

inline void abortOta(const char* why) {
  if (active) { Update.abort(); active = false; }
  rebootPending = false;
  Serial.printf("[OTA] aborted (%s) — running firmware untouched\n", why);
}

inline void end() {
  if (!active) { Serial.println(F("[OTA] FAIL no transfer active")); return; }
  active = false;
  if (written != total) {
    Update.abort();
    Serial.printf("[OTA] FAIL incomplete: %u of %u bytes\n", (unsigned)written, (unsigned)total);
    return;
  }
  if (Update.end()) {
    Serial.println(F("[OTA] OK — new firmware staged, rebooting in 1.2 s"));
    rebootPending = true;
    rebootAt = millis() + 1200;     // let the TunnelOut mirror drain first
  } else {
    Serial.printf("[OTA] FAIL end: %s\n", Update.errorString());
  }
}

inline void status() {
  if (active) Serial.printf("[OTA] active: %u/%u bytes (next chunk %u)\n", (unsigned)written, (unsigned)total, (unsigned)expected);
  else Serial.println(F("[OTA] idle"));
}

inline void service() {
  if (rebootPending && (long)(millis() - rebootAt) >= 0) ESP.restart();
  while (q[qr & 1].full) {
    Slot& s = q[qr & 1];
    if (!active) {
      Serial.println(F("[OTAERR] not active"));
    } else if (s.seq == expected) {
      size_t n = Update.write((uint8_t*)s.data, s.len);
      if (n != s.len) {
        Serial.printf("[OTA] FAIL write at %u: %s\n", (unsigned)written, Update.errorString());
        Update.abort(); active = false;
      } else {
        written += n;
        expected++;
        Serial.printf("[OTAACK] %u\n", (unsigned)s.seq);
      }
    } else if (s.seq < expected) {
      Serial.printf("[OTAACK] %u\n", (unsigned)s.seq);      // dupe from a retry
    } else {
      Serial.printf("[OTAERR] want %u\n", (unsigned)expected);
    }
    s.full = false;
    qr++;
  }
}

}  // namespace OtaRx
