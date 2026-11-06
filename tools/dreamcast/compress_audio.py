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
with it. --max-rate HZ resamples EVERY file above HZ (music, effects, voices): the
Dreamcast mixer runs at 22050 Hz, and a 44.1 / 37.8 kHz bank played there is
decimated without a filter, which aliases - and costs twice the space.
BGM_LOOP_HEAD: the asset migrator (1.4.2+) writes a looping PS1 track as
intro + first pass + second pass, looping on the second pass: the second pass
starts with the tails of the previous pass, the first must not. That doubles
the looping tracks - too much for the Dreamcast's RAM. Such a file is cut to
intro + the first pass's opening seconds (until those tails have died away) +
the second pass, and a 're1j' chunk tells the Dreamcast mixer to jump, the
first time through, from the end of that opening to the same point of the
second pass. Same sound, about half the size. --no-loop-head keeps the file.

--bgm-gain DB changes the music's level (e.g. -7 to match the PC
release's music, which is that much quieter than the PS1 rips). PS1 music is 44.1 kHz stereo - ~2.7 MB of RAM per minute even as IMA;
--bgm-rate 22050 halves that and keeps what makes it better than the PC
music (instruments, stereo, reverb, intro + loop).
Originals are MOVED to --backup (default: <data-dir>/../audio_wav_backup),
keeping their relative paths; --restore moves them back.

The PC/Linux builds cannot read IMA banks - keep the backup if the same data
folder also feeds those.

Needs ffmpeg.
"""
import argparse
import array
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


def loop_region(ch_list):
    """(start, end_exclusive) of the first smpl loop, or None."""
    body = dict(ch_list).get(b"smpl")
    if body is None or len(body) < 60 or struct.unpack_from("<I", body, 28)[0] == 0:
        return None
    start, last = struct.unpack_from("<II", body, 44)
    return start, last + 1


def mono(pcm, ch, a, n, step=1):
    """Mono sums of frames a, a+step, ... (n of them) of interleaved PCM."""
    out = []
    for k in range(n):
        i = (a + k * step) * ch
        out.append(pcm[i] + pcm[i + 1] if ch == 2 else pcm[i])
    return out


def rel_diff(x, y):
    e = sum((p - q) * (p - q) for p, q in zip(x, y))
    s = sum(p * p for p in y) + 1.0
    return e / s


def loop_head_layout(ch_list):
    """For a BGM in the migrator's intro + pass 1 + pass 2 layout, the frames to
    keep: (s1, head, j1, j2) = pass 1 starts at s1, keep `head` frames of it,
    pass 2 is [j1, j2). None when the file is not in that layout."""
    fmt = dict(ch_list).get(b"fmt ")
    data = dict(ch_list).get(b"data")
    reg = loop_region(ch_list)
    if fmt is None or data is None or reg is None:
        return None
    ch, rate, bits = struct.unpack_from("<H", fmt, 2)[0], struct.unpack_from("<I", fmt, 4)[0], \
        struct.unpack_from("<H", fmt, 14)[0]
    if bits != 16 or ch not in (1, 2):
        return None
    pcm = array.array("h")
    pcm.frombytes(data[: len(data) // (2 * ch) * 2 * ch])
    if sys.byteorder != "little":
        pcm.byteswap()
    frames = len(pcm) // ch
    j1, j2 = reg
    L = j2 - j1
    if L < rate * 2 or j2 > frames or j1 - L < 0:
        return None
    # Pass 1 is the same notes as pass 2, minus the carried-over tails: find
    # its exact start near j1 - L, matching a stretch late in the pass.
    probe_at, probe_n, step = L // 2, min(rate // 2, L // 4), 4
    ref = mono(pcm, ch, j1 + probe_at, probe_n // step, step)
    best = None
    for d in range(-64, 65):
        s1 = j1 - L + d
        if s1 < 0 or s1 + probe_at + probe_n >= j1:
            continue
        e = rel_diff(mono(pcm, ch, s1 + probe_at, probe_n // step, step), ref)
        if best is None or e < best[0]:
            best = (e, s1)
    if best is None or best[0] > 0.01:
        return None                       # not two copies of one pass
    s1 = best[1]
    # The head: until pass 1 and pass 2 agree for good (the tails are gone).
    blk = rate // 20                      # 50 ms
    limit = min(L // 2, rate * 4)
    head = None
    quiet = 0
    for b in range(0, limit // blk):
        x = mono(pcm, ch, s1 + b * blk, blk // 2, 2)
        y = mono(pcm, ch, j1 + b * blk, blk // 2, 2)
        if rel_diff(x, y) < 0.003:
            quiet += 1
            if quiet == 20:               # 1 s of agreement
                head = (b - 19) * blk + rate // 10
                break
        else:
            quiet = 0
    if head is None:
        return None
    head = max(head, rate // 4)
    return s1, head, j1, j2, ch, pcm


def build_loop_head(ch_list):
    """New chunk list for the cut file (see BGM_LOOP_HEAD), or None."""
    lay = loop_head_layout(ch_list)
    if lay is None:
        return None
    s1, head, j1, j2, ch, pcm = lay
    keep = pcm[: (s1 + head) * ch] + pcm[j1 * ch: j2 * ch]
    if sys.byteorder != "little":
        keep.byteswap()
    loop_start = s1 + head                # pass 2 begins here in the new file
    loop_end = loop_start + (j2 - j1)
    smpl = bytearray(dict(ch_list)[b"smpl"])
    struct.pack_into("<II", smpl, 44, loop_start, loop_end - 1)
    # re1j: first time through, at frame `from` continue at frame `to`.
    jump = struct.pack("<II", loop_start, loop_start + head)
    out = []
    for cid, body in ch_list:
        if cid == b"data":
            body = keep.tobytes()
        elif cid == b"smpl":
            body = bytes(smpl)
        out.append((cid, body))
    out.append((b"re1j", jump))
    return out


def write_wav(path, ch_list):
    body = b"WAVE"
    for cid, b in ch_list:
        body += cid + struct.pack("<I", len(b)) + b + (b"\0" if len(b) & 1 else b"")
    with open(path, "wb") as f:
        f.write(b"RIFF" + struct.pack("<I", len(body)) + body)


def scaled_jump(body, old_rate, new_rate):
    a, b = struct.unpack_from("<II", body, 0)
    return struct.pack("<II", int(round(a * new_rate / old_rate)), int(round(b * new_rate / old_rate)))


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
        elif cid in (b"re1p", b"mark"):
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


def convert(path, root, backup, bgm_rate, bgm_gain=0.0, max_rate=0, loop_head=True):
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
    new_rate = old_rate
    if bgm_rate and os.path.basename(path).lower().startswith("bgm"):
        new_rate = min(new_rate, bgm_rate)
    if max_rate:
        new_rate = min(new_rate, max_rate)
    resample = new_rate < old_rate
    # 're1p' is the asset migrator's PS1 marker: without it the game ignores
    # the loop region and plays the file the PC way (whole-buffer loop).
    is_bgm = os.path.basename(path).lower().startswith("bgm")
    before = len(src)
    source = path
    cut = None
    if is_bgm and loop_head:
        cut = build_loop_head(ch_list)          # BGM_LOOP_HEAD
        if cut is not None:
            ch_list = cut
    keep = [(cid, body) for cid, body in ch_list if cid in (b"smpl", b"re1p", b"mark", b"re1j")]
    fd, tmp = tempfile.mkstemp(suffix=".wav", dir=os.path.dirname(path))
    os.close(fd)
    cut_tmp = None
    try:
        if cut is not None:
            fd, cut_tmp = tempfile.mkstemp(suffix=".wav", dir=os.path.dirname(path))
            os.close(fd)
            write_wav(cut_tmp, cut)
            source = cut_tmp
        cmd = ["ffmpeg", "-hide_banner", "-loglevel", "error", "-y", "-i", source]
        if is_bgm and bgm_gain:
            cmd += ["-af", "volume=%gdB" % bgm_gain]
        if resample:
            cmd += ["-ar", str(new_rate)]
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
                    body = scaled_smpl(body, old_rate, new_rate, frames)
                if cid == b"re1j" and resample:
                    body = scaled_jump(body, old_rate, new_rate)
                out += cid + struct.pack("<I", len(body)) + body + (b"\0" if len(body) & 1 else b"")
            struct.pack_into("<I", out, 4, len(out) - 8)
            with open(tmp, "wb") as f:
                f.write(out)
        dst = os.path.join(backup, os.path.relpath(path, root))
        os.makedirs(os.path.dirname(dst), exist_ok=True)
        shutil.move(path, dst)
        os.replace(tmp, path)
        if cut_tmp:
            os.unlink(cut_tmp)
    except Exception as e:
        for t in (tmp, cut_tmp):
            if t and os.path.exists(t):
                os.unlink(t)
        return ("fail", path, str(e))
    return ("ok", path, (before, os.path.getsize(path), cut is not None))


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
    ap.add_argument("--max-rate", type=int, default=0,
                    help="resample every file above this rate down to it (e.g. 22050)")
    ap.add_argument("--no-loop-head", action="store_true",
                    help="keep migrator BGMs whole (intro + pass 1 + pass 2) instead of cutting pass 1")
    ap.add_argument("--bgm-gain", type=float, default=0.0,
                    help="level change for Bgm_*.wav in dB (e.g. -7)")
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
    skipped, failed, cut = [], [], []
    with cf.ThreadPoolExecutor(max_workers=os.cpu_count() or 4) as ex:
        for status, path, extra in ex.map(lambda p: convert(p, root, backup, a.bgm_rate, a.bgm_gain, a.max_rate,
                                                          not a.no_loop_head), files):
            if status == "ok":
                before += extra[0]
                after += extra[1]
                if extra[2]:
                    cut.append(os.path.basename(path))
            elif status == "skip":
                skipped.append((path, extra))
            else:
                failed.append((path, extra))
    done = len(files) - len(skipped) - len(failed)
    print("converted %d files: %.1f MB -> %.1f MB" % (done, before / 1048576, after / 1048576))
    if cut:
        print("BGM loops cut to intro + head + second pass (%d): %s" % (len(cut), " ".join(sorted(cut))))
    if skipped:
        print("skipped %d (already compressed / not PCM)" % len(skipped))
    for p, why in failed:
        print("FAILED %s: %s" % (p, why))
    print("originals moved to %s (restore with --restore)" % backup)


if __name__ == "__main__":
    main()
