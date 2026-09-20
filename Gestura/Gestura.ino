// Gestura - motion control gloves for gaming (LoR Core V3)
//
// Hand 1 (gyro 0x68, "mouse hand"): turn hand = mouse, swing down = click
// Hand 2 (gyro 0x69, "move hand"):  tilt = W/A/S/D, quick drop = jump
// Touch 1 = hold for left click, Touch 2 = hold for right click,
// hold Touch 1 + 2 together = talk to the AI (holds F13 while held), Touch 3 = next slot
// Board buttons: A = zero both hands, B = hold F13 (voice push-to-talk),
//                C = swap left/right click, D = pause
//
// Shows up as Bluetooth mouse+keyboard "Gestura".
// Calibration page: join WiFi "Gestura" (pass gestura123), open 192.168.4.1
//
// Wiring (LoR Core V3 labels):
//   Both gyros SCL -> port 11 SIG, SDA -> port 12 SIG
//   Gyro 1 VCC -> AUX 10 (3.3V), GND -> port 11 GND, AD0 unconnected
//   Gyro 2 VCC -> AUX 11 (3.3V), GND -> port 12 GND, AD0 -> its own VCC
//   Touch 1/2/3 I/O -> port 01/02/03 SIG, GND -> same port GND, VCC -> 3.3V
//   Never use the V+ pins (6V).

#include <Wire.h>
#include <WiFi.h>
#include <WebServer.h>
#include <Preferences.h>
#include <NimBLEDevice.h>
#include <NimBLEHIDDevice.h>
#include <FastLED.h>
#include <WiFiClientSecure.h>
#include <PubSubClient.h>
#include "page.h"
#include "types.h"

// ---------- pins ----------
const int PIN_SDA = 21, PIN_SCL = 22;          // port 12 / port 11 SIG
const int PIN_TOUCH[3] = {32, 25, 26};         // port 01 / 02 / 03 SIG
const int BTN_A = 35, BTN_B = 39, BTN_C = 38, BTN_D = 37;
const int PIN_LED = 33, NUM_LEDS = 4;

const char* AP_SSID = "Gestura";
const char* AP_PASS = "gestura123";

// ---------- HID key codes ----------
const uint8_t KEY_W = 0x1A, KEY_A = 0x04, KEY_S = 0x16, KEY_D = 0x07;
const uint8_t KEY_SPACE = 0x2C, KEY_F13 = 0x68;

// ---------- settings (saved to flash) ----------
const uint16_t SET_VER = 12;
Settings S;

void setDefaults() {
  // axX=Y turn, axY=Z look up&down (on by default), swing on Z
  S = {SET_VER, 1.5f, 10.0f, 1, 2, true, true, true, 2, false, 250.0f, false,
       18.0f, false, false, false, true, 0.45f, 0,
       0.6f, false, 22.0f, true, true, 8.0f, true, false};
}

// ---------- cloud settings (WiFi + HiveMQ), kept out of the code ----------
String netSsid, netPass, mqHost, mqUser, mqPass, mqTopic = "gestura";
uint16_t mqPort = 8883;
WiFiClientSecure tls;
PubSubClient mqtt(tls);
uint32_t lastMqttTry = 0, lastPub = 0, hurtUntil = 0, usbSeen = 0;

Preferences prefs;

void loadNet() {
  prefs.begin("net", true);
  netSsid = prefs.getString("ssid", "");
  netPass = prefs.getString("pass", "");
  mqHost  = prefs.getString("host", "");
  mqUser  = prefs.getString("user", "");
  mqPass  = prefs.getString("mpass", "");
  mqTopic = prefs.getString("topic", "gestura");
  mqPort  = prefs.getUShort("port", 8883);
  prefs.end();
}

void saveNet() {
  prefs.begin("net", false);
  prefs.putString("ssid", netSsid);
  prefs.putString("pass", netPass);
  prefs.putString("host", mqHost);
  prefs.putString("user", mqUser);
  prefs.putString("mpass", mqPass);
  prefs.putString("topic", mqTopic);
  prefs.putUShort("port", mqPort);
  prefs.end();
}

void loadSettings() {
  setDefaults();
  prefs.begin("glove", true);
  Settings t;
  if (prefs.getBytes("s", &t, sizeof(t)) == sizeof(t) && t.ver == SET_VER) S = t;
  prefs.end();
}
void saveSettings() {
  prefs.begin("glove", false);
  prefs.putBytes("s", &S, sizeof(S));
  prefs.end();
}

// ---------- gyros ----------
Imu imu[2] = {Imu(0x68), Imu(0x69)};

bool i2cWrite(uint8_t addr, uint8_t reg, uint8_t val) {
  Wire.beginTransmission(addr);
  Wire.write(reg);
  Wire.write(val);
  return Wire.endTransmission() == 0;
}

// Works for MPU6050 and MPU6500 (same registers).
bool imuInit(Imu& m) {
  if (!i2cWrite(m.addr, 0x6B, 0x01)) return false; // wake up
  delay(5);
  i2cWrite(m.addr, 0x1A, 0x03); // low pass filter ~44Hz
  i2cWrite(m.addr, 0x1B, 0x08); // gyro +-500 deg/s
  i2cWrite(m.addr, 0x1C, 0x00); // accel +-2g
  return true;
}

bool imuRead(Imu& m, float& ax, float& ay, float& az, float& gx, float& gy, float& gz) {
  Wire.beginTransmission(m.addr);
  Wire.write(0x3B);
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom((int)m.addr, 14) != 14) return false;
  int16_t v[7];
  for (int i = 0; i < 7; i++) {
    uint8_t hi = Wire.read();
    uint8_t lo = Wire.read();
    v[i] = (int16_t)((hi << 8) | lo);
  }
  ax = v[0] / 16384.0f; ay = v[1] / 16384.0f; az = v[2] / 16384.0f;
  gx = v[4] / 65.5f;    gy = v[5] / 65.5f;    gz = v[6] / 65.5f;
  return true;
}

void accAngles(float ax, float ay, float az, float& p, float& r) {
  p = atan2f(-ax, sqrtf(ay * ay + az * az)) * RAD_TO_DEG;
  r = atan2f(ay, az) * RAD_TO_DEG;
}

void imuUpdate(Imu& m, float dt) {
  float ax, ay, az, gx, gy, gz;
  if (!imuRead(m, ax, ay, az, gx, gy, gz)) { m.ok = false; return; }
  m.ax = ax; m.ay = ay; m.az = az;
  m.gx = gx - m.bx; m.gy = gy - m.by; m.gz = gz - m.bz;
  m.amag = sqrtf(ax * ax + ay * ay + az * az);
  float ap, ar;
  accAngles(ax, ay, az, ap, ar);
  const float k = 0.98f; // trust gyro short term, accel long term
  m.pitch = k * (m.pitch + m.gy * dt) + (1 - k) * ap;
  m.roll  = k * (m.roll  + m.gx * dt) + (1 - k) * ar;
  m.yaw  += m.gz * dt;
}

float relPitch(const Imu& m) { return m.pitch - m.p0; }
float relRoll(const Imu& m)  { return m.roll - m.r0; }

float gyroAxis(const Imu& m, uint8_t a) { return a == 0 ? m.gx : (a == 1 ? m.gy : m.gz); }

// angle since the last zero, for absolute aiming
float absAngle(const Imu& m, uint8_t a) {
  return a == 0 ? (m.roll - m.r0) : (a == 1 ? (m.pitch - m.p0) : m.yaw);
}

// Turn rate -> mouse speed. Below the deadzone nothing happens; past it the
// response ramps up, so small turns are precise and big turns are quick.
float curve(float rate, float dead) {
  float a = fabsf(rate) - dead;
  if (a <= 0) return 0;
  return copysignf(powf(a, 1.25f), rate);
}

// ---------- LEDs ----------
CRGB leds[NUM_LEDS];
bool zeroing = false, paused = false;
volatile bool bleConn = false;

extern bool touch[3];
extern bool comboFired;

bool usbActive() { return millis() - usbSeen < 2000; }  // laptop bridge is running

void updateLeds() {
  bool blink = (millis() / 400) % 2;
  bool usb = usbActive();
  // LED 1: white = USB + Bluetooth, cyan = USB only, green = Bluetooth only,
  //        blinking blue = nothing connected
  if (usb && bleConn)      leds[0] = CRGB::White;
  else if (usb)            leds[0] = CRGB::Cyan;
  else if (bleConn)        leds[0] = CRGB::Green;
  else                     leds[0] = blink ? CRGB::Blue : CRGB::Black;
  leds[1] = imu[0].ok ? CRGB::Green : CRGB::Red;
  leds[2] = imu[1].ok ? CRGB::Green : CRGB::Red;
  if (millis() < hurtUntil) {     // took damage in game -> all red
    for (int i = 0; i < NUM_LEDS; i++) leds[i] = CRGB::Red;
    FastLED.show();
    return;
  }
  if (zeroing)      leds[3] = CRGB::Yellow;
  else if (touch[0] && touch[1]) leds[3] = comboFired ? CRGB::Green : CRGB::Cyan; // voice combo
  else if (paused)  leds[3] = CRGB::Red;
  else if (S.clickRight) leds[3] = CRGB::Purple;
  else              leds[3] = CRGB::Black;
  FastLED.show();
}

// ---------- Bluetooth HID (keyboard = report 1, mouse = report 2) ----------
static const uint8_t REPORT_MAP[] = {
  // keyboard
  0x05, 0x01, 0x09, 0x06, 0xA1, 0x01, 0x85, 0x01,
  0x05, 0x07, 0x19, 0xE0, 0x29, 0xE7, 0x15, 0x00, 0x25, 0x01, 0x75, 0x01, 0x95, 0x08, 0x81, 0x02,
  0x95, 0x01, 0x75, 0x08, 0x81, 0x01,
  0x95, 0x06, 0x75, 0x08, 0x15, 0x00, 0x25, 0x73, 0x05, 0x07, 0x19, 0x00, 0x29, 0x73, 0x81, 0x00,
  0xC0,
  // mouse
  0x05, 0x01, 0x09, 0x02, 0xA1, 0x01, 0x85, 0x02, 0x09, 0x01, 0xA1, 0x00,
  0x05, 0x09, 0x19, 0x01, 0x29, 0x03, 0x15, 0x00, 0x25, 0x01, 0x95, 0x03, 0x75, 0x01, 0x81, 0x02,
  0x95, 0x01, 0x75, 0x05, 0x81, 0x03,
  0x05, 0x01, 0x09, 0x30, 0x09, 0x31, 0x09, 0x38, 0x15, 0x81, 0x25, 0x7F, 0x75, 0x08, 0x95, 0x03, 0x81, 0x06,
  0xC0, 0xC0
};

NimBLEHIDDevice* hid;
NimBLECharacteristic* kbIn;
NimBLECharacteristic* msIn;

class ServerCallbacks : public NimBLEServerCallbacks {
  void onConnect(NimBLEServer* s, NimBLEConnInfo& info) override {
    bleConn = true;
    s->updateConnParams(info.getConnHandle(), 6, 12, 0, 200); // faster updates
  }
  void onDisconnect(NimBLEServer* s, NimBLEConnInfo& info, int reason) override {
    bleConn = false;
  }
};

void bleBegin() {
  NimBLEDevice::init("Gestura");
  NimBLEDevice::setSecurityAuth(true, false, true);
  NimBLEDevice::setSecurityIOCap(BLE_HS_IO_NO_INPUT_OUTPUT);
  NimBLEServer* srv = NimBLEDevice::createServer();
  srv->setCallbacks(new ServerCallbacks());
  srv->advertiseOnDisconnect(true);

  hid = new NimBLEHIDDevice(srv);
  hid->setManufacturer("Gestura");
  hid->setPnp(0x02, 0xe502, 0xa111, 0x0210);
  hid->setHidInfo(0x00, 0x01);
  hid->setReportMap((uint8_t*)REPORT_MAP, sizeof(REPORT_MAP));
  kbIn = hid->getInputReport(1);
  msIn = hid->getInputReport(2);
  hid->setBatteryLevel(100);
  srv->start();

  NimBLEAdvertising* adv = NimBLEDevice::getAdvertising();
  adv->setAppearance(0x03C1);
  adv->addServiceUUID(hid->getHidService()->getUUID());
  adv->setName("Gestura");
  adv->enableScanResponse(true);
  adv->start();
}

// ---------- output state ----------
bool keyW = false, keyA = false, keyS = false, keyD = false;
bool pttHeld = false;
uint32_t voiceTapUntil = 0;             // F13 tap that starts voice
uint32_t touchDownAt[3] = {0, 0, 0};
bool comboFired = false, comboUsed = false;
const uint32_t COMBO_MS = 700;          // hold both touches this long before voice starts
const uint32_t TAP_MS = 600;            // shorter than this counts as a tap
uint32_t jumpUntil = 0, lastJump = 0;
uint32_t swingUntil = 0, lastSwing = 0, suppressMouseUntil = 0;
float accX = 0, accY = 0;
float sentX = 0, sentY = 0;   // how much absolute aim has already been sent
float rxS = 0, ryS = 0;   // smoothed mouse hand rates
float fbS = 0, lrS = 0;   // smoothed move hand tilt
int8_t wheelPending = 0;
uint8_t lastKb[8] = {0};
uint8_t lastBtns = 0;
uint8_t mouseBtns = 0;
bool touch[3] = {false, false, false};

void sendKeyboard(bool force = false) {
  uint8_t r[8] = {0};
  int i = 2;
  uint32_t now = millis();
  if (keyW) r[i++] = KEY_W;
  if (keyA) r[i++] = KEY_A;
  if (keyS) r[i++] = KEY_S;
  if (keyD) r[i++] = KEY_D;
  if (now < jumpUntil) r[i++] = KEY_SPACE;
  if ((pttHeld || now < voiceTapUntil) && i < 8) r[i++] = KEY_F13;
  if (!force && memcmp(r, lastKb, 8) == 0) return;
  memcpy(lastKb, r, 8);

  if (S.outUsb) {   // USB: the laptop bridge presses these keys
    Serial.printf("GG:K:%d%d%d%d%d%d\n", keyW, keyA, keyS, keyD,
                  now < jumpUntil, (pttHeld || now < voiceTapUntil));
  }
  if (!S.outBle || !bleConn) return;
  kbIn->setValue(r, 8);
  kbIn->notify();
}

void sendMouse(int8_t dx, int8_t dy, int8_t wheel, bool force = false) {
  if (!force && dx == 0 && dy == 0 && wheel == 0 && mouseBtns == lastBtns) return;
  if (S.outUsb) Serial.printf("GG:M:%d,%d,%d,%d\n", dx, dy, mouseBtns, wheel);
  lastBtns = mouseBtns;
  if (!S.outBle || !bleConn) return;
  uint8_t r[4] = {mouseBtns, (uint8_t)dx, (uint8_t)dy, (uint8_t)wheel};
  msIn->setValue(r, 4);
  msIn->notify();
}

void releaseAll() {
  keyW = keyA = keyS = keyD = false;
  jumpUntil = 0;
  swingUntil = 0;
  mouseBtns = 0;
  accX = accY = 0;
  wheelPending = 0;
  sendKeyboard(true);
  sendMouse(0, 0, 0, true);
}

// ---------- zeroing ----------
void zeroHands(uint8_t mask) {
  zeroing = true;
  releaseAll();
  updateLeds();
  float sgx[2] = {0}, sgy[2] = {0}, sgz[2] = {0}, sp[2] = {0}, sr[2] = {0};
  int n[2] = {0, 0};
  for (int i = 0; i < 200; i++) {            // ~1 second, hold still
    for (int h = 0; h < 2; h++) {
      if (!(mask & (1 << h)) || !imu[h].ok) continue;
      float ax, ay, az, gx, gy, gz;
      if (!imuRead(imu[h], ax, ay, az, gx, gy, gz)) continue;
      float p, r;
      accAngles(ax, ay, az, p, r);
      sgx[h] += gx; sgy[h] += gy; sgz[h] += gz; sp[h] += p; sr[h] += r;
      n[h]++;
    }
    delay(5);
  }
  for (int h = 0; h < 2; h++) {
    if (n[h] < 50) continue;
    Imu& m = imu[h];
    m.bx = sgx[h] / n[h]; m.by = sgy[h] / n[h]; m.bz = sgz[h] / n[h];
    m.pitch = m.p0 = sp[h] / n[h];
    m.roll = m.r0 = sr[h] / n[h];
    m.yaw = 0;
  }
  fbS = lrS = rxS = ryS = 0;
  sentX = sentY = 0;
  zeroing = false;
}

// ---------- inputs ----------
void applyTouchPins() {
  for (int i = 0; i < 3; i++)
    pinMode(PIN_TOUCH[i], S.touchMode == 0 ? INPUT_PULLDOWN : INPUT_PULLUP);
}

bool readTouch(int i) {
  int v = digitalRead(PIN_TOUCH[i]);
  return S.touchMode == 0 ? v == HIGH : v == LOW;
}

Debounce dTouch[3], dBtn[4];
const int BTN_PINS[4] = {BTN_A, BTN_B, BTN_C, BTN_D};

// ---------- main control step (100Hz) ----------
void controlStep(float dt) {
  uint32_t now = millis();

  for (int h = 0; h < 2; h++) {
    if (imu[h].ok) imuUpdate(imu[h], dt);
    else if (now - imu[h].lastTry > 1000) {  // wire came loose? keep retrying
      imu[h].lastTry = now;
      imu[h].ok = imuInit(imu[h]);
    }
  }

  // board buttons (LOW = pressed)
  bool bPress[4];
  for (int i = 0; i < 4; i++) bPress[i] = dBtn[i].update(digitalRead(BTN_PINS[i]) == LOW, now);
  if (bPress[0]) zeroHands(3);
  if (bPress[2]) S.clickRight = !S.clickRight;
  if (bPress[3]) { paused = !paused; releaseAll(); }

  // touch pads
  bool tRelease[3];
  for (int i = 0; i < 3; i++) {
    bool was = touch[i];
    if (dTouch[i].update(readTouch(i), now)) touchDownAt[i] = now;
    touch[i] = dTouch[i].state;
    tRelease[i] = was && !touch[i];
  }

  // hold touch 1 + touch 2 together: after a short delay the glove holds F13
  // down for as long as you keep holding, so the laptop records while you talk
  if (touch[0] && touch[1]) {
    comboUsed = true;
    uint32_t since = max(touchDownAt[0], touchDownAt[1]);
    if (!comboFired && now - since >= COMBO_MS) comboFired = true;
  }
  bool wasCombo = comboUsed;
  if (!touch[0] && !touch[1]) comboFired = comboUsed = false;

  // board button B is the backup hold-to-talk
  pttHeld = comboFired || dBtn[1].state;

  if (paused || zeroing) { sendKeyboard(); return; }

  // quick taps (on release, and not part of the combo) move the hotbar
  if (!wasCombo) {
    if (tRelease[2] && now - touchDownAt[2] < TAP_MS) wheelPending -= 1;  // next slot
  }

  // ----- hand 1: mouse + swing click -----
  if (imu[0].ok) {
    float rx = gyroAxis(imu[0], S.axX) * (S.invX ? -1 : 1);
    float ry = gyroAxis(imu[0], S.axY) * (S.invY ? -1 : 1);
    float sw = gyroAxis(imu[0], S.swAx) * (S.swInv ? -1 : 1);
    if (sw > S.swTh && now - lastSwing > 300) {
      lastSwing = now;
      swingUntil = now + 60;
      suppressMouseUntil = now + 200; // don't let the swing jerk the camera
    }
    // while the hand is held still, re-learn the gyro bias. this is what
    // kills the slow left/right creep.
    float spin = fabsf(imu[0].gx) + fabsf(imu[0].gy) + fabsf(imu[0].gz);
    bool still = spin < 8.0f;
    if (still) {
      imu[0].bx += imu[0].gx * 0.05f;
      imu[0].by += imu[0].gy * 0.05f;
      imu[0].bz += imu[0].gz * 0.05f;
    }

    if (S.absMouse) {
      // absolute aim: hand angle = camera angle, so putting your hand back
      // at the zero position puts the view back where it started
      float tx = absAngle(imu[0], S.axX) * (S.invX ? -1 : 1) * S.sens * 12.0f;
      float ty = absAngle(imu[0], S.axY) * (S.invY ? -1 : 1) * S.sens * 12.0f;
      if (still || now < suppressMouseUntil) {
        // hand isn't really moving (or is mid-swing): absorb it so leftover
        // drift never reaches the screen
        sentX = tx;
        sentY = ty;
      } else {
        float ex = tx - sentX, ey = ty - sentY;
        if (fabsf(ex) > 1.0f) { accX += ex; sentX = tx; }   // ignore tiny creep
        if (S.lookY && fabsf(ey) > 1.0f) { accY += ey; sentY = ty; }
      }
    } else {
      // relative aim: how fast you turn your hand = how fast the view turns.
      // Smoothed, deadzoned, and gated on "actually moving" so a resting hand
      // can never creep the camera.
      rxS = rxS * S.smooth + rx * (1 - S.smooth);
      ryS = ryS * S.smooth + ry * (1 - S.smooth);
      if (now >= suppressMouseUntil && !still) {
        accX += curve(rxS, S.dead) * S.sens * dt;
        if (S.lookY) accY += curve(ryS, S.dead) * S.sens * dt;
      }
    }
  }
  // touch 1 = left click, touch 2 = right click, both together = voice combo
  // (no clicks while the combo is being held)
  uint8_t swingBit = S.clickRight ? 2 : 1;
  mouseBtns = 0;
  if (now < swingUntil) mouseBtns |= swingBit;
  if (!(touch[0] && touch[1])) {
    if (touch[0]) mouseBtns |= 1;   // left
    if (touch[1]) mouseBtns |= 2;   // right
  }
  if (touch[2]) mouseBtns |= 2;     // optional third pad = right click

  int dx = constrain((int)accX, -127, 127);
  int dy = constrain((int)accY, -127, 127);
  accX -= dx; accY -= dy;
  int8_t wheel = constrain(wheelPending, -127, 127);
  wheelPending = 0;
  sendMouse(dx, dy, wheel);

  // ----- hand 2: WASD + jump -----
  if (imu[1].ok) {
    float p = relPitch(imu[1]), r = relRoll(imu[1]);
    float fb = (S.swapTilt ? r : p) * (S.invFB ? -1 : 1);
    float lr = (S.swapTilt ? p : r) * (S.invLR ? -1 : 1);
    fbS = fbS * S.smooth + fb * (1 - S.smooth);
    lrS = lrS * S.smooth + lr * (1 - S.smooth);
    float afb = fabsf(fbS), alr = fabsf(lrS);

    // keys release well before they trigger, so coming back to neutral
    // definitely stops you walking
    float hyst = S.release;
    bool fbOn = (keyW || keyS) ? afb > S.tilt - hyst : afb > S.tilt;
    bool lrOn = (keyA || keyD) ? alr > S.sideTilt - hyst : alr > S.sideTilt;

    // slow auto re-centre: while the hand is held still and roughly level,
    // drag the zero point towards where it actually is, so drift can't
    // build up into phantom walking
    if (S.autoZero && !fbOn && !lrOn) {
      float spin = fabsf(imu[1].gx) + fabsf(imu[1].gy) + fabsf(imu[1].gz);
      if (spin < 10.0f && afb < S.tilt * 0.7f && alr < S.sideTilt * 0.7f) {
        imu[1].p0 += (imu[1].pitch - imu[1].p0) * 0.01f;
        imu[1].r0 += (imu[1].roll - imu[1].r0) * 0.01f;
      }
    }

    // only the strongest direction wins, unless diagonals are on and
    // both tilts are close in size
    if (fbOn && lrOn) {
      if (!S.diag) {
        if (afb >= alr) lrOn = false; else fbOn = false;
      } else {
        if (alr < afb * 0.6f) lrOn = false;
        else if (afb < alr * 0.6f) fbOn = false;
      }
    }
    keyW = fbOn && fbS > 0;
    keyS = fbOn && fbS < 0;
    keyD = lrOn && lrS > 0;
    keyA = lrOn && lrS < 0;
    if (S.jumpOn && imu[1].amag < S.jumpTh && now - lastJump > 450) {
      lastJump = now;
      jumpUntil = now + 90;
    }
  } else {
    keyW = keyA = keyS = keyD = false;
  }
  sendKeyboard();
}

// ---------- web page ----------
WebServer server(80);

String handJson(const Imu& m) {
  char b[200];
  snprintf(b, sizeof(b),
           "{\"ok\":%d,\"p\":%.1f,\"r\":%.1f,\"y\":%.1f,\"gx\":%.0f,\"gy\":%.0f,\"gz\":%.0f,\"a\":%.2f}",
           m.ok, relPitch(m), relRoll(m), m.yaw, m.gx, m.gy, m.gz, m.amag);
  return b;
}

String settingsJson() {
  char b[480];
  snprintf(b, sizeof(b),
           "{\"sens\":%.2f,\"dead\":%.1f,\"axX\":%d,\"axY\":%d,\"invX\":%d,\"invY\":%d,\"lookY\":%d,"
           "\"swAx\":%d,\"swInv\":%d,\"swTh\":%.0f,\"clickRight\":%d,\"tilt\":%.0f,\"swapTilt\":%d,"
           "\"invFB\":%d,\"invLR\":%d,\"jumpOn\":%d,\"jumpTh\":%.2f,\"touchMode\":%d,"
           "\"smooth\":%.2f,\"diag\":%d,\"sideTilt\":%.0f}",
           S.sens, S.dead, S.axX, S.axY, S.invX, S.invY, S.lookY, S.swAx, S.swInv, S.swTh,
           S.clickRight, S.tilt, S.swapTilt, S.invFB, S.invLR, S.jumpOn, S.jumpTh, S.touchMode,
           S.smooth, S.diag, S.sideTilt, S.outBle, S.outUsb, S.release, S.autoZero, S.absMouse);
  return b;
}

String dataJson() {
  uint32_t now = millis();
  return "{\"ble\":" + String(bleConn) + ",\"paused\":" + String(paused) +
             ",\"zeroing\":" + String(zeroing) +
             ",\"h\":[" + handJson(imu[0]) + "," + handJson(imu[1]) + "]" +
             ",\"k\":{\"w\":" + String(keyW) + ",\"a\":" + String(keyA) + ",\"s\":" + String(keyS) +
             ",\"d\":" + String(keyD) + ",\"j\":" + String(now < lastJump + 300) +
             ",\"l\":" + String((mouseBtns & 1) || (lastSwing && now - lastSwing < 300 && !S.clickRight)) +
             ",\"r\":" + String((mouseBtns & 2) || (lastSwing && now - lastSwing < 300 && S.clickRight)) +
             ",\"v\":" + String(pttHeld || now < voiceTapUntil + 1000) + "}" +
             ",\"t\":[" + String(touch[0]) + "," + String(touch[1]) + "," + String(touch[2]) + "]" +
             ",\"s\":" + settingsJson() + "}";
}

void handleData() { server.send(200, "application/json", dataJson()); }

void setKey(const String& k, const String& v) {
  float f = v.toFloat();
  int i = v.toInt();
  if (k == "sens") S.sens = f;
  else if (k == "dead") S.dead = f;
  else if (k == "axX") S.axX = i;
  else if (k == "axY") S.axY = i;
  else if (k == "invX") S.invX = i;
  else if (k == "invY") S.invY = i;
  else if (k == "lookY") S.lookY = i;
  else if (k == "swAx") S.swAx = i;
  else if (k == "swInv") S.swInv = i;
  else if (k == "swTh") S.swTh = f;
  else if (k == "clickRight") S.clickRight = i;
  else if (k == "tilt") S.tilt = f;
  else if (k == "swapTilt") S.swapTilt = i;
  else if (k == "invFB") S.invFB = i;
  else if (k == "invLR") S.invLR = i;
  else if (k == "jumpOn") S.jumpOn = i;
  else if (k == "jumpTh") S.jumpTh = f;
  else if (k == "touchMode") { S.touchMode = i; applyTouchPins(); }
  else if (k == "smooth") S.smooth = constrain(f, 0.0f, 0.95f);
  else if (k == "diag") S.diag = i;
  else if (k == "sideTilt") S.sideTilt = f;
  else if (k == "outBle") S.outBle = i;
  else if (k == "outUsb") S.outUsb = i;
  else if (k == "release") S.release = f;
  else if (k == "autoZero") S.autoZero = i;
  else if (k == "absMouse") S.absMouse = i;
}

void runCommand(const String& c) {
  if (c == "zero0") zeroHands(1);
  else if (c == "zero1") zeroHands(2);
  else if (c == "zero") zeroHands(3);
  else if (c == "save") saveSettings();
  else if (c == "defaults") { setDefaults(); applyTouchPins(); }
  else if (c == "pause") { paused = !paused; releaseAll(); }
  else if (c.startsWith("hurt")) hurtUntil = millis() + 600;  // flash red (from the game)
}

// ---------- HiveMQ cloud (MQTT over TLS) ----------
void mqttMessage(char* topic, byte* payload, unsigned int len) {
  String t(topic), body;
  for (unsigned int i = 0; i < len; i++) body += (char)payload[i];
  if (t.endsWith("/cmd")) {
    runCommand(body);
    return;
  }
  if (!t.endsWith("/set")) return;
  // "key=value&key=value"
  int start = 0;
  while (start < (int)body.length()) {
    int amp = body.indexOf('&', start);
    if (amp < 0) amp = body.length();
    String pair = body.substring(start, amp);
    int eq = pair.indexOf('=');
    if (eq > 0) setKey(pair.substring(0, eq), pair.substring(eq + 1));
    start = amp + 1;
  }
}

void mqttBegin() {
  if (!netSsid.length()) return;
  WiFi.begin(netSsid.c_str(), netPass.c_str());
  if (!mqHost.length()) return;
  tls.setInsecure();               // no certificate check (fine for a hackathon)
  mqtt.setServer(mqHost.c_str(), mqPort);
  mqtt.setBufferSize(1024);
  mqtt.setCallback(mqttMessage);
  mqtt.setSocketTimeout(3);
}

void mqttLoop() {
  uint32_t now = millis();
  if (!mqHost.length() || WiFi.status() != WL_CONNECTED) return;

  if (!mqtt.connected()) {
    if (now - lastMqttTry < 5000) return;
    lastMqttTry = now;
    String id = "glove-" + WiFi.macAddress();
    if (mqtt.connect(id.c_str(), mqUser.c_str(), mqPass.c_str(),
                     (mqTopic + "/status").c_str(), 0, true, "offline")) {
      mqtt.publish((mqTopic + "/status").c_str(), "online", true);
      mqtt.subscribe((mqTopic + "/set").c_str());
      mqtt.subscribe((mqTopic + "/cmd").c_str());
      Serial.println("MQTT connected");
    }
    return;
  }

  mqtt.loop();
  if (now - lastPub >= 150) {      // ~7 updates a second
    lastPub = now;
    String j = dataJson();
    mqtt.publish((mqTopic + "/telemetry").c_str(), j.c_str());
  }
}

void webBegin() {
  WiFi.mode(WIFI_AP_STA);          // own hotspot AND joins your WiFi
  WiFi.softAP(AP_SSID, AP_PASS);
  server.on("/", [] { server.send_P(200, "text/html", PAGE); });
  server.on("/data", handleData);
  server.on("/set", [] {
    for (int i = 0; i < server.args(); i++) setKey(server.argName(i), server.arg(i));
    server.send(200, "text/plain", "ok");
  });
  server.on("/save", [] { saveSettings(); server.send(200, "text/plain", "saved"); });
  server.on("/defaults", [] { setDefaults(); applyTouchPins(); server.send(200, "text/plain", "ok"); });
  server.on("/zero", [] {
    int h = server.hasArg("h") ? server.arg("h").toInt() : 3;
    zeroHands(h == 0 ? 1 : h == 1 ? 2 : 3);
    server.send(200, "text/plain", "ok");
  });
  server.on("/pause", [] { paused = !paused; releaseAll(); server.send(200, "text/plain", "ok"); });
  server.on("/net", [] {
    if (server.hasArg("ssid")) netSsid = server.arg("ssid");
    if (server.hasArg("pass")) netPass = server.arg("pass");
    if (server.hasArg("host")) mqHost = server.arg("host");
    if (server.hasArg("port")) mqPort = server.arg("port").toInt();
    if (server.hasArg("user")) mqUser = server.arg("user");
    if (server.hasArg("mpass")) mqPass = server.arg("mpass");
    if (server.hasArg("topic")) mqTopic = server.arg("topic");
    saveNet();
    server.send(200, "text/plain", "saved");
    delay(100);
    ESP.restart();                 // easiest way to reconnect cleanly
  });
  server.on("/netinfo", [] {
    String j = "{\"ssid\":\"" + netSsid + "\",\"host\":\"" + mqHost + "\",\"port\":" + String(mqPort) +
               ",\"user\":\"" + mqUser + "\",\"topic\":\"" + mqTopic +
               "\",\"wifi\":" + String(WiFi.status() == WL_CONNECTED) +
               ",\"ip\":\"" + WiFi.localIP().toString() +
               "\",\"mqtt\":" + String(mqtt.connected()) + "}";
    server.send(200, "application/json", j);
  });
  server.begin();
}

// ---------- setup / loop ----------
uint32_t lastCtl = 0, lastLed = 0;

void setup() {
  Serial.begin(115200);
  loadSettings();

  FastLED.addLeds<WS2812B, PIN_LED, GRB>(leds, NUM_LEDS);
  FastLED.setBrightness(40);

  for (int i = 0; i < 4; i++) pinMode(BTN_PINS[i], INPUT); // board has pull-ups
  applyTouchPins();

  Wire.begin(PIN_SDA, PIN_SCL, 100000); // 100kHz is safer on long wires
  Wire.setTimeOut(20);
  for (int h = 0; h < 2; h++) {
    imu[h].ok = imuInit(imu[h]);
    Serial.printf("gyro 0x%02X: %s\n", imu[h].addr, imu[h].ok ? "found" : "NOT FOUND");
  }

  delay(300);
  zeroHands(3); // hold both hands still on power up

  bleBegin();
  loadNet();
  webBegin();
  mqttBegin();
  Serial.println("Gestura ready. WiFi: Gestura / gestura123 -> http://192.168.4.1");
  lastCtl = micros();
}

void loop() {
  server.handleClient();
  mqttLoop();

  // the laptop bridge sends 'P' every half second so we know it's listening
  while (Serial.available()) {
    if (Serial.read() == 'P') usbSeen = millis();
  }

  uint32_t now = micros();
  if (now - lastCtl >= 10000) {
    float dt = (now - lastCtl) / 1e6f;
    lastCtl = now;
    if (dt > 0.05f) dt = 0.05f;
    controlStep(dt);
  }

  if (millis() - lastLed > 50) {
    lastLed = millis();
    updateLeds();
  }
}
