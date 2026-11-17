"""Works out how each voice line moves the speaker's mouth, and writes it into the game's source.

    python3 Scripts/make_lipsync.py <folder with V_*.wav> [more folders...]

For every V_<name>.wav (the newest one, when several folders hold the same name) it finds when each
word is said with whisper.cpp (`whisper-cli`, word-level timestamps) and how loud the voice is from
moment to moment. The letters of each word become mouth shapes laid across the time the word is
voiced: an open jaw for "a", rounded lips for "o" and "oo", lips pressed shut for "m", "b" and "p",
the lower lip under the teeth for "f" and "v", and so on. Loudness scales how far the jaw opens and
closes the mouth in the gaps, so the shapes stay on the voice even where the spelling misleads.

The result is Source/HorrorGame/HGLipSyncData.inl: fifty frames a second, six channels, 0 to 100.
It is only mouth timings, no audio, so it can live in the public repo next to the subtitles.

Needs whisper-cli and a model (HG_WHISPER_MODEL, default ~/.cache/whisper/ggml-small.en.bin) and ffmpeg.
"""
import glob
import json
import math
import os
import struct
import subprocess
import sys
import tempfile
import wave

RATE = 50
MODEL = os.environ.get("HG_WHISPER_MODEL", os.path.expanduser("~/.cache/whisper/ggml-small.en.bin"))
OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "Source", "HorrorGame", "HGLipSyncData.inl")

# Channels: jaw open, funnel (o), purse (oo, w), lips together (m b p), wide (ee, s), lower lip bite (f v).
SHAPES = {
    "AA": (0.62, 0.0, 0.0, 0.0, 0.12, 0.0),
    "AH": (0.46, 0.0, 0.08, 0.0, 0.0, 0.0),
    "EH": (0.42, 0.0, 0.0, 0.0, 0.34, 0.0),
    "IH": (0.3, 0.0, 0.0, 0.0, 0.42, 0.0),
    "IY": (0.2, 0.0, 0.0, 0.0, 0.62, 0.0),
    "OH": (0.44, 0.7, 0.18, 0.0, 0.0, 0.0),
    "UW": (0.16, 0.38, 0.8, 0.0, 0.0, 0.0),
    "MBP": (0.0, 0.0, 0.0, 1.0, 0.0, 0.0),
    "FV": (0.1, 0.0, 0.0, 0.0, 0.15, 0.85),
    "TD": (0.2, 0.0, 0.0, 0.0, 0.22, 0.0),
    "S": (0.1, 0.0, 0.0, 0.0, 0.48, 0.0),
    "SH": (0.18, 0.52, 0.38, 0.0, 0.0, 0.0),
    "R": (0.2, 0.22, 0.42, 0.0, 0.0, 0.0),
    "KG": (0.3, 0.0, 0.0, 0.0, 0.14, 0.0),
}
VOWELS = {"AA", "AH", "EH", "IH", "IY", "OH", "UW", "R"}
PAIRS = {"th": ["TD"], "sh": ["SH"], "ch": ["SH"], "oo": ["UW"], "ee": ["IY"], "ea": ["IY"], "ou": ["AH", "UW"],
         "ow": ["OH", "UW"], "ph": ["FV"], "ng": ["KG"], "wh": ["UW"], "ck": ["KG"], "qu": ["KG", "UW"], "ai": ["EH"],
         "ay": ["EH", "IY"], "er": ["R"], "ur": ["R"], "ir": ["R"], "ll": ["TD"], "ss": ["S"], "oy": ["OH", "IY"],
         "ey": ["EH", "IY"], "ie": ["IY"], "ue": ["UW"], "oa": ["OH"], "au": ["AH"], "aw": ["AH"]}
SINGLE = {"a": "AA", "e": "EH", "i": "IH", "o": "OH", "u": "AH", "w": "UW", "m": "MBP", "b": "MBP", "p": "MBP",
          "f": "FV", "v": "FV", "t": "TD", "d": "TD", "n": "TD", "l": "TD", "s": "S", "z": "S", "c": "KG", "k": "KG",
          "q": "KG", "x": "KG", "g": "KG", "h": "KG", "j": "SH", "r": "R"}


def shapes_for(word):
    """Mouth shapes for a written word, in order."""
    letters = "".join(ch for ch in word.lower() if ch.isalpha())
    if len(letters) > 2 and letters.endswith("e") and letters[-2] not in "aeiou":
        letters = letters[:-1]  # a silent final e
    out, i = [], 0
    while i < len(letters):
        pair = letters[i:i + 2]
        if pair in PAIRS:
            out += PAIRS[pair]
            i += 2
            continue
        ch = letters[i]
        if ch == "y":
            out.append("IY" if i == len(letters) - 1 or i > 0 else "IH")
        elif ch == "c" and i + 1 < len(letters) and letters[i + 1] in "eiy":
            out.append("S")
        elif ch in SINGLE:
            out.append(SINGLE[ch])
        i += 1
    # The same shape twice in a row reads as one held a little longer.
    merged = []
    for shape in out:
        if merged and merged[-1][0] == shape:
            merged[-1][1] += 0.6
        else:
            merged.append([shape, 1.6 if shape in VOWELS else 1.0])
    return merged


def loudness(path):
    """RMS per frame, normalised so the loud parts of the line sit near 1."""
    with wave.open(path) as w:
        rate, width, channels, count = w.getframerate(), w.getsampwidth(), w.getnchannels(), w.getnframes()
        raw = w.readframes(count)
    if width != 2:
        raise SystemExit(f"{path}: expected 16-bit audio")
    samples = struct.unpack("<%dh" % (len(raw) // 2), raw)[::channels]
    step = rate // RATE
    env = []
    for start in range(0, len(samples), step):
        chunk = samples[start:start + step]
        env.append(math.sqrt(sum(s * s for s in chunk) / max(1, len(chunk))) / 32768.0)
    ranked = sorted(env)
    top = ranked[int(len(ranked) * 0.95)] if ranked else 1.0
    return [min(1.2, e / max(top, 1e-4)) for e in env]


def words(path, scratch):
    """(start, end, text) for each word said, from whisper.cpp."""
    small = os.path.join(scratch, "in16.wav")
    subprocess.run(["ffmpeg", "-loglevel", "error", "-y", "-i", path, "-ar", "16000", "-ac", "1", small], check=True)
    base = os.path.join(scratch, "words")
    subprocess.run(["whisper-cli", "-m", MODEL, "-f", small, "-ml", "1", "-sow", "-oj", "-of", base, "-np"],
                   check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    out = []
    for seg in json.load(open(base + ".json"))["transcription"]:
        text = seg["text"].strip()
        if any(ch.isalpha() for ch in text):
            out.append((seg["offsets"]["from"] / 1000.0, seg["offsets"]["to"] / 1000.0, text))
    return out


def track(path, scratch):
    env = loudness(path)
    frames = [[0.0] * 6 for _ in env]
    said = words(path, scratch)
    for start, end, text in said:
        first, last = int(start * RATE), min(len(env) - 1, int(math.ceil(end * RATE)))
        # Whisper's word edges are loose: keep to the part of the span that is actually voiced.
        voiced = [i for i in range(max(0, first - 3), last + 1) if env[i] > 0.1]
        if not voiced:
            continue
        a, b = voiced[0], voiced[-1] + 1
        plan = shapes_for(text)
        total = sum(w for _, w in plan) or 1.0
        at = float(a)
        for shape, weight in plan:
            span = (b - a) * weight / total
            for i in range(int(at), min(len(env), int(math.ceil(at + span)))):
                frames[i] = list(SHAPES[shape])
            at += span
    # Loudness opens the jaw further on stressed sounds and lets the mouth rest in the gaps.
    for i, e in enumerate(env):
        gain = 0.0 if e < 0.06 else min(1.15, 0.4 + 0.85 * e)
        f = frames[i]
        frames[i] = [f[0] * gain, f[1] * min(1.0, gain + 0.2), f[2] * min(1.0, gain + 0.2), f[3], f[4] * min(1.0, gain + 0.2), f[5]]
    # A mouth moves on to the next shape before the last one is done: blur each channel a little.
    kernel = [math.exp(-(k * k) / (2 * 1.3 * 1.3)) for k in range(-3, 4)]
    norm = sum(kernel)
    smooth = []
    for i in range(len(frames)):
        row = []
        for c in range(6):
            v = sum(kernel[k + 3] * frames[min(len(frames) - 1, max(0, i + k))][c] for k in range(-3, 4)) / norm
            if c == 3:
                v = max(v, frames[i][c] * 0.9)  # the lips must still meet on m, b and p
            row.append(int(round(max(0.0, min(1.0, v)) * 100)))
        smooth.append(row)
    while smooth and not any(smooth[-1]):
        smooth.pop()
    return smooth, said


def main():
    if len(sys.argv) < 2:
        raise SystemExit(__doc__)
    newest = {}
    for folder in sys.argv[1:]:
        for path in glob.glob(os.path.join(folder, "V_*.wav")):
            name = os.path.basename(path)[2:-4]
            if name not in newest or os.path.getmtime(path) > os.path.getmtime(newest[name]):
                newest[name] = path
    lines = ["// Generated by Scripts/make_lipsync.py: mouth shapes for each voice line. Do not edit by hand.",
             "// Each line: name, frame count, then frames of six channels (jaw, funnel, purse, together, wide, bite), 0 to 100, at 50 a second.",
             ""]
    table = []
    with tempfile.TemporaryDirectory() as scratch:
        for name in sorted(newest):
            frames, said = track(newest[name], scratch)
            print(f"{name}: {len(frames)} frames, words: {' '.join(w for _, _, w in said)}")
            values = ", ".join(str(v) for row in frames for v in row) or "0"
            lines.append(f"static const uint8 HGLip_{name}[] = {{ {values} }};")
            table.append(f"\t{{ TEXT(\"{name}\"), {len(frames)}, HGLip_{name} }},")
    lines += ["", "static const FHGLipLine HGLipLines[] = {"] + table + ["};", ""]
    with open(OUT, "w") as f:
        f.write("\n".join(lines))
    print("wrote", os.path.relpath(OUT), len(table), "lines")


if __name__ == "__main__":
    main()
