#!/usr/bin/env python3
"""compress_audio.py <data-dir> [--backup DIR] [--restore]

Converts the game's sound banks to IMA ADPCM WAV (4 bits/sample) for the
Dreamcast build: every *.wav under a Sound/ or Voice/ folder of each tree in
<data-dir> (USA/, JPN/, DC/, ...). Names stay the same, so the game finds them
as before; the Dreamcast audio backend keeps them compressed in RAM and
decodes one ~2000-sample block at a time in the mixer.

  16-bit voices: 4:1     8-bit music: 2:1     (disc space and RAM alike)

Skipped: files that are not plain PCM (already converted, Ogg in disguise).
PS1 audio from the asset migrator (WAV format) is supported: its loop region
('smpl') and PS1 marker ('mark') are carried into the compressed file.

--bgm-rate HZ resamples only the music (Bgm_*.wav) to HZ, loop points scaled
with it. PS1 music is 44.1 kHz stereo - ~2.7 MB of RAM per minute even as IMA;
--bgm-rate 22050 halves that and keeps what makes it better than the PC
music (instruments, stereo, reverb, intro + loop).
Originals are MOVED to --backup (default: <data-dir>/../audio_wav_backup),
keeping their relative paths; --restore moves them back.

The PC/Linux builds cannot read IMA banks - keep the backup if the same data
folder also feeds those.

Needs ffmpeg.
"""
import argparse
import concurrent.futures as cf
import os
import shutil
import struct
import subprocess
import sys
import tempfile


def chunks(data):
    """[(id, body)] of a RIFF/WAVE file, or None."""
    if len(data) < 12 or data[:4] != b"RIFF" or data[8:12] != b"WAVE":
        return None
    out, off = [], 12
    while off + 8 <= len(data):
        cid, clen = data[off:off + 4], struct.unpack_from("<I", data, off + 4)[0]
        out.append((cid, data[off + 8:off + 8 + clen]))
        off += 8 + clen + (clen & 1)
    return out


def ima_frames(ch_list):
    """Frame count of an IMA ADPCM WAV, the way the Dreamcast backend counts."""
    fmt = dict(ch_list).get(b"fmt ")
    data = dict(ch_list).get(b"data")
    ch, align = struct.unpack_from("<H", fmt, 2)[0], struct.unpack_from("<H", fmt, 12)[0]
    spb = struct.unpack_from("<H", fmt, 18)[0] if len(fmt) >= 20 else (align - 4 * ch) * 8 // (4 * ch) + 1
    n = (len(data) // align) * spb
    rem = len(data) % align
    if rem >= 4 * ch:
        n += (rem - 4 * ch) * 8 // (4 * ch) + 1
    return n


def scaled_smpl(body, old_rate, new_rate, max_frame):
    """smpl chunk with sample period and loop points moved to new_rate."""
    b = bytearray(body)
    struct.pack_into("<I", b, 8, int(round(1e9 / new_rate)))         # dwSamplePeriod (ns)
    nloops = struct.unpack_from("<I", b, 28)[0]
    for i in range(nloops):
        o = 36 + i * 24
        if o + 24 > len(b):
            break
        start, last = struct.unpack_from("<II", b, o + 8)
        ns = int(round(start * new_rate / old_rate))
        nl = min(int(round((last + 1) * new_rate / old_rate)) - 1, max_frame - 1)
        struct.pack_into("<II", b, o + 8, ns, max(nl, ns))
    return bytes(b)


def wav_info(path):
    """(format_tag, has_mark) or None if not a RIFF/WAVE file."""
    try:
        with open(path, "rb") as f:
            data = f.read()
    except OSError:
        return None
    if len(data) < 12 or data[:4] != b"RIFF" or data[8:12] != b"WAVE":
        return None
    tag, mark, off = None, False, 12
    while off + 8 <= len(data):
        cid, clen = data[off:off + 4], struct.unpack_from("<I", data, off + 4)[0]
        if cid == b"fmt " and clen >= 2:
            tag = struct.unpack_from("<H", data, off + 8)[0]
        elif cid == b"mark":
            mark = True
        off += 8 + clen + (clen & 1)
    return tag, mark


def targets(root):
    for dirpath, _dirs, files in os.walk(root):
        parts = [p.lower() for p in os.path.relpath(dirpath, root).split(os.sep)]
        if not ({"sound", "voice"} & set(parts)):
            continue
        for name in files:
            if name.lower().endswith(".wav"):
                yield os.path.join(dirpath, name)


def convert(path, root, backup, bgm_rate):
    with open(path, "rb") as f:
        src = f.read()
    ch_list = chunks(src)
    if ch_list is None:
        return ("skip", path, "not a WAV")
    fmt = dict(ch_list).get(b"fmt ")
    tag = struct.unpack_from("<H", fmt, 0)[0] if fmt and len(fmt) >= 16 else None
    if tag != 1:
        return ("skip", path, "already compressed" if tag == 0x11 else "format 0x%x" % (tag or 0))
    old_rate = struct.unpack_from("<I", fmt, 4)[0]
    resample = bgm_rate and os.path.basename(path).lower().startswith("bgm") and old_rate > bgm_rate
    keep = [(cid, body) for cid, body in ch_list if cid in (b"smpl", b"mark")]
    before = len(src)
    fd, tmp = tempfile.mkstemp(suffix=".wav", dir=os.path.dirname(path))
    os.close(fd)
    try:
        cmd = ["ffmpeg", "-hide_banner", "-loglevel", "error", "-y", "-i", path]
        if resample:
            cmd += ["-ar", str(bgm_rate)]
        r = subprocess.run(cmd + ["-c:a", "adpcm_ima_wav", tmp], capture_output=True, text=True)
        if r.returncode != 0 or (wav_info(tmp) or (None,))[0] != 0x11:
            os.unlink(tmp)
            return ("fail", path, r.stderr.strip()[:200])
        if keep:
            # Carry the loop region / PS1 marker over (ffmpeg drops them).
            with open(tmp, "rb") as f:
                out = bytearray(f.read())
            frames = ima_frames(chunks(bytes(out)))
            for cid, body in keep:
                if cid == b"smpl" and resample:
                    body = scaled_smpl(body, old_rate, bgm_rate, frames)
                out += cid + struct.pack("<I", len(body)) + body + (b"\0" if len(body) & 1 else b"")
            struct.pack_into("<I", out, 4, len(out) - 8)
            with open(tmp, "wb") as f:
                f.write(out)
        dst = os.path.join(backup, os.path.relpath(path, root))
        os.makedirs(os.path.dirname(dst), exist_ok=True)
        shutil.move(path, dst)
        os.replace(tmp, path)
    except Exception as e:
        if os.path.exists(tmp):
            os.unlink(tmp)
        return ("fail", path, str(e))
    return ("ok", path, (before, os.path.getsize(path)))


def restore(root, backup):
    n = 0
    for dirpath, _dirs, files in os.walk(backup):
        for name in files:
            src = os.path.join(dirpath, name)
            dst = os.path.join(root, os.path.relpath(src, backup))
            os.makedirs(os.path.dirname(dst), exist_ok=True)
            shutil.move(src, dst)
            n += 1
    print("restored %d files from %s" % (n, backup))


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("data")
    ap.add_argument("--backup")
    ap.add_argument("--restore", action="store_true")
    ap.add_argument("--bgm-rate", type=int, default=0,
                    help="resample Bgm_*.wav above this rate down to it (e.g. 22050)")
    a = ap.parse_args()
    root = os.path.abspath(a.data)
    backup = os.path.abspath(a.backup or os.path.join(os.path.dirname(root), "audio_wav_backup"))
    if a.restore:
        restore(root, backup)
        return
    if shutil.which("ffmpeg") is None:
        sys.exit("ffmpeg not found (apt install ffmpeg)")
    files = list(targets(root))
    if not files:
        sys.exit("no WAV files under a Sound/ or Voice/ folder in %s" % root)
    before = after = 0
    skipped, failed = [], []
    with cf.ThreadPoolExecutor(max_workers=os.cpu_count() or 4) as ex:
        for status, path, extra in ex.map(lambda p: convert(p, root, backup, a.bgm_rate), files):
            if status == "ok":
                before += extra[0]
                after += extra[1]
            elif status == "skip":
                skipped.append((path, extra))
            else:
                failed.append((path, extra))
    done = len(files) - len(skipped) - len(failed)
    print("converted %d files: %.1f MB -> %.1f MB" % (done, before / 1048576, after / 1048576))
    if skipped:
        print("skipped %d (already compressed / not PCM)" % len(skipped))
    for p, why in failed:
        print("FAILED %s: %s" % (p, why))
    print("originals moved to %s (restore with --restore)" % backup)


if __name__ == "__main__":
    main()
