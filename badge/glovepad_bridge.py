"""GlovePad bridge: reads button events from the Hacker Badge over USB
and presses the matching Minecraft keys on this laptop.

Setup (once):
    pip install pyserial pydirectinput

Run:
    python glovepad_bridge.py          (finds the badge by itself)
    python glovepad_bridge.py COM7     (or pick the port)

Close the Badge IDE tab first, since only one program can use the port.
Click into Minecraft after starting so the keys go to the game.
"""

import ctypes
import re
import sys
import time

import pydirectinput
import serial
import serial.tools.list_ports

pydirectinput.PAUSE = 0  # no built-in delay between key presses

ESPRESSIF_VID = 0x303A
EVENT = re.compile(r"GGB:(\w+):([PR])")

# Held while the badge button is held
HOLD_KEYS = {"UP": "w", "DOWN": "s", "LEFT": "a", "RIGHT": "d", "A": "space", "AUX1": "ctrl"}
MOUSE_HOLD = {}
# One quick press
TAP_KEYS = {"B": "e", "START": "esc", "SHAKE": "f5"}
# Mouse wheel: up = previous hotbar slot, down = next
SCROLL = {}

held = set()


def scroll(delta):
    ctypes.windll.user32.mouse_event(0x0800, 0, 0, delta, 0)


def release_all():
    for name in list(held):
        if name in HOLD_KEYS:
            pydirectinput.keyUp(HOLD_KEYS[name])
        elif name in MOUSE_HOLD:
            pydirectinput.mouseUp(button=MOUSE_HOLD[name])
    held.clear()


def handle(name, kind):
    if name == "HELLO":
        release_all()
        print("Badge app opened")
        return
    if name == "BYE":
        release_all()
        print("Badge app closed")
        return

    if kind == "P":
        if name in HOLD_KEYS:
            pydirectinput.keyDown(HOLD_KEYS[name])
            held.add(name)
        elif name in MOUSE_HOLD:
            pydirectinput.mouseDown(button=MOUSE_HOLD[name])
            held.add(name)
        elif name in TAP_KEYS:
            pydirectinput.press(TAP_KEYS[name])
        elif name in SCROLL:
            scroll(SCROLL[name])
    elif kind == "R" and name in held:
        if name in HOLD_KEYS:
            pydirectinput.keyUp(HOLD_KEYS[name])
        else:
            pydirectinput.mouseUp(button=MOUSE_HOLD[name])
        held.discard(name)

    print(f"{name} {'down' if kind == 'P' else 'up'}")


def find_port():
    if len(sys.argv) > 1:
        return sys.argv[1]
    for p in serial.tools.list_ports.comports():
        if p.vid == ESPRESSIF_VID:
            return p.device
    return None


def open_port(port):
    ser = serial.Serial()
    ser.port = port
    ser.baudrate = 115200
    ser.timeout = 0.1
    # Keep DTR/RTS low so opening the port doesn't reset the badge
    ser.dtr = False
    ser.rts = False
    ser.open()
    return ser


def main():
    print("GlovePad bridge - waiting for badge (Ctrl+C to quit)")
    while True:
        port = find_port()
        if not port:
            time.sleep(1)
            continue
        try:
            with open_port(port) as ser:
                print(f"Connected to badge on {port}")
                while True:
                    line = ser.readline().decode("utf-8", errors="ignore")
                    m = EVENT.search(line)
                    if m:
                        handle(m.group(1), m.group(2))
        except serial.SerialException as e:
            release_all()
            print(f"Lost badge ({e}), retrying...")
            time.sleep(1)


if __name__ == "__main__":
    try:
        main()
    except KeyboardInterrupt:
        release_all()
