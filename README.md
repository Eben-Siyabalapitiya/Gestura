# GameGlove — Hack the North 2026

Motion control gloves for gaming. One hand is the mouse, the other hand is the keyboard, so you can play any game (like Minecraft) with whatever you're holding, even a water bottle as a sword.

## What it does

| Hand | Motion | Does |
|---|---|---|
| Mouse hand (gyro 1) | Turn hand left/right | Mouse moves (look around) |
| | Swing down | Left click (attack) |
| Move hand (gyro 2) | Tilt forward/back | W / S |
| | Tilt left/right | A / D |
| | Quick drop | Space (jump) |
| Touch 1 | Tap | Next hotbar slot |
| Touch 2 | Tap | Previous hotbar slot |
| Touch 1 + 2 | Hold both 3 sec | Start voice AI (sends F13) |
| Touch 3 | Hold | Right click (place / use) |

Board buttons: **A** = zero both hands, **B** = voice push-to-talk (sends F13), **C** = swap left/right click, **D** = pause.

## Folder

```
Hack the North Project/
  README.md          <- this file
  GameGlove/
    GameGlove.ino    <- main code for the LoR Core V3
    page.h           <- calibration web page
    types.h          <- settings / gyro structs
```

## Parts

- LoR Core V3 (ESP32)
- 2x MPU6050 (or MPU6500) gyros
- 3x TTP223 touch sensors (or copper tape)
- Gloves, long flexible wires (~1–1.5 m)
- USB power bank + USB-C cable

## Wiring (LoR Core V3 labels)

| Part pin | → | LoR Core |
|---|---|---|
| Gyro 1 VCC | → | AUX 10 (3.3V) |
| Gyro 1 GND | → | Port 11 GND |
| Gyro 1 SCL | → | Port 11 SIG |
| Gyro 1 SDA | → | Port 12 SIG |
| Gyro 1 AD0 | → | nothing (address 0x68) |
| Gyro 2 VCC | → | AUX 11 (3.3V) |
| Gyro 2 GND | → | Port 12 GND |
| Gyro 2 SCL | → | Port 11 SIG (shared with gyro 1) |
| Gyro 2 SDA | → | Port 12 SIG (shared with gyro 1) |
| Gyro 2 AD0 | → | its own VCC pin (address 0x69) |
| Touch 1 I/O / GND | → | Port 01 SIG / Port 01 GND |
| Touch 2 I/O / GND | → | Port 02 SIG / Port 02 GND |
| Touch 3 I/O / GND | → | Port 03 SIG / Port 03 GND |
| Touch 1/2/3 VCC | → | 3.3V (split off AUX 10 / AUX 11) |

**Never use the V+ pins (middle row of each port, and AUX 12). They're 6V and will fry the sensors.**

## Uploading

1. Open `GameGlove/GameGlove.ino` in Arduino IDE
2. Libraries needed: **NimBLE-Arduino** (2.x) and **FastLED**
3. Board: **ESP32 Dev Module**
4. Tools → Partition Scheme: **Huge APP (3MB No OTA/1MB SPIFFS)**
5. Plug in the LoR Core with USB-C and upload

## Using it

1. Power on and **hold both hands still for ~1 second** while it zeros (LED 4 yellow)
2. Laptop: Bluetooth settings → Add device → **GameGlove** (only the first time, after that it reconnects on its own)
3. Calibration page: connect your phone to WiFi **GameGlove** (password `glove1234`) and open **http://192.168.4.1**

The page shows a live 3D view of each hand, the gyro bars, which keys are being pressed, zero buttons, and sliders for everything (mouse speed, deadzone, swing strength, tilt angle, jump, axis flips). Hit **Save settings** so it remembers after power off.

**LEDs:** 1 = Bluetooth (blinking blue waiting / green connected), 2 = mouse hand gyro, 3 = move hand gyro (green OK / red missing), 4 = yellow zeroing / red paused / purple swing is right click.

## Troubleshooting

- **Hand shows "missing" on the page:** check the SDA/SCL splits and power. If only one gyro works, check gyro 2's AD0 → VCC wire.
- **Mouse goes the wrong way:** tick "Flip turn" on the page. Turning moves nothing? Change "Turn axis" (watch which X/Y/Z bar moves when you turn your hand).
- **Clicks when you don't want:** raise "Swing click strength".
- **Walks by itself:** zero the move hand again, or raise "Tilt to walk".
- **Changed the Bluetooth code and Windows acts weird:** remove GameGlove from Bluetooth settings and pair it again.

## Badge controller (GlovePad)

The Hack the North badge works as an extra controller over USB.

| Badge | Minecraft |
|---|---|
| D-pad (hold) | Walk: W / A / S / D |
| A (hold) | Jump |
| B | Inventory (E) |
| START | Pause menu (Esc) |
| AUX1 (hold) | Sprint |
| Shake | Camera view (F5) |

1. Badge IDE (badge.hackthenorth.com/ide) → **Import app** → paste `badge/glovepad.lua` → **Connect** → **Push**
2. Close the IDE tab, keep the badge plugged in, open **GlovePad** on the badge
3. Laptop: `pip install pyserial pydirectinput`, then `python badge/glovepad_bridge.py`
4. Click into Minecraft

## Next up

- Voice control on the laptop (hold touch 1 + 2 for 3 sec → laptop mic → Gemini → keys) for when your hands are tired
