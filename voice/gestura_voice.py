"""Gestura voice control.

Hold both touch pads on the glove (or board button B) and the glove holds down
F13. This script hears that, beeps so you know it's listening, records for as
long as you keep holding, asks Gemini what you meant, says "got it" plus what
it's doing, and presses the keys in your game.

Setup (once):
    pip install google-genai sounddevice numpy pynput pydirectinput python-dotenv pyttsx3
    put your Gemini key in voice/.env  ->  GEMINI_API_KEY=xxxxx
    (free key from aistudio.google.com)

Run:
    python gestura_voice.py           normal
    python gestura_voice.py --list    show microphones
    python gestura_voice.py --test    pretend the pads were held (no glove needed)

Say things like:
    "jump"                            "open inventory"
    "walk forward until I say stop"   "stop"
    "turn around"                     "mine this block"
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

SAMPLE_RATE = 16000       # what we send to Gemini
IN_RATE = SAMPLE_RATE     # what the mic actually runs at (set at startup)
SILENCE_LEVEL = 0.010     # below this counts as quiet
SILENCE_STOP = 1.2        # quiet for this long after you let go = done
MIN_SECONDS = 0.6         # ignore accidental taps
MAX_SECONDS = 10
MODEL = "gemini-3.6-flash"
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
- say: a very short confirmation (max 5 words), e.g. "walking forward"
- tap: keys to press once. Allowed: {ALLOWED_KEYS}
- hold: keys to hold down until told to stop. Same list.
- click: "left", "right" or null. Left click = attack/mine, right = use/place.
- hold_seconds: if they asked for a set time ("forward for 3 seconds"), put the
  number here. 0 means hold until they say stop.
- stop: true if they asked to stop, halt, freeze or cancel.

Examples:
"jump" -> {{"say":"jumping","tap":["space"],"hold":[],"click":null,"hold_seconds":0,"stop":false}}
"walk forward until I say stop" -> {{"say":"walking forward","tap":[],"hold":["w"],"click":null,"hold_seconds":0,"stop":false}}
"mine this block" -> {{"say":"mining","tap":[],"hold":[],"click":"left","hold_seconds":3,"stop":false}}
"stop" -> {{"say":"stopped","tap":[],"hold":[],"click":null,"hold_seconds":0,"stop":true}}
If you cannot make out the words, reply with everything empty and stop false.
"""

held_keys = set()
held_click = None
audio_q = queue.Queue()
talking = threading.Event()      # true while the pads are held
client = None


# ---------- game input ----------
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


# ---------- sound out ----------
_tts = None
_tts_lock = threading.Lock()


def beep(freq=880, ms=120):
    """Short tone, so you know it started listening without waiting for speech."""
    try:
        t = np.linspace(0, ms / 1000, int(SAMPLE_RATE * ms / 1000), endpoint=False)
        tone = (0.25 * np.sin(2 * np.pi * freq * t)).astype(np.float32)
        sd.play(tone, SAMPLE_RATE)
    except Exception:
        pass


def say(text, wait=True):
    """Speak a line out loud. Skips silently if speech isn't available."""
    if not text:
        return

    def run():
        global _tts
        try:
            with _tts_lock:
                if _tts is None:
                    import pyttsx3
                    _tts = pyttsx3.init()
                    _tts.setProperty("rate", 190)
                _tts.say(text)
                _tts.runAndWait()
        except Exception as e:
            print(f"  (voice output off: {e})")

    if wait:
        run()
    else:
        threading.Thread(target=run, daemon=True).start()


# ---------- mic ----------
def on_audio(indata, frames, time_info, status):
    if talking.is_set() or not audio_q.empty():
        audio_q.put(indata.copy())


def record_while_held():
    """Record while the pads are held, then a moment longer in case you were
    still finishing your sentence."""
    while not audio_q.empty():
        audio_q.get()
    chunks, quiet_for, started = [], 0.0, time.time()
    heard_anything = False

    while time.time() - started < MAX_SECONDS:
        try:
            block = audio_q.get(timeout=0.3)
        except queue.Empty:
            if not talking.is_set() and time.time() - started > MIN_SECONDS:
                break
            continue
        chunks.append(block)
        level = float(np.abs(block).mean())
        if level > SILENCE_LEVEL:
            heard_anything = True
            quiet_for = 0.0
        else:
            quiet_for += len(block) / IN_RATE
            # once you let go, a short pause means you're done
            if not talking.is_set() and quiet_for > SILENCE_STOP:
                break

    if not chunks:
        return None, False
    audio = np.concatenate(chunks).flatten()
    if IN_RATE != SAMPLE_RATE:      # resample to what Gemini expects
        n = int(len(audio) * SAMPLE_RATE / IN_RATE)
        audio = np.interp(np.linspace(0, len(audio), n, endpoint=False),
                          np.arange(len(audio)), audio).astype(np.float32)
    return (audio * 32767).astype(np.int16).tobytes(), heard_anything


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
        model=MODEL,
        contents=[PROMPT, types.Part.from_bytes(data=wav_bytes(pcm),
                                                mime_type="audio/wav")],
        config=types.GenerateContentConfig(response_mime_type="application/json",
                                           temperature=0),
    )
    return json.loads(reply.text)


def handle_voice():
    beep()                       # "I'm listening"
    print("* listening (keep holding)...")
    pcm, heard = record_while_held()
    if not pcm or not heard:
        print("  heard nothing")
        beep(320, 180)
        say("i didn't hear that", wait=False)
        return

    print(f"  got {len(pcm) / (SAMPLE_RATE * 2):.1f}s of audio, thinking...")
    try:
        action = ask_gemini(pcm)
    except Exception as e:
        print(f"  Gemini error: {e}")
        say("something went wrong", wait=False)
        return

    reply = action.get("say") or ""
    print(f"  -> {action}")
    do_action(action)            # act first so the game responds instantly
    say(f"got it, {reply}" if reply else "got it", wait=False)


# ---------- main ----------
def list_mics():
    apis = sd.query_hostapis()
    for i, d in enumerate(sd.query_devices()):
        if d["max_input_channels"] > 0:
            print(i, "|", d["name"], "|", apis[d["hostapi"]]["name"])


def pick_mic(want):
    """MIC in .env can be a device number or part of a name. Windows lists the
    same microphone once per audio system, so prefer WASAPI, then MME, and fall
    back to the default if it isn't plugged in."""
    if want.isdigit():
        return int(want)
    if not want:
        return None

    apis = sd.query_hostapis()
    matches = [(i, d) for i, d in enumerate(sd.query_devices())
               if d["max_input_channels"] > 0 and want.lower() in d["name"].lower()]
    if not matches:
        print(f"mic '{want}' not found (unplugged?), using the default instead")
        return None

    for preferred in ("WASAPI", "MME", "DirectSound"):
        for i, d in matches:
            if preferred.lower() in apis[d["hostapi"]]["name"].lower():
                return i
    return matches[0][0]


def main():
    global client
    if "--list" in sys.argv:
        list_mics()
        return

    load_dotenv(os.path.join(os.path.dirname(__file__), ".env"))
    key = os.getenv("GEMINI_API_KEY")
    if not key:
        print("No GEMINI_API_KEY found. Put it in voice/.env like:")
        print("GEMINI_API_KEY=your_key_here")
        sys.exit(1)
    client = genai.Client(api_key=key)

    # MIC in .env is a device number or part of its name. Numbers move around
    # when you plug things in, so a name is safer.
    device = pick_mic(os.getenv("MIC", "").strip())
    print("mic:", sd.query_devices(device, kind="input")["name"])

    # some mics refuse 16 kHz, so fall back to whatever they do support
    global IN_RATE
    stream = None
    rates = [SAMPLE_RATE,
             int(sd.query_devices(device, kind="input")["default_samplerate"]),
             48000, 44100]
    for rate in rates:
        try:
            stream = sd.InputStream(samplerate=rate, channels=1, dtype="float32",
                                    blocksize=int(rate / 10), device=device,
                                    callback=on_audio)
            stream.start()
            IN_RATE = rate
            break
        except Exception:
            stream = None
    if stream is None:
        print("could not open that microphone. Try another MIC= in .env (--list)")
        sys.exit(1)
    if IN_RATE != SAMPLE_RATE:
        print(f"(recording at {IN_RATE} Hz)")

    busy = threading.Event()

    def start_session():
        if busy.is_set():
            return
        busy.set()
        talking.set()
        threading.Thread(target=lambda: (handle_voice(), busy.clear()),
                         daemon=True).start()

    # test mode: hold ENTER-free 3 second session without the glove
    if "--test" in sys.argv:
        print("test mode: recording 4 seconds now, speak!")
        start_session()
        time.sleep(4)
        talking.clear()
        time.sleep(8)
        return

    def on_press(k):
        if k == F13:
            if not busy.is_set():
                print("F13 down (pads held)")
            start_session()

    def on_release(k):
        if k == F13:
            talking.clear()      # you let go: wrap up the recording

    print("Gestura voice ready.")
    print("Hold both touch pads (or board button B), speak, then let go.")
    print("Ctrl+C to quit.")
    with keyboard.Listener(on_press=on_press, on_release=on_release) as listener:
        listener.join()


if __name__ == "__main__":
    try:
        main()
    except KeyboardInterrupt:
        release_all()
        print("\nstopped")
