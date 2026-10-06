#!/usr/bin/env python3
"""Build an original, soft two-note chime and the app's bell icons."""
from pathlib import Path
import math
import struct
import wave
from PIL import Image, ImageDraw

HERE = Path(__file__).resolve().parent
ASSETS = HERE / "assets"
ASSETS.mkdir(exist_ok=True)
RATE = 44100
duration = 2.25
samples = []
for i in range(round(RATE * duration)):
    t = i / RATE
    value = 0.0
    for onset, frequency, amplitude in ((0.04, 659.255, 0.52), (0.28, 987.767, 0.42)):
        age = t - onset
        if age < 0:
            continue
        attack = 1 - math.exp(-age / 0.007)
        for ratio, weight, decay in ((1, 1, 0.63), (2, 0.20, 0.30), (2.76, 0.07, 0.21), (4.12, 0.035, 0.11)):
            value += amplitude * attack * weight * math.exp(-age / decay) * math.sin(2 * math.pi * frequency * ratio * age)
    # A quiet early reflection gives a small resonant room, rather than a beep.
    reflected = samples[i - round(RATE * 0.073)] if i >= round(RATE * 0.073) else 0
    samples.append(value + reflected * 0.09)
peak = max(abs(x) for x in samples)
pcm = bytearray()
for i, value in enumerate(samples):
    # Fade the last 120 ms fully to silence, with no abrupt wave truncation.
    remaining = duration - i / RATE
    fade = min(1.0, max(0.0, remaining / 0.12))
    pcm.extend(struct.pack("<h", round(value / peak * 26500 * fade)))
with wave.open(str(ASSETS / "fantasy-chime.wav"), "wb") as output:
    output.setnchannels(1)
    output.setsampwidth(2)
    output.setframerate(RATE)
    output.writeframes(pcm)

def bell_icon(name, fill, border):
    size = 256
    img = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    draw = ImageDraw.Draw(img)
    draw.rounded_rectangle((6, 6, 250, 250), radius=54, fill=border)
    draw.ellipse((111, 48, 145, 83), fill=fill)
    draw.polygon([(72, 179), (82, 156), (86, 109), (94, 87), (111, 77),
                  (145, 77), (162, 87), (170, 109), (174, 156), (184, 179)], fill=fill)
    draw.rounded_rectangle((67, 173, 189, 191), radius=7, fill=fill)
    draw.ellipse((112, 194, 144, 216), fill=fill)
    # Two short glints suggest a little enchanted brass, not a system warning.
    draw.line((185, 87, 199, 79), fill=fill, width=8)
    draw.line((189, 110, 208, 110), fill=fill, width=8)
    img.save(ASSETS / name, sizes=[(16, 16), (24, 24), (32, 32), (48, 48), (64, 64), (256, 256)])

bell_icon("bell-on.ico", (233, 209, 145, 255), (33, 91, 66, 255))
bell_icon("bell-off.ico", (193, 186, 205, 255), (57, 51, 69, 255))
print("Created original chime and two bell icons.")
