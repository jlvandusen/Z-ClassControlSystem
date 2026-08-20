
#pragma once

#pragma pack(push, 1)
struct struct_messagempu {
  float rawX;
  float rawY;
  float rawZ;
  float pitch;
  float roll;
  byte functionnumber;  // 99 = debug toggle command
  uint16_t checksum;
};
#pragma pack(pop)

#pragma pack(push, 1)
struct struct_messagedome {

  int psi;          // Flicker flag
  int anim;         // Animation code
  float bat;        // Battery voltage

  uint16_t checksum;
};
#pragma pack(pop)

#pragma pack(push, 1)
struct send32u4 {
  uint8_t driveEnabled;
  uint8_t driveReverse;
  uint8_t autoBalance;
  uint8_t domeFunction;
  int8_t  DomeSpin;
  int8_t  leftStickX;
  int8_t  leftStickY;
  int8_t  soundcmd;
  float   pitch;
  float   roll;
  uint8_t functionnumber;
  uint16_t checksum;
};
#pragma pack(pop)

#pragma pack(push, 1)
struct Rec32u4 {
  bool isplaying;
  bool domedirection;
  float dometilt;
  uint16_t checksum;
};
#pragma pack(pop)


struct DriveConfig {
  char revision[48];
  char revisionDate[16];
  float pitchOffset, rollOffset;
  int32_t potCenter;
  float mpuDeadzone;
  uint32_t cfgVersion;

  // NEW: PID tunings
  float driveKp, driveKi, driveKd;
  float s2sKp, s2sKi, s2sKd;
};


// declarations for global variables
extern struct_messagempu mpudata;
extern struct_messagedome domeData;
extern send32u4 sendTo32u4;
extern Rec32u4 recFrom32u4;
extern DriveConfig cfg;
