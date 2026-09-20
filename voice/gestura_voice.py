"""Gestura voice control.

Hold both touch pads on the glove for 3 seconds (or press board button B) and
the glove sends F13. This script hears that, records what you say, asks Gemini
what to do, and presses the keys in your game.

Setup (once):
    pip install google-genai sounddevice numpy pynput pydirectinput python-dotenv
    put your Gemini key in voice/.env  ->  GEMINI_API_KEY=xxxxx
    (free key from aistudio.google.com)

Run:
    python gestura_voice.py

Say things like:
    "jump"                         "open inventory"
    "walk forward until I say stop"   "stop"
    "turn around"                  "mine this block"
"""

import json
import os
import queue
import sys
import threading
import time

import numpy as np
import pydirectinput
import sounddevice as sd
from dotenv import load_dotenv
from google import genai
from google.genai import types
from pynput import keyboard

pydirectinput.PAUSE = 0

SAMPLE_RATE = 16000
SILENCE_LEVEL = 0.012     # below this counts as quiet
SILENCE_STOP = 1.0        # seconds of quiet that ends a recording
MAX_SECONDS = 8
F13 = keyboard.KeyCode.from_vk(0x7C)

# what Gemini is allowed to do
ALLOWED_KEYS = ["w", "a", "s", "d", "space", "shift", "ctrl", "e", "q", "esc",
                "1", "2", "3", "4", "5", "6", "7", "8", "9", "f5"]
ALLOWED_CLICKS = ["left", "right"]

PROMPT = f"""You control a Minecraft player through keyboard and mouse.
Listen to the audio and reply with ONLY a JSON object, no markdown, like:

{{"say": "walking forward", "tap": ["space"], "hold": ["w"], "click": null,
  "hold_seconds": 0, "stop": false}}

Fields:
- say: a very short confirmation (max 6 words)
- tap: keys to press once. Allowed: {ALLOWED_KEYS}
- hold: keys to hold down until told to stop. Same list.
- click: "left", "right" or null. Left click = attack/mine, right = use/place.
- hold_seconds: if the player asked for a set time ("forward for 3 seconds"),
  put the number here. 0 means hold until they say stop.
- stop: true if the player asked to stop, halt, freeze or cancel.

Examples:
"jump" -> {{"say":"jumping","tap":["space"],"hold":[],"click":null,"hold_seconds":0,"stop":false}}
"walk forward until I say stop" -> {{"say":"walking","tap":[],"hold":["w"],"click":null,"hold_seconds":0,"stop":false}}
"mine this block" -> {{"say":"mining","tap":[],"hold":[],"click":"left","hold_seconds":3,"stop":false}}
"stop" -> {{"say":"stopped","tap":[],"hold":[],"click":null,"hold_seconds":0,"stop":true}}
If you cannot tell what they said, reply with everything empty and stop false.
"""

held_keys = set()
held_click = None
audio_q = queue.Queue()
recording = threading.Event()
client = None


# ---------- input ----------
def release_all():
    global held_click
    for k in list(held_keys):
        pydirectinput.keyUp(k)
    held_keys.clear()
    if held_click:
        pydirectinput.mouseUp(button=held_click)
        held_click = None


def do_action(a):
    global held_click
    if a.get("stop"):
        release_all()
        print("  -> stopped everything")
        return

    for k in a.get("tap") or []:
        if k in ALLOWED_KEYS:
            pydirectinput.press(k)

    for k in a.get("hold") or []:
        if k in ALLOWED_KEYS and k not in held_keys:
            pydirectinput.keyDown(k)
            held_keys.add(k)

    click = a.get("click")
    if click in ALLOWED_CLICKS and held_click is None:
        pydirectinput.mouseDown(button=click)
        held_click = click

    secs = a.get("hold_seconds") or 0
    if secs and (held_keys or held_click):
        threading.Timer(min(float(secs), 15.0), release_all).start()


# ---------- audio ----------
def on_audio(indata, frames, time_info, status):
    if recording.is_set():
        audio_q.put(indata.copy())


def record_until_quiet():
    """Record from the mic until the speaker goes quiet (or the max time)."""
    while not audio_q.empty():
        audio_q.get()
    recording.set()
    chunks, quiet_for, started = [], 0.0, time.time()
    heard_anything = False

    while time.time() - started < MAX_SECONDS:
        try:
            block = audio_q.get(timeout=0.5)
        except queue.Empty:
            continue
        chunks.append(block)
        level = float(np.abs(block).mean())
        if level > SILENCE_LEVEL:
            heard_anything = True
            quiet_for = 0.0
        else:
            quiet_for += len(block) / SAMPLE_RATE
            if heard_anything and quiet_for > SILENCE_STOP:
                break

    recording.clear()
    if not chunks or not heard_anything:
        return None
    audio = np.concatenate(chunks)
    return (audio * 32767).astype(np.int16).tobytes()


def wav_bytes(pcm):
    """Wrap raw PCM in a WAV header so Gemini can read it."""
    import struct
    n = len(pcm)
    header = b"RIFF" + struct.pack("<I", 36 + n) + b"WAVEfmt " + struct.pack(
        "<IHHIIHH", 16, 1, 1, SAMPLE_RATE, SAMPLE_RATE * 2, 2, 16)
    return header + b"data" + struct.pack("<I", n) + pcm


# ---------- gemini ----------
def ask_gemini(pcm):
    reply = client.models.generate_content(
        model="gemini-2.0-flash",
        contents=[
            PROMPT,
            types.Part.from_bytes(data=wav_bytes(pcm), mime_type="audio/wav"),
        ],
        config=types.GenerateContentConfig(
            response_mime_type="application/json",
            temperature=0,
        ),
    )
    return json.loads(reply.text)


def handle_voice():
    print("* listening...")
    pcm = record_until_quiet()
    if not pcm:
        print("  (heard nothing)")
        return
    print("  thinking...")
    try:
        action = ask_gemini(pcm)
    except Exception as e:
        print(f"  Gemini error: {e}")
        return
    print(f"  {action.get('say') or action}")
    do_action(action)


# ---------- main ----------
def main():
    global client
    load_dotenv(os.path.join(os.path.dirname(__file__), ".env"))
    key = os.getenv("GEMINI_API_KEY")
    if not key:
        print("No GEMINI_API_KEY found. Put it in voice/.env like:")
        print("GEMINI_API_KEY=your_key_here")
        sys.exit(1)
    client = genai.Client(api_key=key)

    stream = sd.InputStream(samplerate=SAMPLE_RATE, channels=1, dtype="float32",
                            blocksize=1600, callback=on_audio)
    stream.start()

    busy = threading.Event()

    def on_press(k):
        if k == F13 and not busy.is_set():
            busy.set()
            threading.Thread(
                target=lambda: (handle_voice(), busy.clear()), daemon=True).start()

    print("Gestura voice ready.")
    print("Hold both touch pads 3s (or board button B) and speak. Ctrl+C to quit.")
    with keyboard.Listener(on_press=on_press) as listener:
        listener.join()


if __name__ == "__main__":
    try:
        main()
    except KeyboardInterrupt:
        release_all()
        print("\nstopped")
