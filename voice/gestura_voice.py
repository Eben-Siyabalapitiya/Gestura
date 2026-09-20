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

import ctypes
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
# lite first: it's quick and has a far bigger free daily allowance. The others
# are fallbacks for when one runs out of quota.
MODELS = ["gemini-3.5-flash-lite", "gemini-3.1-flash-lite", "gemini-3.6-flash"]
MODEL = MODELS[0]
PX_PER_DEGREE = 8         # mouse pixels per degree of view; tune to your game
F13_VK = 0x7C             # Windows virtual key code for F13


def is_f13(k):
    """pynput reports F13 as Key.f13 on some setups and as a raw key code on
    others, so accept either."""
    if k == getattr(keyboard.Key, "f13", None):
        return True
    return getattr(k, "vk", None) == F13_VK

# what Gemini is allowed to do
ALLOWED_KEYS = ["w", "a", "s", "d", "space", "shift", "ctrl", "e", "q", "esc",
                "1", "2", "3", "4", "5", "6", "7", "8", "9", "f5"]
ALLOWED_CLICKS = ["left", "right"]

PROMPT = f"""You control a Minecraft player's keyboard and mouse. The player
talks to you casually, in whatever words they like. Work out what they meant
and reply with ONLY a JSON object, no markdown:

{{"say": "walking forward", "tap": [], "hold": ["w"], "release": [],
  "click": null, "hold_seconds": 0, "turn": 0, "pitch": 0, "stop": false}}

Fields:
- say: a short confirmation, max 5 words
- tap: keys pressed once. Allowed: {ALLOWED_KEYS}
- hold: keys held down until told otherwise. Same list.
- release: keys to let go of, for things like "stop walking" (which releases w)
- click: "left" (attack/mine), "right" (use/place) or null
- hold_seconds: how long to hold, when they gave a time like "for 3 seconds"
  or a short instruction like "walk a bit". 0 means hold until told to stop.
- turn: degrees to turn the view. Positive is right, negative is left.
  "turn around" is 180, "look left" is about -90, "a little right" is 30.
- pitch: degrees to look up (positive) or down (negative)
- stop: true only if they want EVERYTHING to stop

Movement keys: w forward, s back, a left, d right, space jump, shift sneak,
ctrl sprint, e inventory, q drop, 1-9 hotbar slots, f5 camera view.

Understand natural speech, not fixed phrases. Some examples:
"jump" -> tap space
"jump twice" -> tap space, say "jumping twice" (use tap ["space","space"])
"go forward for three seconds" -> hold w, hold_seconds 3
"walk a bit" -> hold w, hold_seconds 1
"run forward" -> hold w and ctrl
"stop walking" -> release ["w"], say "stopped walking"
"quit moving" / "freeze" -> stop true
"turn around" -> turn 180
"look behind me and mine" -> turn 180, click left, hold_seconds 2
"back up slowly" -> hold s, hold_seconds 2
"put a block down" -> click right
"open my inventory" -> tap e
"switch to slot 3" -> tap 3
"crouch" -> hold shift
"stand up" -> release ["shift"]
If you genuinely cannot make out the words, reply with everything empty,
say "" and stop false.
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


def turn_view(degrees, vertical=False):
    """Turn the camera by moving the mouse, in small steps so the game keeps up."""
    if not degrees:
        return
    total = int(degrees * PX_PER_DEGREE)
    steps = max(1, min(40, abs(total) // 20))
    per = total // steps
    for _ in range(steps):
        if vertical:
            ctypes.windll.user32.mouse_event(0x0001, 0, per, 0, 0)
        else:
            ctypes.windll.user32.mouse_event(0x0001, per, 0, 0, 0)
        time.sleep(0.01)


def do_action(a):
    global held_click
    if a.get("stop"):
        release_all()
        return

    for k in a.get("release") or []:
        if k in held_keys:
            pydirectinput.keyUp(k)
            held_keys.discard(k)

    turn_view(float(a.get("turn") or 0))
    turn_view(-float(a.get("pitch") or 0), vertical=True)

    for k in a.get("tap") or []:
        if k in ALLOWED_KEYS:
            # a real press-and-hold: games often miss an instant tap
            pydirectinput.keyDown(k)
            time.sleep(0.08)
            pydirectinput.keyUp(k)

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


_ps = None


def _speak_to_wav(text, path):
    """Render speech to a wav with the Windows voice, so it plays through the
    same output as the beep. One PowerShell stays running, otherwise every line
    would pay a few seconds of startup."""
    global _ps
    import subprocess
    if _ps is None or _ps.poll() is not None:
        _ps = subprocess.Popen(["powershell", "-NoProfile", "-Command", "-"],
                               stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                               stderr=subprocess.DEVNULL, text=True, bufsize=1)
        _ps.stdin.write("Add-Type -AssemblyName System.Speech\n"
                        "$v = New-Object System.Speech.Synthesis.SpeechSynthesizer\n"
                        "$v.Rate = 1\n'ready'\n")
        _ps.stdin.flush()
        _ps.stdout.readline()

    if not text:
        return
    safe = text.replace("'", "")
    _ps.stdin.write(f"$v.SetOutputToWaveFile('{path}')\n$v.Speak('{safe}')\n"
                    f"$v.SetOutputToNull()\n'spoken'\n")
    _ps.stdin.flush()
    _ps.stdout.readline()


def warm_tts():
    """Start the speech process up front so the first reply isn't slow."""
    try:
        _speak_to_wav("", "")
    except Exception:
        pass


def say(text, wait=True):
    """Speak a line out loud. Skips silently if speech isn't available."""
    if not text:
        return

    def run():
        import tempfile
        import wave
        path = os.path.join(tempfile.gettempdir(), "gestura_say.wav")
        try:
            with _tts_lock:
                _speak_to_wav(text, path)
                with wave.open(path, "rb") as w:
                    rate = w.getframerate()
                    pcm = np.frombuffer(w.readframes(w.getnframes()), dtype=np.int16)
                sd.play(pcm.astype(np.float32) / 32768.0, rate)
                sd.wait()
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
    audio = types.Part.from_bytes(data=wav_bytes(pcm), mime_type="audio/wav")
    last = None
    for model in MODELS:
        try:
            reply = client.models.generate_content(
                model=model,
                contents=[PROMPT, audio],
                config=types.GenerateContentConfig(
                    response_mime_type="application/json", temperature=0),
            )
            return json.loads(reply.text)
        except Exception as e:
            last = e
            if "RESOURCE_EXHAUSTED" in str(e) or "429" in str(e):
                print(f"  {model} out of quota, trying another model")
                continue
            raise
    raise last


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

    debug = "--debug" in sys.argv

    def on_press(k):
        if debug:
            print("key:", k, "vk:", getattr(k, "vk", None))
        if is_f13(k):
            if not busy.is_set():
                print("F13 down (pad held)")
            start_session()

    def on_release(k):
        if is_f13(k):
            talking.clear()      # you let go: wrap up the recording

    threading.Thread(target=warm_tts, daemon=True).start()

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
