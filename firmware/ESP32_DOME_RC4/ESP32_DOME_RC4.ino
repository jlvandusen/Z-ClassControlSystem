
// ============================================================
//  ESP32_DOME_RC4 — dome node: 5 NeoPixel groups + ESP-NOW to the drive,
//  and (RC4.4) the USB<->ESP-NOW console bridge used by 'bb8 monitor ball'.
//
//  Builds on the STOCK esp32:esp32 core (3.x). NOT the Bluepad32 core: it
//  boots BTstack for sketches that never use BT, which starved this radio
//  into 89-94 % ESP-NOW loss and made WiFi.setSleep(false) an abort().
//  ESP-NOW callback signatures are version-guarded for core 2.x / 3.x.
//
//  RC4.4  stock core + WiFi.setSleep(false) (98/98 delivery); console
//         tunnel (TunnelCmd/TunnelOut, retry on NACK, keepalive).
//  RC4.5  lights per the reference look: PSI white speech-pulse while a
//         track plays (isplaying via drive), blue scrolling logic bars
//         (LOGIC_PIXELS), solid blue HP, eye unchanged. RED/BLUE/IDLE
//         anims painted on change (the old per-loop clear fought the PSI).
// ============================================================
#include <Adafruit_NeoPixel.h>
#include <esp_now.h>
#include <WiFi.h>
#include "esp_wifi.h"
#include "esp_sleep.h"

#include <Preferences.h>
#include "BuildStamp.h"
Preferences prefs;

// ---------------- CONFIG ----------------

#define psiPIN     25
#define sLogicPIN  27
#define lLogicPIN  33
#define hpPIN      15
#define eyePIN     32
#define battPin    A13 // GPIO35

#define NUM_PIXELS 1
#define LOGIC_PIXELS 4   // RC4.5: pixels per logic bar (scrolling blue) - set to the real count
#define BRIGHTNESS 64

#define WIFI_CHANNEL 11
#define HEARTBEAT_LED 2  // Built-in LED on many ESP32 boards

// ESPNOW peer MAC
uint8_t masterMAC[] = {0xC4, 0x5B, 0xBE, 0x90, 0x6A, 0x68};

// ------------------- CONFIGURABLE DEFAULTS -------------------
static const char* DEFAULT_REVISION = "Joe Drive Rev 1.0 RC4 DOME";
static const char* DEFAULT_REVISION_DATE = "2026-08-20";

const unsigned long SHOW_AFTER_MS = 5000;

bool shownAfterWait = false;
unsigned long bootMs = 0;

#pragma pack(push, 1)
typedef struct struct_message {

  int psi;          // Flicker flag
  int anim;         // Animation code
  float bat;        // Battery voltage

  uint16_t checksum = 0;
} struct_message;
#pragma pack(pop)

struct_message incoming;
struct_message outgoing;

Adafruit_NeoPixel PSI(NUM_PIXELS, psiPIN, NEO_GRB + NEO_KHZ800);
Adafruit_NeoPixel sLOGIC(LOGIC_PIXELS, sLogicPIN, NEO_GRB + NEO_KHZ800);
Adafruit_NeoPixel lLOGIC(LOGIC_PIXELS, lLogicPIN, NEO_GRB + NEO_KHZ800);
Adafruit_NeoPixel HP(NUM_PIXELS, hpPIN, NEO_RGB + NEO_KHZ800);
Adafruit_NeoPixel EYE(NUM_PIXELS, eyePIN, NEO_GRB + NEO_KHZ800);

bool debugLocal = false;

// Timers
unsigned long lastEyeUpdate = 0;
unsigned long lastLogicUpdate = 0;
unsigned long lastRainbowUpdate = 0;
unsigned long flickerEndTime = 0;
unsigned long lastDataTime = 0;
unsigned long lastBatteryUpdate = 0;

bool shouldFlicker = false;

// Sleep logic
RTC_DATA_ATTR bool wake = true;
const unsigned long SLEEP_TIMEOUT = 15UL * 60UL * 1000UL; // 15 min
const unsigned long BATTERY_INTERVAL = 5UL * 60UL * 1000UL; // 5 min

// Animation state machine
enum AnimState { IDLE, RAINBOW, RED, BLUE };
AnimState currentAnim = IDLE;

// Serial command buffer
String cmdBuffer = "";

// === Function Prototypes ===
// RC4.3: the dome now builds on the STOCK esp32 core (no Bluepad32) — the
// Bluepad32 core boots BTstack even for sketches that never use BT, which
// (a) contended the radio into 89-94% ESP-NOW loss and (b) made
// WiFi.setSleep(false) an abort(). Core 3.x changed the recv callback
// signature; both are supported here.
#if ESP_ARDUINO_VERSION_MAJOR >= 3
void OnDataRecv(const esp_now_recv_info_t *info, const uint8_t *data, int len);
#else
void OnDataRecv(const uint8_t *mac, const uint8_t *data, int len);
#endif
// void OnDataSent(const uint8_t *mac_addr, esp_now_send_status_t status);
void checkSerialCommand();
void updateAnimations();
void handleAnimation();
void rainbowCycle();
void sendBattery();
float readBatteryVoltage();
uint16_t calculateChecksum(const struct_message &d);
void bootFeedback();

void setup() {
  Serial.begin(115200);
  unsigned long start = millis();
  while (!Serial && millis() - start < 2000) {
      ; // Wait max 2 seconds for Serial
  }
  Serial.println(F("\n=== Dome ESP32 Boot ==="));
  Serial.println(F("Instructions:"));
  Serial.println(F("1. Ensure Drive Master MAC is correct in masterMAC[]"));
  Serial.println(F("2. Type 'debug' to toggle local debug"));
  Serial.println(F("3. Device will sleep after 15 min inactivity"));
  Serial.println(F("--------------------------------------------"));

  bootMs = millis();
  // Display right after reboot (every boot)
  showBuildInfoSerial("BOOT");

  prefs.begin("dome", false);

  // Load MAC from prefs if available
  String savedMAC = prefs.getString("masterMAC", "");
  if (savedMAC.length() == 17) { // Valid MAC format
    Serial.printf("[MAC] Loaded saved Drive Master MAC: %s\n", savedMAC.c_str());
    parseMAC(savedMAC, masterMAC);
  } else {
    Serial.println("[MAC] Using default Drive Master MAC");
  }

  pinMode(battPin, INPUT);  // RC4: GPIO35 is input-only — it has no internal pull-up

  // Boot LED feedback
  bootFeedback();

  // Wake-up cause
  if (esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_EXT0) {
    wake = !wake;
    Serial.printf("[WAKE] Button pressed. New state: %s\n", wake ? "AWAKE" : "SLEEP");
  }
  if (!wake) {
    Serial.println("[SLEEP] Going back to sleep...");
    esp_sleep_enable_ext0_wakeup(GPIO_NUM_35, 0);
    esp_deep_sleep_start();
  }

  WiFi.mode(WIFI_STA);
  // RC4.3: WiFi modem sleep is ON by default in STA mode and the receiver
  // naps between AP beacons it isn't even associated to — measured 89-94%
  // ESP-NOW loss (no MAC ACK) on the bench until this line. The dome's
  // radio has no BT to share with, so keep it awake permanently.
  WiFi.setSleep(false);
  esp_wifi_set_channel(WIFI_CHANNEL, WIFI_SECOND_CHAN_NONE);

  uint8_t mac[6];
  WiFi.macAddress(mac);   // core-3.x-safe (esp_read_mac needs esp_mac.h there)
  Serial.printf("[MAC] Dome WiFi STA MAC: %02X:%02X:%02X:%02X:%02X:%02X\n", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

  if (esp_now_init() != ESP_OK) {
    Serial.println(F("[ESPNOW] Init Failed"));
    return;
  }
  esp_now_register_recv_cb(OnDataRecv);
  esp_now_register_send_cb(OnDataSent);   // RC4.4: tunnel retry needs the ACK

  esp_now_peer_info_t peerInfo = {};
  memcpy(peerInfo.peer_addr, masterMAC, 6);
  peerInfo.channel = WIFI_CHANNEL;
  peerInfo.encrypt = false;
  if (esp_now_add_peer(&peerInfo) != ESP_OK) {
    Serial.println(F("[ESPNOW] Failed to add Drive peer"));
  } else {
    Serial.println(F("[ESPNOW] Connected to Drive ESP32"));
  }

  PSI.begin(); sLOGIC.begin(); lLOGIC.begin(); HP.begin(); EYE.begin();
  PSI.setBrightness(255);   // RC4.5: PSI at full - matches the boot-feedback green the user liked
  sLOGIC.setBrightness(BRIGHTNESS);
  lLOGIC.setBrightness(BRIGHTNESS);
  HP.setBrightness(BRIGHTNESS);
  EYE.setBrightness(BRIGHTNESS);

  PSI.clear(); sLOGIC.clear(); lLOGIC.clear(); HP.clear(); EYE.clear();
  PSI.show(); sLOGIC.show(); lLOGIC.show(); HP.show(); EYE.show();

  Serial.println(F("[READY] Dome ready"));

  lastDataTime = millis();
  lastBatteryUpdate = millis();
}

void loop() {
  checkSerialCommand();
  serviceTunnelRetry();
  updateAnimations();
  handleAnimation();

  // Display after waiting (once)
  if (!shownAfterWait && (millis() - bootMs) >= SHOW_AFTER_MS) {
    shownAfterWait = true;
    showBuildInfoSerial("AFTER WAIT");
  }

  // RC4: battery telemetry to the drive (was declared but never sent)
  if (millis() - lastBatteryUpdate >= BATTERY_INTERVAL) {
    lastBatteryUpdate = millis();
    sendBattery();
  }

  // Inactivity sleep
  if (millis() - lastDataTime > SLEEP_TIMEOUT) {
    Serial.println("[TIMEOUT] No data for 15 mins. Sleeping...");
    esp_sleep_enable_ext0_wakeup(GPIO_NUM_35, 0);
    esp_deep_sleep_start();
  }
}


bool parseMAC(const String &macStr, uint8_t *macArray) {
  int values[6];
  if (sscanf(macStr.c_str(), "%x:%x:%x:%x:%x:%x",
             &values[0], &values[1], &values[2],
             &values[3], &values[4], &values[5]) == 6) {
    for (int i = 0; i < 6; i++) macArray[i] = (uint8_t)values[i];
    return true;
  }
  return false;
}


// ============ RC4.4: bb8 <-> drive console tunnel over ESP-NOW ============
// This dome (on USB at the PC) is a transparent bridge: any console line
// that is not a dome-local command (help/version/debug/setmac) is sent to
// the drive as a TunnelCmd; the drive mirrors its whole console back as
// TunnelOut packets which are written raw to USB. While bb8 is attached
// (USB activity in the last 10 min) a keepalive TunnelCmd goes out every
// 15 s so the drive keeps its mirror armed. Structs hand-mirrored from
// ESP32_DRIVE_RC4.ino — sizes are the discriminator.
struct __attribute__((packed)) TunnelCmd { uint8_t type, seq, len; char data[180]; uint16_t checksum; };
struct __attribute__((packed)) TunnelOut { uint8_t type, seq, len; char data[236]; uint16_t checksum; };
static const uint8_t TUNNEL_CMD_TYPE = 0xC1, TUNNEL_OUT_TYPE = 0xC2;
static uint16_t tunnelSumBytes(const void *p, size_t n) {
  const uint8_t *b = (const uint8_t *)p; uint16_t s = 0;
  for (size_t i = 0; i + 2 < n; i++) s += b[i];
  return s;
}
#define tunnelSum(pkt) tunnelSumBytes(&(pkt), sizeof(pkt))
TunnelCmd gTunTx;                     // last command sent (kept for retry)
uint8_t gTunSeq = 0, gTunAttempts = 0;
volatile bool gTunAwaitAck = false, gTunResend = false;
unsigned long lastUsbMs = 0, lastPingMs = 0, gTunSentMs = 0;

void sendTunnelCmd(const String &line) {
  gTunTx.type = TUNNEL_CMD_TYPE;
  gTunTx.seq = ++gTunSeq; if (gTunSeq == 0) gTunTx.seq = gTunSeq = 1;
  gTunTx.len = (uint8_t)min((unsigned int)line.length(), (unsigned int)sizeof(gTunTx.data));
  memcpy(gTunTx.data, line.c_str(), gTunTx.len);
  gTunTx.checksum = tunnelSum(gTunTx);
  gTunAttempts = 1; gTunAwaitAck = true; gTunSentMs = millis();
  esp_now_send(masterMAC, (uint8_t *)&gTunTx, sizeof(gTunTx));
}
void serviceTunnelRetry() {          // called from loop(); resend on NACK, max 6 tries 40 ms apart
  if (gTunResend && gTunAttempts < 6 && millis() - gTunSentMs >= 40) {
    gTunResend = false; gTunAttempts++; gTunAwaitAck = true; gTunSentMs = millis();
    esp_now_send(masterMAC, (uint8_t *)&gTunTx, sizeof(gTunTx));
  }
  // keepalive while bb8 is attached so the drive keeps mirroring
  if (millis() - lastUsbMs < 600000UL && millis() - lastPingMs >= 15000UL) {
    lastPingMs = millis();
    if (!gTunAwaitAck) sendTunnelCmd("");
  }
}
// ==========================================================================

// === Functions ===
void checkSerialCommand() {
  while (Serial.available()) {
    char c = Serial.read();
    lastUsbMs = millis();
    if (c == '\n') {
      cmdBuffer.trim();
      String rawLine = cmdBuffer;          // RC4.4: preserve case for the tunnel
      bool local = false;
      cmdBuffer.toLowerCase();
      if (cmdBuffer == "version") {
        local = true;
        showBuildInfoSerial("VERSION");
      }
      if (cmdBuffer == "debug") {
        local = true;
        debugLocal = !debugLocal;
        Serial.println(debugLocal ? F("Dome local debug ENABLED") : F("Dome local debug DISABLED"));
      }
    if (cmdBuffer.startsWith("setmac")) {
      local = true;
      String macStr = cmdBuffer.substring(6);
      macStr.trim();
      if (parseMAC(macStr, masterMAC)) {
        prefs.putString("masterMAC", macStr);
        Serial.printf("[MAC] New MAC saved: %s\n", macStr.c_str());
      } else {
        Serial.println("[ERROR] Invalid MAC format. Use XX:XX:XX:XX:XX:XX");
      }
    }
    if (cmdBuffer == "help") {
      local = true;
      Serial.println("Dome-local: help, version, debug, setmac XX:XX:XX:XX:XX:XX");
      Serial.println("Anything else is sent to the DRIVE over ESP-NOW; its console streams back here.");
    }
      // RC4.4: not a dome command -> tunnel it to the drive
      if (!local && rawLine.length()) sendTunnelCmd(rawLine);
      cmdBuffer = "";
    } else {
      cmdBuffer += c;
    }
    


  }
}

void updateAnimations() {
  unsigned long currentMillis = millis();

  // ---------------- PSI (RC4.5, per the reference video) ----------------
  // While a sound plays: WHITE, easing slowly up and down (0.6-1.2 s ramps,
  // short holds) with occasional fast flicker bursts in between. Not a
  // loop-rate shimmer, not a cylon sweep. PSI is OWNED here; the RED/BLUE/
  // RAINBOW pad anims paint it only when idle (and only on change).
  {
    static uint8_t  phase = 0;            // 0 up, 1 hold, 2 down, 3 dark, 4 flicker
    static float    level = 0;
    static uint16_t rampMs = 900;
    static unsigned long phaseAt = 0, tickAt = 0, flickAt = 0;
    static bool     flickOn = false, wasTalking = false;

    bool talking = (incoming.psi != 0) || (shouldFlicker && currentMillis <= flickerEndTime);
    if (talking) {
      wasTalking = true;
      if (currentMillis - tickAt >= 20) {           // 50 Hz easing tick
        tickAt = currentMillis;
        float step = 255.0f * 20.0f / rampMs;
        switch (phase) {
          case 0:  // ramp up
            level += step;
            if (level >= 255) { level = 255; phase = 1; phaseAt = currentMillis + random(80, 250); }
            break;
          case 1:  // bright hold -> sometimes a flicker burst
            if ((long)(currentMillis - phaseAt) >= 0) {
              if (random(0, 100) < 60) { phase = 4; phaseAt = currentMillis + random(250, 600); flickAt = 0; }
              else { phase = 2; rampMs = random(220, 500); }
            }
            break;
          case 2:  // ramp down
            level -= step;
            if (level <= 0) { level = 0; phase = 3; phaseAt = currentMillis + random(60, 200); }
            break;
          case 3:  // dark hold
            if ((long)(currentMillis - phaseAt) >= 0) { phase = 0; rampMs = random(220, 500); }
            break;
          case 4:  // fast flicker burst, then ease down
            if ((long)(currentMillis - flickAt) >= 0) {
              flickOn = !flickOn;
              level = flickOn ? 255 : 0;
              flickAt = currentMillis + random(40, 90);
            }
            if ((long)(currentMillis - phaseAt) >= 0) { phase = 2; level = 255; rampMs = random(220, 500); }
            break;
        }
        uint8_t v = (uint8_t)level;
        PSI.setPixelColor(0, PSI.Color(v, v, v));   // WHITE
        PSI.show();
      }
    } else {
      if (wasTalking) {                              // clean exit once
        wasTalking = false;
        shouldFlicker = false;
        phase = 0; level = 0;
        PSI.clear(); PSI.show();
      }
      // idle: pad anims may own the PSI (painted on change only)
      static AnimState shown = (AnimState)255;
      if (currentAnim != shown) {
        shown = currentAnim;
        switch (currentAnim) {
          case RED:  PSI.setPixelColor(0, PSI.Color(255, 0, 0)); PSI.show(); break;
          case BLUE: PSI.setPixelColor(0, PSI.Color(0, 0, 255)); PSI.show(); break;
          case RAINBOW: break;                       // handleAnimation cycles it
          default:   PSI.clear(); PSI.show(); break;
        }
      }
      if (currentAnim == RAINBOW) handleAnimation();
    }
  }

  // ---------------- Eye (unchanged: red while running) ----------------
  if (currentMillis - lastEyeUpdate > random(3000, 10000)) {
    EYE.setPixelColor(0, EYE.Color(random(50, 255), 0, 0));
    EYE.show();
    lastEyeUpdate = currentMillis;
  }

  // ---------------- Logic bars (RC4.5): slow BLUE scroll ----------------
  // A blue comet drifting along the bar (head bright, tail fading) - a calm
  // scroll, NOT a KITT/cylon bounce. Both bars run the same pattern offset.
  {
    static unsigned long scrollAt = 0;
    static uint8_t head = 0;
    if (currentMillis - scrollAt >= 160) {
      scrollAt = currentMillis;
      head = (head + 1) % LOGIC_PIXELS;
      for (uint8_t i = 0; i < LOGIC_PIXELS; i++) {
        uint8_t d = (uint8_t)((head - i + LOGIC_PIXELS) % LOGIC_PIXELS);  // 0 = head
        uint8_t b = (d == 0) ? 255 : (d == 1 ? 90 : (d == 2 ? 25 : 0));
        sLOGIC.setPixelColor(i, sLOGIC.Color(0, 0, b));
        lLOGIC.setPixelColor((i + LOGIC_PIXELS / 2) % LOGIC_PIXELS, lLOGIC.Color(0, 0, b));
      }
      sLOGIC.show(); lLOGIC.show();
    }
  }

  // ---------------- HP: solid blue (say the word for red) ----------------
  {
    static bool hpPainted = false;
    if (!hpPainted) { hpPainted = true; HP.setPixelColor(0, HP.Color(0, 0, 255)); HP.show(); }
  }
}

void handleAnimation() {
  // RC4.5: only the rainbow needs a per-loop service now; RED/BLUE/IDLE are
  // painted on change inside updateAnimations() (the old version cleared the
  // PSI every loop pass and fought the talking flicker).
  if (currentAnim == RAINBOW) rainbowCycle();
}


void rainbowCycle() {
  static uint16_t hue = 0;
  if (millis() - lastRainbowUpdate > 50) {
    PSI.setPixelColor(0, PSI.gamma32(PSI.ColorHSV(hue)));
    sLOGIC.setPixelColor(0, sLOGIC.gamma32(sLOGIC.ColorHSV(hue + 5000)));
    lLOGIC.setPixelColor(0, lLOGIC.gamma32(lLOGIC.ColorHSV(hue + 10000)));
    HP.setPixelColor(0, HP.gamma32(HP.ColorHSV(hue + 15000)));
    EYE.setPixelColor(0, EYE.gamma32(EYE.ColorHSV(hue + 20000)));
    PSI.show(); sLOGIC.show(); lLOGIC.show(); HP.show(); EYE.show();
    hue += 256;
    lastRainbowUpdate = millis();
  }
}


#if ESP_ARDUINO_VERSION_MAJOR >= 3
void OnDataRecv(const esp_now_recv_info_t *info, const uint8_t *data, int len) {
  const uint8_t *mac = info->src_addr;
#else
void OnDataRecv(const uint8_t *mac, const uint8_t *data, int len) {
#endif
  // RC4.4: drive console stream -> straight out the USB port
  if (len == sizeof(TunnelOut)) {
    TunnelOut t;
    memcpy(&t, data, sizeof(t));
    if (t.type != TUNNEL_OUT_TYPE || t.len > sizeof(t.data)) return;
    if (tunnelSum(t) != t.checksum) return;
    Serial.write((const uint8_t *)t.data, t.len);
    lastDataTime = millis();
    return;
  }
  if (len != sizeof(incoming)) return;
  memcpy(&incoming, data, sizeof(incoming));
  lastDataTime = millis();

  if (debugLocal) {
    Serial.println(F("[ESP-NOW] Received data from Drive:"));
    Serial.printf("  MAC: %02X:%02X:%02X:%02X:%02X:%02X\n",
                  mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    Serial.printf("  PSI Flicker: %s\n", incoming.psi ? "ON" : "OFF");
    Serial.printf("  Animation: %d\n", incoming.anim);
    Serial.printf("  Battery: %.2f V\n", incoming.bat);
    Serial.printf("  Checksum: 0x%04X\n", incoming.checksum);
    Serial.println(F("--------------------------------------------"));
  }

  // Flicker logic
  if (incoming.psi != 0 && !shouldFlicker) {
    shouldFlicker = true;
    flickerEndTime = millis() + random(1000, 3000);
  }

  // Update animation state
  switch(incoming.anim) {
    case 1: currentAnim = RAINBOW; break;
    case 2: currentAnim = RED; break;
    case 3: currentAnim = BLUE; break;
    default: currentAnim = IDLE; break;
  }
}

#if ESP_ARDUINO_VERSION_MAJOR >= 3
void OnDataSent(const wifi_tx_info_t *tx_info, esp_now_send_status_t status) {
#else
void OnDataSent(const uint8_t *mac_addr, esp_now_send_status_t status) {
#endif
  if (debugLocal) {
    Serial.printf("[SEND STATUS] %s\n", status == ESP_NOW_SEND_SUCCESS ? "Success" : "Fail");
  }
  // RC4.4: command packets retry until the MAC ACK lands (max 3 tries)
  if (gTunAwaitAck) {
    gTunAwaitAck = false;
    if (status != ESP_NOW_SEND_SUCCESS) gTunResend = true;
  }
}

void sendBattery() {
  float voltage = readBatteryVoltage();
  outgoing.bat = voltage;
  outgoing.checksum = calculateChecksum(outgoing);
  esp_now_send(masterMAC, (uint8_t*)&outgoing, sizeof(outgoing));
  if (debugLocal) {
    Serial.printf("[SEND] Battery=%.2fV\n", voltage);
  }
}

float readBatteryVoltage() {
  int raw = analogRead(battPin);
  return (raw / 4095.0) * 3.3 * 2.0;
}

uint16_t calculateChecksum(const struct_message &d) {
  const uint8_t* p = (const uint8_t*)&d;
  uint16_t s = 0;
  for (size_t i = 0; i < sizeof(d) - sizeof(d.checksum); i++) {
    s += p[i];
  }
  return s;
}

void bootFeedback() {
  unsigned long start = millis();
  bool ledOn = false;
  while (millis() - start < 1200) {
    if ((millis() / 200) % 2 == 0 && !ledOn) {
      PSI.setPixelColor(0, PSI.Color(0, 255, 0));
      PSI.show();
      ledOn = true;
    } else if ((millis() / 200) % 2 == 1 && ledOn) {
      PSI.clear(); PSI.show();
      ledOn = false;
    }
  }
}

void showBuildInfoSerial(const char* prefix)
{
  Serial.print(prefix);
  Serial.print(" | ");
  Serial.print(DEFAULT_REVISION);
  Serial.print(F(" | build "));
  Serial.print(BB8_BUILD_NUM);
  Serial.print(F(" | "));
  Serial.print(F(BB8_BUILD_DATE));
  Serial.print(F(" | git "));
  Serial.println(F(BB8_BUILD_GIT));
}

