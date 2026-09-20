#pragma once
#include <Arduino.h>
// Kept in a header so Arduino's auto-generated function prototypes can see these types.

// ---------- settings (saved to flash) ----------
struct Settings {
  uint16_t ver;
  float sens;       // mouse speed
  float dead;       // mouse deadzone, deg/s
  uint8_t axX, axY; // gyro axis for mouse X / Y (0=X 1=Y 2=Z)
  bool invX, invY;
  bool lookY;       // allow looking up/down
  uint8_t swAx;     // gyro axis for swing
  bool swInv;
  float swTh;       // swing strength for click, deg/s
  bool clickRight;  // swing = right click instead of left
  float tilt;       // tilt angle for WASD, deg
  bool swapTilt, invFB, invLR;
  bool jumpOn;
  float jumpTh;     // jump when total accel drops below this, g
  uint8_t touchMode;// 0 = TTP223 (HIGH when touched), 1 = copper tape to GND
  float smooth;     // 0 = raw, 0.9 = very smooth (both hands)
  bool diag;        // allow diagonal walking (W+A etc)
  float sideTilt;   // tilt angle for A/D, deg
};

// ---------- one gyro ----------
struct Imu {
  Imu(uint8_t a = 0) : addr(a) {}
  uint8_t addr;
  bool ok = false;
  float ax = 0, ay = 0, az = 1;   // g
  float gx = 0, gy = 0, gz = 0;   // deg/s, bias removed
  float bx = 0, by = 0, bz = 0;   // gyro bias
  float pitch = 0, roll = 0, yaw = 0;
  float p0 = 0, r0 = 0;           // zero position
  float amag = 1;
  uint32_t lastTry = 0;
};

// ---------- button / touch debounce ----------
struct Debounce {
  bool state = false, last = false;
  uint32_t since = 0;
  // returns true on a new press
  bool update(bool raw, uint32_t now) {
    if (raw != last) { last = raw; since = now; }
    if (now - since > 25 && state != raw) {
      state = raw;
      return raw;
    }
    return false;
  }
};
