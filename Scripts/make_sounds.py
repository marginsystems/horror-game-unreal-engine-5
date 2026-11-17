#!/usr/bin/env python3
"""Synthesises every sound in the game and writes them as WAV files.

Nothing is recorded or downloaded: each sound is built here from sine waves, noise and
filters, so the repo ships no third-party audio. Standard library only.

    python3 Scripts/make_sounds.py <output folder>

Scripts/make-assets.sh runs this and then imports the results into Content/Audio.
The list of names at the bottom must match HG::SoundNames in the C++ code.
"""
import math
import os
import random
import struct
import sys
import wave

RATE = 44100
TAU = 2.0 * math.pi


# ------------------------------------------------------------------------------------------
# Building blocks. A sound is a plain list of floats.

def silence(seconds):
    return [0.0] * int(seconds * RATE)


def tone(seconds, freq, shape="sine", vibrato=0.0, vibrato_hz=6.0):
    """freq is a number or a function of time (0..1 through the sound) returning Hz."""
    n = int(seconds * RATE)
    out = [0.0] * n
    phase = 0.0
    for i in range(n):
        u = i / n
        f = freq(u) if callable(freq) else freq
        if vibrato:
            f *= 1.0 + vibrato * math.sin(TAU * vibrato_hz * i / RATE)
        phase += f / RATE
        p = phase % 1.0
        if shape == "sine":
            out[i] = math.sin(TAU * p)
        elif shape == "saw":
            out[i] = 2.0 * p - 1.0
        elif shape == "square":
            out[i] = 1.0 if p < 0.5 else -1.0
        elif shape == "tri":
            out[i] = 4.0 * abs(p - 0.5) - 1.0
    return out


def noise(seconds, rng):
    return [rng.uniform(-1.0, 1.0) for _ in range(int(seconds * RATE))]


def lowpass(x, cutoff):
    """One pole. cutoff is Hz or a function of time (0..1)."""
    out = [0.0] * len(x)
    y = 0.0
    n = len(x)
    for i in range(n):
        c = cutoff(i / n) if callable(cutoff) else cutoff
        a = 1.0 - math.exp(-TAU * c / RATE)
        y += a * (x[i] - y)
        out[i] = y
    return out


def highpass(x, cutoff):
    low = lowpass(x, cutoff)
    return [a - b for a, b in zip(x, low)]


def bandpass(x, centre, q=4.0):
    """Two pole resonator. centre is Hz or a function of time (0..1)."""
    out = [0.0] * len(x)
    y1 = y2 = x1 = x2 = 0.0
    n = len(x)
    for i in range(n):
        c = centre(i / n) if callable(centre) else centre
        w = TAU * min(c, RATE * 0.45) / RATE
        alpha = math.sin(w) / (2.0 * q)
        b0 = alpha
        a0 = 1.0 + alpha
        a1 = -2.0 * math.cos(w)
        a2 = 1.0 - alpha
        y = (b0 * x[i] - b0 * x2 - a1 * y1 - a2 * y2) / a0
        x2, x1 = x1, x[i]
        y2, y1 = y1, y
        out[i] = y
    return out


def envelope(x, attack=0.005, release=0.05, curve=1.0, hold=None):
    """Fades in over attack seconds, then decays to nothing by the end.
    With hold (seconds) it stays at full level that long before the release."""
    n = len(x)
    a = max(1, int(attack * RATE))
    out = [0.0] * n
    if hold is None:
        for i in range(n):
            if i < a:
                g = i / a
            else:
                g = (1.0 - (i - a) / max(1, n - a)) ** curve
            out[i] = x[i] * g
        return out
    h = int(hold * RATE)
    r = max(1, int(release * RATE))
    for i in range(n):
        if i < a:
            g = i / a
        elif i < a + h:
            g = 1.0
        else:
            g = max(0.0, 1.0 - (i - a - h) / r) ** curve
        out[i] = x[i] * g
    return out


def decay(x, rate):
    """Exponential decay: rate is how many times it halves per second, roughly."""
    return [v * math.exp(-rate * i / RATE) for i, v in enumerate(x)]


def gain(x, g):
    return [v * g for v in x]


def mix(*parts):
    """Each part is a sound or (sound, start seconds)."""
    placed = [(p, 0.0) if isinstance(p, list) else p for p in parts]
    n = max(len(p) + int(start * RATE) for p, start in placed)
    out = [0.0] * n
    for p, start in placed:
        at = int(start * RATE)
        for i, v in enumerate(p):
            out[at + i] += v
    return out


def join(*parts):
    out = []
    for p in parts:
        out.extend(p)
    return out


def drive(x, amount):
    """Soft clipping, for anything that should sound overloaded."""
    return [math.tanh(v * amount) for v in x]


def normalise(x, peak=0.9):
    top = max(1e-9, max(abs(v) for v in x))
    return [v * peak / top for v in x]


def seamless(x, fade=0.5):
    """Crossfades the tail into the head so the sound loops without a click."""
    f = int(fade * RATE)
    body = x[:-f]
    tail = x[-f:]
    for i in range(f):
        t = i / f
        body[i] = body[i] * t + tail[i] * (1.0 - t)
    return body


def brown(seconds, rng):
    out = []
    y = 0.0
    for _ in range(int(seconds * RATE)):
        y = 0.995 * y + rng.uniform(-1.0, 1.0) * 0.08
        out.append(y)
    return out


# ------------------------------------------------------------------------------------------
# The sounds.

def thump(rng, pitch=95.0, seconds=0.16, grit=0.5):
    body = decay(tone(seconds, lambda u: pitch * (1.0 - 0.45 * u)), 26.0)
    scuff = decay(lowpass(noise(seconds, rng), 1400.0), 38.0)
    return envelope(mix(body, gain(scuff, grit)), 0.002)


def click(rng, bright=4200.0, seconds=0.03):
    return decay(bandpass(noise(seconds, rng), bright, 2.0), 160.0)


def step(rng, pitch):
    return normalise(mix(thump(rng, pitch, 0.17, 0.7), (gain(click(rng, 2200.0), 0.25), 0.012)), 0.8)


def pistol(rng):
    # A click, then the spring that pushes the flag out.
    boing = decay(tone(0.6, lambda u: 190.0 + 240.0 * u, "tri", vibrato=0.22, vibrato_hz=17.0), 5.5)
    return normalise(mix(gain(click(rng, 3000.0, 0.05), 1.4), (gain(click(rng, 5200.0), 1.0), 0.05), (boing, 0.07)))


def pistol_back(rng):
    # Winding the flag back in: a ratchet.
    parts = []
    for i in range(7):
        parts.append((gain(click(rng, 2600.0 + 220.0 * i, 0.04), 0.7 + 0.04 * i), 0.055 * i))
    parts.append((gain(thump(rng, 160.0, 0.08, 0.3), 0.8), 0.42))
    return normalise(mix(*parts), 0.8)


def party_horn(seconds, start_hz, end_hz):
    reed = tone(seconds, lambda u: start_hz + (end_hz - start_hz) * u, "square", vibrato=0.03, vibrato_hz=28.0)
    reed = bandpass(reed, 1500.0, 1.2)
    return envelope(reed, 0.02, 0.08, 1.0, hold=seconds - 0.12)


def shotgun(rng):
    pop = decay(lowpass(noise(0.3, rng), lambda u: 5000.0 * (1.0 - u) + 200.0), 16.0)
    thud = decay(tone(0.3, lambda u: 150.0 - 90.0 * u), 14.0)
    horn = party_horn(0.55, 520.0, 610.0)
    flutter = decay(highpass(noise(0.9, rng), 3500.0), 4.0)
    return normalise(mix(gain(pop, 1.6), thud, (gain(horn, 0.9), 0.16), (gain(flutter, 0.22), 0.1)))


def chicken(rng):
    # Squeezed air through a reed: up fast, then a long wheezing fall.
    def pitch(u):
        if u < 0.12:
            return 700.0 + 5200.0 * u
        return 1320.0 - 760.0 * ((u - 0.12) / 0.88) ** 0.7
    raw = tone(0.62, pitch, "saw", vibrato=0.035, vibrato_hz=41.0)
    voiced = mix(bandpass(raw, lambda u: pitch(u) * 2.1, 3.0), gain(bandpass(raw, 3100.0, 2.0), 0.6))
    air = gain(highpass(noise(0.62, rng), 2500.0), 0.08)
    return normalise(envelope(mix(voiced, air), 0.012, 0.1, 0.7))


def pin(rng):
    ting = mix(decay(tone(0.5, 3140.0), 9.0), gain(decay(tone(0.5, 5210.0), 13.0), 0.6), gain(decay(tone(0.5, 7900.0), 18.0), 0.3))
    return normalise(mix(gain(click(rng, 6000.0), 0.8), (ting, 0.01)), 0.7)


def boom(rng):
    blast = lowpass(noise(2.2, rng), lambda u: 3200.0 * (1.0 - u) ** 3 + 60.0)
    blast = decay(drive(gain(blast, 3.0), 1.4), 2.6)
    sub = decay(tone(2.2, lambda u: 62.0 - 30.0 * u), 2.2)
    crack = decay(highpass(noise(0.12, rng), 1800.0), 40.0)
    return normalise(mix(blast, gain(sub, 1.2), gain(crack, 0.8)), 0.97)


def light_off(rng):
    fizz = bandpass(noise(0.5, rng), lambda u: 2600.0 - 2100.0 * u, 3.0)
    buzz = tone(0.5, lambda u: 120.0 - 70.0 * u, "saw")
    return normalise(envelope(mix(fizz, gain(buzz, 0.35)), 0.004, 0.1, 1.6), 0.7)


def light_on(rng):
    hum = envelope(tone(0.35, lambda u: 180.0 + 260.0 * u, "tri"), 0.05, 0.1, 1.0)
    return normalise(mix(gain(click(rng, 3600.0), 1.2), (gain(hum, 0.35), 0.02)), 0.7)


def smack(rng):
    slap = decay(bandpass(noise(0.14, rng), 1300.0, 0.8), 34.0)
    rattle = mix(*[(gain(click(rng, 4800.0 + 300.0 * i), 0.35), 0.03 + 0.021 * i) for i in range(4)])
    return normalise(mix(gain(slap, 1.4), gain(thump(rng, 190.0, 0.12, 0.2), 0.8), rattle), 0.85)


def blip(notes, seconds=0.09, shape="tri", tail=0.25):
    parts = []
    for i, hz in enumerate(notes):
        last = i == len(notes) - 1
        note = decay(tone(seconds + (tail if last else 0.0), hz, shape), 14.0 if not last else 7.0)
        parts.append((envelope(note, 0.003), seconds * i))
    return mix(*parts)


def pickup(rng):
    return normalise(blip([660.0, 990.0]), 0.6)


def fuse(rng):
    zap = decay(bandpass(drive(noise(0.16, rng), 3.0), lambda u: 3800.0 - 2600.0 * u, 2.0), 20.0)
    chime = blip([784.0, 988.0, 1319.0], 0.085, "sine", 0.5)
    return normalise(mix(gain(zap, 0.7), (chime, 0.06)), 0.75)


def power(rng):
    # The relay drops in, the motor winds up, something somewhere goes ding.
    clunk = mix(gain(thump(rng, 70.0, 0.3, 0.9), 1.4), (gain(click(rng, 1800.0, 0.06), 0.9), 0.0))
    motor = tone(1.5, lambda u: 45.0 + 75.0 * u ** 0.6, "saw")
    motor = envelope(lowpass(motor, 700.0), 0.3, 0.4, 1.0, hold=0.8)
    rattle = envelope(bandpass(noise(1.5, rng), 900.0, 1.0), 0.3, 0.4, 1.0, hold=0.8)
    ding = mix(decay(tone(1.2, 1568.0), 4.0), gain(decay(tone(1.2, 3136.0), 7.0), 0.3))
    return normalise(mix(clunk, (gain(motor, 0.6), 0.15), (gain(rattle, 0.18), 0.15), (gain(ding, 0.6), 1.45)), 0.9)


def caught(rng):
    # Every wrong note at once, and a door slamming on it.
    cluster = mix(*[tone(1.7, hz, "saw", vibrato=0.012, vibrato_hz=5.0 + i) for i, hz in enumerate([196.0, 208.0, 277.0, 294.0, 415.0, 440.0, 831.0])])
    cluster = envelope(lowpass(cluster, lambda u: 6000.0 * (1.0 - u) + 500.0), 0.004, 0.2, 1.4)
    crash = decay(lowpass(noise(1.2, rng), 5000.0), 5.0)
    sub = decay(tone(1.2, lambda u: 70.0 - 35.0 * u), 3.5)
    return normalise(mix(gain(cluster, 0.5), gain(crash, 1.5), gain(sub, 2.2)), 0.97)


def win(rng):
    # The sort of jingle a lift plays when it reaches the lobby.
    tune = [(523.25, 0.0), (659.25, 0.13), (783.99, 0.26), (1046.5, 0.39), (783.99, 0.56), (1046.5, 0.69)]
    parts = []
    for hz, at in tune:
        last = at == tune[-1][1]
        note = mix(tone(0.9 if last else 0.2, hz, "tri"), gain(tone(0.9 if last else 0.2, hz * 2.0, "sine"), 0.3))
        parts.append((envelope(decay(note, 3.0 if last else 9.0), 0.004), at))
    bass = envelope(decay(tone(1.4, 130.81, "tri"), 2.0), 0.01)
    return normalise(mix(gain(bass, 0.7), *parts), 0.75)


def scream(rng):
    # A voice-ish thing: one reed, two vowel resonances, going up and then giving out.
    def pitch(u):
        return 330.0 + 380.0 * math.sin(math.pi * min(1.0, u * 1.25)) ** 0.8
    raw = tone(1.05, pitch, "saw", vibrato=0.05, vibrato_hz=9.0)
    vowel = mix(bandpass(raw, 850.0, 4.0), gain(bandpass(raw, 1250.0, 5.0), 0.8), gain(bandpass(raw, 2800.0, 6.0), 0.35))
    breath = gain(bandpass(noise(1.05, rng), 2200.0, 1.0), 0.1)
    return normalise(envelope(drive(gain(mix(vowel, breath), 3.0), 1.2), 0.03, 0.2, 0.8), 0.9)


def heart(rng):
    lub = decay(tone(0.16, lambda u: 68.0 - 22.0 * u), 26.0)
    dub = decay(tone(0.14, lambda u: 58.0 - 18.0 * u), 30.0)
    return normalise(lowpass(mix(lub, (gain(dub, 0.75), 0.21), silence(0.5)), 220.0), 0.9)


def blink(rng):
    return normalise(envelope(lowpass(noise(0.3, rng), lambda u: 300.0 + 900.0 * math.sin(math.pi * u)), 0.1, 0.1, 1.0), 0.35)


def question(rng):
    # "Hm?"
    hm = tone(0.34, lambda u: 150.0 + 110.0 * u ** 2.2, "saw", vibrato=0.02, vibrato_hz=7.0)
    hm = mix(bandpass(hm, 420.0, 3.0), gain(bandpass(hm, 1100.0, 4.0), 0.4))
    return normalise(envelope(hm, 0.04, 0.08, 0.7), 0.7)


def gary_step(rng):
    # Hollow plastic on a hard floor.
    knock = decay(bandpass(noise(0.12, rng), 760.0, 6.0), 36.0)
    shell = decay(tone(0.12, 410.0), 44.0)
    return normalise(mix(gain(knock, 2.0), gain(shell, 0.5), gain(click(rng, 3000.0), 0.3)), 0.85)


def gary_freeze(rng):
    # Joints locking: two sharp clacks and a creak that stops dead.
    creak = tone(0.16, lambda u: 520.0 + 300.0 * u, "saw", vibrato=0.2, vibrato_hz=55.0)
    creak = envelope(bandpass(creak, 1700.0, 5.0), 0.01, 0.01, 0.3)
    return normalise(mix(gain(creak, 0.5), (gain(gary_step(rng), 1.0), 0.15), (gain(click(rng, 2400.0, 0.05), 0.9), 0.21)), 0.85)


def shush(rng):
    hiss = bandpass(noise(1.0, rng), lambda u: 3200.0 + 1600.0 * u, 0.9)
    hiss = mix(hiss, gain(highpass(noise(1.0, rng), 5000.0), 0.4))
    return normalise(envelope(hiss, 0.06, 0.3, 1.0, hold=0.5), 0.85)


def heels(rng):
    tip = decay(bandpass(noise(0.05, rng), 2900.0, 4.0), 90.0)
    heel = decay(tone(0.07, 240.0), 60.0)
    return normalise(mix(gain(tip, 2.0), (gain(heel, 0.7), 0.0)), 0.8)


def squelch(rng):
    wet = bandpass(noise(0.26, rng), lambda u: 500.0 + 1300.0 * math.sin(math.pi * u), 2.5)
    return normalise(mix(envelope(wet, 0.03, 0.1, 1.2), gain(thump(rng, 60.0, 0.22, 0.2), 1.3)), 0.85)


def mouth_loop(rng):
    # A wet idle growl. Loops.
    seconds = 4.0
    throat = tone(seconds + 0.5, lambda u: 47.0 + 5.0 * math.sin(TAU * 3.0 * u), "saw")
    n = len(throat)
    throat = [v * (0.55 + 0.45 * math.sin(TAU * 23.0 * i / RATE)) for i, v in enumerate(throat)]
    throat = lowpass(drive(gain(throat, 2.5), 1.5), 520.0)
    spit = bandpass(noise(seconds + 0.5, rng), 1700.0, 2.0)
    spit = [v * max(0.0, math.sin(TAU * 1.5 * i / RATE)) ** 6 for i, v in enumerate(spit)]
    return normalise(seamless(mix(throat, gain(spit, 0.25)), 0.5), 0.8)


def mouth_roar(rng):
    raw = tone(1.5, lambda u: 70.0 + 150.0 * u ** 1.6, "saw", vibrato=0.06, vibrato_hz=31.0)
    raw = [v * (0.6 + 0.4 * math.sin(TAU * 37.0 * i / RATE)) for i, v in enumerate(raw)]
    voiced = mix(lowpass(raw, 900.0), gain(bandpass(raw, lambda u: 600.0 + 900.0 * u, 2.0), 0.9))
    air = bandpass(noise(1.5, rng), lambda u: 900.0 + 1400.0 * u, 1.0)
    return normalise(envelope(drive(gain(mix(voiced, gain(air, 0.5)), 3.0), 1.3), 0.25, 0.2, 0.6), 0.95)


def bonk(rng):
    # Face, meet wall. Then the wobble.
    donk = mix(decay(tone(0.35, lambda u: 235.0 - 40.0 * u), 15.0), gain(decay(tone(0.35, 705.0), 28.0), 0.4))
    hit = decay(lowpass(noise(0.2, rng), 900.0), 26.0)
    wobble = decay(tone(0.9, lambda u: 150.0 + 60.0 * math.sin(TAU * 7.0 * u), "tri", vibrato=0.25, vibrato_hz=12.0), 4.0)
    return normalise(mix(gain(donk, 1.2), gain(hit, 1.4), (gain(wobble, 0.5), 0.12)), 0.95)


def amb_clinic(rng):
    # Mains hum, a tube that will not quite light, and a ventilator somewhere down the hall.
    seconds = 12.5
    hum = mix(tone(seconds, 50.0), gain(tone(seconds, 100.0), 0.5), gain(tone(seconds, 150.0), 0.2))
    tube = bandpass(tone(seconds, 100.0, "saw"), 3300.0, 6.0)
    tube = [v * (0.6 + 0.4 * math.sin(TAU * 0.37 * i / RATE) * math.sin(TAU * 7.3 * i / RATE)) for i, v in enumerate(tube)]
    room = lowpass(brown(seconds, rng), 240.0)
    breath = bandpass(noise(seconds, rng), 420.0, 1.2)
    breath = [v * max(0.0, math.sin(TAU * (1.0 / 4.0) * i / RATE)) ** 2 for i, v in enumerate(breath)]
    beep = silence(seconds)
    for k in range(6):
        note = envelope(tone(0.09, 1760.0), 0.005, 0.02, 1.0, hold=0.06)
        at = int((0.9 + 2.0 * k) * RATE)
        for i, v in enumerate(note):
            beep[at + i] += v
    return normalise(seamless(mix(gain(hum, 0.5), gain(tube, 0.5), gain(room, 1.4), gain(breath, 0.12), gain(beep, 0.035)), 0.5), 0.55)


def amb_office(rng):
    # Air conditioning, a wall clock, and one computer nobody switched off.
    seconds = 12.5
    air = lowpass(highpass(noise(seconds, rng), 120.0), 900.0)
    air = [v * (0.85 + 0.15 * math.sin(TAU * 0.13 * i / RATE)) for i, v in enumerate(air)]
    fan = mix(tone(seconds, 118.0, "tri"), gain(tone(seconds, 236.5, "sine"), 0.4))
    room = lowpass(brown(seconds, rng), 160.0)
    clock = silence(seconds)
    for k in range(12):
        tick = click(rng, 2100.0 if k % 2 == 0 else 1700.0, 0.04)
        at = int((0.3 + 1.0 * k) * RATE)
        for i, v in enumerate(tick):
            clock[at + i] += v
    return normalise(seamless(mix(gain(air, 0.35), gain(fan, 0.12), gain(room, 1.2), gain(clock, 0.5)), 0.5), 0.5)


def amb_mine(rng):
    # Wind through the shaft, timber settling, water finding its way down.
    seconds = 14.5
    wind = bandpass(noise(seconds, rng), lambda u: 260.0 + 140.0 * math.sin(TAU * 2.0 * u), 1.5)
    wind = [v * (0.55 + 0.45 * math.sin(TAU * 0.11 * i / RATE + 1.0)) for i, v in enumerate(wind)]
    rumble = lowpass(brown(seconds, rng), 90.0)
    drips = silence(seconds)
    at = 0.7
    while at < seconds - 1.2:
        hz = rng.uniform(900.0, 1700.0)
        drop = decay(tone(0.18, lambda u, hz=hz: hz * (1.0 + 0.9 * u)), 30.0)
        echo = gain(drop, 0.3)
        start = int(at * RATE)
        for i, v in enumerate(drop):
            drips[start + i] += v
            drips[start + i + int(0.11 * RATE)] += echo[i]
        at += rng.uniform(0.9, 2.6)
    creak = silence(seconds)
    groan = tone(0.9, lambda u: 92.0 + 30.0 * math.sin(TAU * 1.5 * u), "saw", vibrato=0.12, vibrato_hz=23.0)
    groan = envelope(bandpass(groan, 520.0, 5.0), 0.2, 0.3, 1.0, hold=0.3)
    start = int(6.2 * RATE)
    for i, v in enumerate(groan):
        creak[start + i] += v
    return normalise(seamless(mix(gain(wind, 0.5), gain(rumble, 2.2), gain(drips, 0.22), gain(creak, 0.16)), 0.5), 0.55)


SOUNDS = {
    "step_1": lambda r: step(r, 92.0),
    "step_2": lambda r: step(r, 104.0),
    "step_3": lambda r: step(r, 84.0),
    "pistol": pistol,
    "pistol_back": pistol_back,
    "shotgun": shotgun,
    "chicken": chicken,
    "pin": pin,
    "boom": boom,
    "light_off": light_off,
    "light_on": light_on,
    "smack": smack,
    "pickup": pickup,
    "fuse": fuse,
    "power": power,
    "caught": caught,
    "win": win,
    "scream": scream,
    "heart": heart,
    "blink": blink,
    "question": question,
    "gary_step": gary_step,
    "gary_freeze": gary_freeze,
    "shush": shush,
    "heels": heels,
    "squelch": squelch,
    "mouth_loop": mouth_loop,
    "mouth_roar": mouth_roar,
    "bonk": bonk,
    "amb_clinic": amb_clinic,
    "amb_office": amb_office,
    "amb_mine": amb_mine,
}


def write(path, samples):
    with wave.open(path, "wb") as out:
        out.setnchannels(1)
        out.setsampwidth(2)
        out.setframerate(RATE)
        out.writeframes(b"".join(struct.pack("<h", int(max(-1.0, min(1.0, v)) * 32767)) for v in samples))


def main():
    if len(sys.argv) != 2:
        sys.exit("usage: make_sounds.py <output folder>")
    folder = sys.argv[1]
    os.makedirs(folder, exist_ok=True)
    for index, (name, build) in enumerate(SOUNDS.items()):
        # A fixed seed per sound: running this twice gives the same files.
        samples = build(random.Random(7919 * (index + 1)))
        write(os.path.join(folder, f"S_{name}.wav"), samples)
        print(f"{name}: {len(samples) / RATE:.2f} s")


if __name__ == "__main__":
    main()
