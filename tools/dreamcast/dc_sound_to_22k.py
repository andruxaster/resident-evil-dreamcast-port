#!/usr/bin/env python3
"""dc_sound_to_22k.py <data-root> [--dry-run]

Converts every 44100 Hz PCM WAV under <data-root> (USA/, DC/, ...) to 22050 Hz.
The Dreamcast mixer runs at 22050 Hz, so half of a 44.1 kHz file's samples are
thrown away at playback anyway - converting halves its RAM, disc and file-cache
cost with no audible difference.

- 2:1 decimation behind a 63-tap windowed-sinc low-pass (cut-off ~10 kHz), so
  nothing above the new Nyquist folds back as aliasing.
- 8- and 16-bit PCM, any channel count. Other formats are left alone.
- Loop points ('smpl' chunk) and cue positions ('cue ' chunk) are halved so
  looping sounds stay seamless; other chunks are copied unchanged.
- Originals are moved to <data-root>/../dc_sound_44k/ (same relative path).
"""
import math
import os
import shutil
import struct
import sys

TAPS = 63


def make_filter():
    # Low-pass at 0.45 x the new sample rate (relative to the old rate: 0.225),
    # Blackman window, unity DC gain.
    fc = 0.225
    h = []
    m = TAPS - 1
    for n in range(TAPS):
        x = n - m / 2
        s = 2 * fc if x == 0 else math.sin(2 * math.pi * fc * x) / (math.pi * x)
        w = 0.42 - 0.5 * math.cos(2 * math.pi * n / m) + 0.08 * math.cos(4 * math.pi * n / m)
        h.append(s * w)
    g = sum(h)
    return [v / g for v in h]


FILT = make_filter()


def read_chunks(data):
    if data[:4] != b'RIFF' or data[8:12] != b'WAVE':
        return None
    chunks, pos = [], 12
    while pos + 8 <= len(data):
        cid = data[pos:pos + 4]
        size = struct.unpack_from('<I', data, pos + 4)[0]
        body = data[pos + 8:pos + 8 + size]
        chunks.append([cid, bytearray(body)])
        pos += 8 + size + (size & 1)
    return chunks


def decimate(samples, ch):
    # samples: list of ints, interleaved. Returns interleaved, half the frames.
    frames = len(samples) // ch
    out = []
    half = TAPS // 2
    for c in range(ch):
        src = samples[c::ch]
        res = []
        for i in range(0, frames, 2):
            acc = 0.0
            for k in range(TAPS):
                j = i + k - half
                if 0 <= j < frames:
                    acc += FILT[k] * src[j]
            res.append(acc)
        out.append(res)
    n = len(out[0])
    return [out[c][i] for i in range(n) for c in range(ch)]


def convert(path, dry):
    with open(path, 'rb') as f:
        data = f.read()
    chunks = read_chunks(data)
    if chunks is None:
        return False
    fmt = next((c for c in chunks if c[0] == b'fmt '), None)
    dat = next((c for c in chunks if c[0] == b'data'), None)
    if fmt is None or dat is None:
        return False
    tag, ch, rate, _, align, bits = struct.unpack_from('<HHIIHH', fmt[1], 0)
    if tag != 1 or rate != 44100 or bits not in (8, 16):
        return False
    print(f"  {path}: {rate} Hz {ch} ch {bits}-bit, {len(dat[1]) // 1024} KB", end='')
    if dry:
        print(" (would convert)")
        return True
    raw = bytes(dat[1])
    if bits == 16:
        n = len(raw) // 2
        s = list(struct.unpack('<%dh' % n, raw[:n * 2]))
    else:
        s = [b - 128 for b in raw]
    y = decimate(s, ch)
    if bits == 16:
        y = [max(-32768, min(32767, int(round(v)))) for v in y]
        newraw = struct.pack('<%dh' % len(y), *y)
    else:
        y = [max(-128, min(127, int(round(v)))) for v in y]
        newraw = bytes(v + 128 for v in y)
    dat[1] = bytearray(newraw)
    struct.pack_into('<I', fmt[1], 4, 22050)                    # sample rate
    struct.pack_into('<I', fmt[1], 8, 22050 * align)            # byte rate
    for c in chunks:                                            # loop / cue positions
        if c[0] == b'smpl' and len(c[1]) >= 36:
            struct.pack_into('<I', c[1], 8, struct.unpack_from('<I', c[1], 8)[0] * 2)  # ns/sample
            nloops = struct.unpack_from('<I', c[1], 28)[0]
            for i in range(nloops):
                off = 36 + i * 24
                if off + 16 <= len(c[1]):
                    st, en = struct.unpack_from('<II', c[1], off + 8)
                    struct.pack_into('<II', c[1], off + 8, st // 2, en // 2)
        elif c[0] == b'cue ' and len(c[1]) >= 4:
            ncue = struct.unpack_from('<I', c[1], 0)[0]
            for i in range(ncue):
                off = 4 + i * 24
                if off + 24 <= len(c[1]):
                    for field in (off + 4, off + 20):           # play order position, sample offset
                        struct.pack_into('<I', c[1], field, struct.unpack_from('<I', c[1], field)[0] // 2)
    body = b'WAVE'
    for cid, b in chunks:
        body += cid + struct.pack('<I', len(b)) + bytes(b) + (b'\0' if len(b) & 1 else b'')
    out = b'RIFF' + struct.pack('<I', len(body)) + body
    print(f" -> 22050 Hz, {len(newraw) // 1024} KB")
    return out


def main():
    if len(sys.argv) < 2:
        print(__doc__); sys.exit(1)
    root = os.path.abspath(sys.argv[1])
    dry = '--dry-run' in sys.argv
    backup = os.path.join(os.path.dirname(root), 'dc_sound_44k')
    n = 0
    for d, _, files in os.walk(root):
        for f in sorted(files):
            if not f.lower().endswith('.wav'):
                continue
            p = os.path.join(d, f)
            res = convert(p, dry)
            if not res:
                continue
            n += 1
            if dry:
                continue
            rel = os.path.relpath(p, root)
            bak = os.path.join(backup, rel)
            os.makedirs(os.path.dirname(bak), exist_ok=True)
            if not os.path.exists(bak):
                shutil.copy2(p, bak)
            with open(p, 'wb') as fo:
                fo.write(res)
    print(f"{n} file(s) {'to convert' if dry else 'converted'}" + ('' if dry or n == 0 else f"; originals in {backup}"))


if __name__ == '__main__':
    main()
