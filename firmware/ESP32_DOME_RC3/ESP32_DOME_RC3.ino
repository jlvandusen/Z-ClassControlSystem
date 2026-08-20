
#include <Adafruit_NeoPixel.h>
#include <esp_now.h>
#include <WiFi.h>
#include "esp_wifi.h"
#include "esp_sleep.h"

#include <Preferences.h>
Preferences prefs;

// ---------------- CONFIG ----------------

#define psiPIN     25
#define sLogicPIN  27
#define lLogicPIN  33
#define hpPIN      15
#define eyePIN     32
#define battPin    A13 // GPIO35

#define NUM_PIXELS 1
#define BRIGHTNESS 64

#define WIFI_CHANNEL 11
#define HEARTBEAT_LED 2  // Built-in LED on many ESP32 boards

// ESPNOW peer MAC
uint8_t masterMAC[] = {0xC4, 0x5B, 0xBE, 0x90, 0x6A, 0x68};

// ------------------- CONFIGURABLE DEFAULTS -------------------
static const char* DEFAULT_REVISION = "Joe Drive Rev 1.0 RC3 DOME";
static const char* DEFAULT_REVISION_DATE = "2026-03-02";

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
Adafruit_NeoPixel sLOGIC(NUM_PIXELS, sLogicPIN, NEO_GRB + NEO_KHZ800);
Adafruit_NeoPixel lLOGIC(NUM_PIXELS, lLogicPIN, NEO_GRB + NEO_KHZ800);
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
void OnDataRecv(const uint8_t *mac, const uint8_t *data, int len);
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

  pinMode(battPin, INPUT_PULLUP);

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
  esp_wifi_set_channel(WIFI_CHANNEL, WIFI_SECOND_CHAN_NONE);

  uint8_t mac[6];
  esp_read_mac(mac, ESP_MAC_WIFI_STA);
  Serial.printf("[MAC] Dome WiFi STA MAC: %02X:%02X:%02X:%02X:%02X:%02X\n", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

  if (esp_now_init() != ESP_OK) {
    Serial.println(F("[ESPNOW] Init Failed"));
    return;
  }
  esp_now_register_recv_cb(OnDataRecv);
  // esp_now_register_send_cb(OnDataSent);

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
  PSI.setBrightness(BRIGHTNESS);
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
  updateAnimations();
  handleAnimation();

  // Display after waiting (once)
  if (!shownAfterWait && (millis() - bootMs) >= SHOW_AFTER_MS) {
    shownAfterWait = true;
    showBuildInfoSerial("AFTER WAIT");
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

// === Functions ===
void checkSerialCommand() {
  while (Serial.available()) {
    char c = Serial.read();
    if (c == '\n') {
      cmdBuffer.trim();
      cmdBuffer.toLowerCase();
      if (cmdBuffer == "debug") {
        debugLocal = !debugLocal;
        Serial.println(debugLocal ? F("Dome local debug ENABLED") : F("Dome local debug DISABLED"));
      }
    if (cmdBuffer.startsWith("setmac")) {
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
      Serial.println("You can use help, debug or setmac XX.XX.XX.XX.XX");
    }
      cmdBuffer = "";
    } else {
      cmdBuffer += c;
    }
    


  }
}

void updateAnimations() {
  unsigned long currentMillis = millis();

  // Flicker
  // if (shouldFlicker && currentMillis <= flickerEndTime) {
    
if (incoming.psi != 0) {
  PSI.setPixelColor(0, PSI.Color(0, 0, random(100, 255)));
  PSI.show();
} else if (shouldFlicker && currentMillis <= flickerEndTime) {
    PSI.setPixelColor(0, PSI.Color(0, 0, random(100, 255)));
    PSI.show();
  } else if (shouldFlicker) {
    shouldFlicker = false;
    PSI.clear(); PSI.show();
  }

  // Eye update
  if (currentMillis - lastEyeUpdate > random(3000, 10000)) {
    EYE.setPixelColor(0, EYE.Color(random(50, 255), 0, 0));
    EYE.show();
    lastEyeUpdate = currentMillis;
  }

  // Logic update
  if (currentMillis - lastLogicUpdate > random(3000, 10000)) {
    uint32_t colors[] = {
      sLOGIC.Color(255, 255, 0),
      sLOGIC.Color(255, 0, 0),
      sLOGIC.Color(255, 255, 255)
    };
    uint8_t idx = random(0, 3);
    sLOGIC.setPixelColor(0, colors[idx]);
    lLOGIC.setPixelColor(0, colors[idx]);
    sLOGIC.show(); lLOGIC.show();
    lastLogicUpdate = currentMillis;
  }

  HP.setPixelColor(0, HP.Color(128, 128, 128));
  HP.show();
}

void handleAnimation() {
  switch(currentAnim) {
    case RAINBOW: rainbowCycle(); break;
    case RED: PSI.setPixelColor(0, PSI.Color(255, 0, 0)); PSI.show(); break;
    case BLUE: PSI.setPixelColor(0, PSI.Color(0, 0, 255)); PSI.show(); break;
    default: PSI.clear(); PSI.show(); break;
  }
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


void OnDataRecv(const uint8_t *mac, const uint8_t *data, int len) {
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

void OnDataSent(const uint8_t *mac_addr, esp_now_send_status_t status) {
  if (debugLocal) {
    Serial.printf("[SEND STATUS] %s\n", status == ESP_NOW_SEND_SUCCESS ? "Success" : "Fail");
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
  Serial.print(" | ");
  Serial.println(DEFAULT_REVISION_DATE);
}

