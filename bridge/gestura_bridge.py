"""Gestura USB bridge: reads the glove over the USB cable and moves the real
mouse / presses the real keys on this laptop. No Bluetooth pairing needed.

Setup (once):
    pip install pyserial pydirectinput

Run:
    python gestura_bridge.py           (finds the glove by itself)
    python gestura_bridge.py COM6      (or pick the port)

Close Arduino IDE's Serial Monitor first, since only one program can use the port.
While this runs, LED 1 on the glove turns cyan (or white if Bluetooth is also on).

Tip: if the glove is ALSO paired over Bluetooth you get double input. Turn one
off on the glove page (Output section) or in the cloud dashboard.
"""

import ctypes
import re
import sys
import time

import pydirectinput
import serial
import serial.tools.list_ports

pydirectinput.PAUSE = 0

CH340_VIDS = {0x1A86, 0x10C4, 0x0403, 0x303A}  # common USB-serial chips
KEYS = ["w", "a", "s", "d", "space", "f13"]
KEY_LINE = re.compile(r"GG:K:([01]{6})")
MOUSE_LINE = re.compile(r"GG:M:(-?\d+),(-?\d+),(\d+),(-?\d+)")

MOUSEEVENTF_MOVE = 0x0001
MOUSEEVENTF_WHEEL = 0x0800
BUTTON_EVENTS = {  # bit -> (down flag, up flag)
    0: (0x0002, 0x0004),  # left
    1: (0x0008, 0x0010),  # right
    2: (0x0020, 0x0040),  # middle
}

held_keys = set()
held_buttons = set()


def mouse_event(flags, dx=0, dy=0, data=0):
    ctypes.windll.user32.mouse_event(flags, dx, dy, data, 0)


def release_all():
    for k in list(held_keys):
        pydirectinput.keyUp(k)
    held_keys.clear()
    for b in list(held_buttons):
        mouse_event(BUTTON_EVENTS[b][1])
    held_buttons.clear()


def apply_keys(bits):
    for key, bit in zip(KEYS, bits):
        want = bit == "1"
        if want and key not in held_keys:
            pydirectinput.keyDown(key)
            held_keys.add(key)
        elif not want and key in held_keys:
            pydirectinput.keyUp(key)
            held_keys.discard(key)


def apply_mouse(dx, dy, buttons, wheel):
    if dx or dy:
        mouse_event(MOUSEEVENTF_MOVE, dx, dy)
    if wheel:
        mouse_event(MOUSEEVENTF_WHEEL, data=wheel * 120)
    for bit, (down, up) in BUTTON_EVENTS.items():
        want = bool(buttons & (1 << bit))
        if want and bit not in held_buttons:
            mouse_event(down)
            held_buttons.add(bit)
        elif not want and bit in held_buttons:
            mouse_event(up)
            held_buttons.discard(bit)


def find_port():
    if len(sys.argv) > 1:
        return sys.argv[1]
    for p in serial.tools.list_ports.comports():
        if p.vid in CH340_VIDS:
            return p.device
    return None


def open_port(port):
    ser = serial.Serial()
    ser.port = port
    ser.baudrate = 115200
    ser.timeout = 0.05
    ser.dtr = False   # don't reset the board when we connect
    ser.rts = False
    ser.open()
    return ser


def main():
    print("Gestura USB bridge - waiting for the glove (Ctrl+C to quit)")
    while True:
        port = find_port()
        if not port:
            time.sleep(1)
            continue
        try:
            with open_port(port) as ser:
                print(f"Connected on {port} - click into your game")
                last_ping = 0
                while True:
                    now = time.time()
                    if now - last_ping > 0.5:
                        ser.write(b"P")   # tells the glove we're here (LED turns cyan)
                        last_ping = now
                    line = ser.readline().decode("utf-8", errors="ignore")
                    if not line:
                        continue
                    m = KEY_LINE.search(line)
                    if m:
                        apply_keys(m.group(1))
                        continue
                    m = MOUSE_LINE.search(line)
                    if m:
                        dx, dy, btns, wheel = (int(x) for x in m.groups())
                        apply_mouse(dx, dy, btns, wheel)
        except serial.SerialException as e:
            release_all()
            print(f"Lost the glove ({e}), retrying...")
            time.sleep(1)


if __name__ == "__main__":
    try:
        main()
    except KeyboardInterrupt:
        release_all()
        print("\nstopped")
