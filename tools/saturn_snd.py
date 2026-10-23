"""Sega Saturn sound banks of Resident Evil: CD_PACK -> tone bank + sequence bank.

The Saturn runs Sega's standard 68000 sound driver ("Ver-2.10 -L", Jul 2 1996,
/SND/SDDRVS.PCK). Every music file is a CD_PACK with two segments for that
driver: a tone bank (TON: voices, key layers, raw PCM) and a sequence bank
(SEQ: one 16-channel event stream per song). This tool reads both, so a
renderer's input can be checked against the files instead of guessed.

    python tools/saturn_snd.py pack  FILE.CDP            segment table
    python tools/saturn_snd.py ton   FILE.CDP            voices and layers
    python tools/saturn_snd.py seq   FILE.CDP [SONG]     event listing
    python tools/saturn_snd.py scan  DIR                 parse every *.CDP

The C++ renderer is tools/asset_migrator/src/core/SaturnSound.cpp
(`re1am_selftest satsong` / `satkeys`). FILE may be a disc file extracted with
tools/saturn_iso.py. Note that the copy
under assets/SATURN_ISO_CONTENT is incomplete for ~100 files (SDDRVS.PCK among them);
extract a fresh one if a size looks wrong.

Format notes, read out of the driver (SDDRVS.PCK, 68000 at 0; Ghidra
"SDDRVS.PCK.0") and checked against every CDP on the disc by `scan`. Where
VGMTrans's SegSatSeq (written for driver 1.33) reads it differently, the
driver's reading is the one here - see docs/SATURN_VERSION_ANALYSIS.md 6a:
  TON  +0 mixer tables, +2 velocity tables, +4 PEG tables, +6 PLFO tables,
       +8.. u16 voice offsets up to the mixer table. Voice: pitch-bend range,
       portamento, layer count - 1, volume bias, then 0x20-byte layers whose
       first 0x18 bytes are SCSP slot register images (+2 = SA/LPCTL/PCM8B,
       +6 LSA, +8 LEA, +A/+C EG, +E TL, +10 modulation, +14 LFO, +16 ISEL,
       +18 DISDL/DIPAN), then base note, fine tune, VL/PEG/PLFO table index.
       Sample address = (BE32 at +2) & 0x7FFFF, relative to the bank start.
  SEQ  +0 song count, then BE32 song offsets. Song: PPQN, tempo event count,
       offset of the event stream, offset of the tempo loop point; tempo events
       are (BE32 step count, BE32 microseconds per quarter).
       Events: 00-7F note (ch = low 5 bits, bit 6 = gate bit 8, bit 5 = step
       bit 8; key, velocity, gate, step), Ax/Bx poly pressure/controller (num,
       value, step), Cx program (prog, step), Dx pressure (v, step), Ex bend
       (v, step) - in Ax..Ex bit 7 of the FIRST data byte is channel + 16,
       which the driver rejects; the step is 8 bits. 81 call (u16 offset from
       the stream, count of events), 82 loop point (step; set the first time,
       jumped to the second), 83 end, 88-8B gate +0x200/0x800/0x1000/0x2000,
       8C-8F step +0x100/0x200/0x800/0x1000; 80, 84-87, 90-9F, F0-FF stop the
       song. An event's step is the wait BEFORE it (0x051F2: the counter adds
       the event's step and emits it once <= 0), at 500 Hz in 8.8 steps
       (0x7D000 / (us per quarter / PPQN) per tick).
"""
import os
import struct
import sys


def be16(d, o):
    return struct.unpack_from(">H", d, o)[0]


def be32(d, o):
    return struct.unpack_from(">I", d, o)[0]


def read_pack(data):
    if not data.startswith(b"CD_PACK Ver.1.0"):
        raise ValueError("not a CD_PACK")
    segs = []
    o = 0x14
    while o + 16 <= len(data):
        src, size, dst, cont = struct.unpack_from(">IIII", data, o)
        segs.append((src, size, dst, cont))
        o += 16
        if cont == 0xFFFFFFFF:
            break
    return segs


def split_pack(data):
    """(tone bank bytes, sequence bank bytes) of a music CD_PACK: the last two
    segments bound for sound RAM above the area map (a pack may also carry the
    area map for 0x500, or SH-2 data)."""
    segs = [(src, size) for src, size, dst, cont in read_pack(data)
            if 0x25A01000 <= dst < 0x25A80000]
    if len(segs) < 2:
        return None, None
    (ts, tz), (ss, sz) = segs[-2], segs[-1]
    return data[ts:ts + tz], data[ss:ss + sz]


def parse_ton(t):
    mixer, vl, peg, plfo = be16(t, 0), be16(t, 2), be16(t, 4), be16(t, 6)
    voices = []
    for o in range(8, mixer, 2):
        vo = be16(t, o)
        v = {"off": vo, "bend": t[vo], "porta": t[vo + 1],
             "nlayers": (t[vo + 2] if t[vo + 2] < 0x80 else -1) + 1,
             "bias": struct.unpack_from("b", t, vo + 3)[0], "layers": []}
        for i in range(v["nlayers"]):
            lo = vo + 4 + i * 0x20
            L = t[lo:lo + 0x20]
            w = be32(L, 2)
            lay = {
                "off": lo, "lo": L[0], "hi": L[1],
                "peg": L[2] >> 7, "plfo": (L[2] >> 6) & 1,
                "loop": (L[3] >> 5) & 3, "pcm8": (L[3] >> 4) & 1,
                "sa": w & 0x7FFFF, "lsa": be16(L, 6), "lea": be16(L, 8),
                "eg1": be16(L, 10), "eg2": be16(L, 12),
                "tl": L[15], "sdir": L[14] & 1, "mod": be16(L, 16),
                "lfo": be16(L, 20), "isel": be16(L, 22),
                "disdl": L[24] >> 5, "dipan": L[24] & 0x1F,
                "efsdl": L[25] >> 5 if False else None,
                "base": L[25], "fine": struct.unpack_from("b", L, 26)[0],
                "b27": L[27], "b28": L[28],
                "vl": L[29], "pegi": L[30], "plfoi": L[31],
            }
            v["layers"].append(lay)
        voices.append(v)
    return {"mixer": mixer, "vl": vl, "peg": peg, "plfo": plfo,
            "voices": voices}


NAMES_8X = {0x88: 512, 0x89: 2048, 0x8A: 4096, 0x8B: 8192}  # 0x87 is invalid in Ver-2.10
RESTS_8X = {0x8C: 256, 0x8D: 512, 0x8E: 2048, 0x8F: 4096}


def seq_songs(s):
    n = be16(s, 0)
    return [be32(s, 2 + i * 4) for i in range(n)]


def parse_song(s, so, out=None):
    """Walk one song linearly (no call expansion). Returns stats."""
    ppqn, ntempo, evo, tloop = be16(s, so), be16(s, so + 2), be16(s, so + 4), be16(s, so + 6)
    tempos = [struct.unpack_from(">II", s, so + 8 + i * 8) for i in range(ntempo)]
    stats = {"ppqn": ppqn, "tempos": tempos, "tloop": tloop, "notes": 0,
             "chans": set(), "progs": set(), "ccs": {}, "unknown": [],
             "end": None, "loops": 0, "calls": 0}
    p = so + evo
    base = p
    while p < len(s):
        st = s[p]
        b = p
        p += 1
        if st < 0x80:
            key, vel, gate, step = s[p:p + 4]
            p += 4
            stats["notes"] += 1
            stats["chans"].add(st & 15)
            txt = "note ch%X key %d vel %d gate %d step %d" % (
                st & 15, key, vel, gate | ((st & 0x40) << 2), step | ((st & 0x20) << 3))
        elif st & 0xF0 in (0xA0, 0xB0):
            # bit 7 of the FIRST data byte is channel + 16 (0x05A92), not step bit 8
            num, val, step = s[p:p + 3]
            p += 3
            ch = (st & 15) | (0x10 if num & 0x80 else 0)
            num &= 0x7F
            if st & 0xF0 == 0xB0:
                stats["ccs"][num] = stats["ccs"].get(num, 0) + 1
                txt = "cc ch%X %d = %d step %d" % (ch, num, val, step)
            else:
                txt = "poly-pressure ch%X key %d = %d step %d" % (ch, num, val, step)
        elif st & 0xF0 == 0xC0:
            prog, step = s[p:p + 2]
            p += 2
            ch = (st & 15) | (0x10 if prog & 0x80 else 0)
            stats["progs"].add(prog & 0x7F)
            txt = "prog ch%X %d step %d" % (ch, prog & 0x7F, step)
        elif st & 0xF0 == 0xD0:
            v, step = s[p:p + 2]
            p += 2
            txt = "pressure ch%X %d step %d" % ((st & 15) | (0x10 if v & 0x80 else 0), v & 0x7F, step)
        elif st & 0xF0 == 0xE0:
            v, step = s[p:p + 2]
            p += 2
            txt = "bend ch%X %d step %d" % ((st & 15) | (0x10 if v & 0x80 else 0), v & 0x7F, step)
        elif st == 0x81:
            off = be16(s, p)
            cnt = s[p + 2]
            p += 3
            stats["calls"] += 1
            txt = "call +%X (abs %X) count %d" % (off, base + off, cnt)
        elif st == 0x82:
            step = s[p]
            p += 1
            stats["loops"] += 1
            txt = "loop-point step %d" % step
        elif st == 0x83:
            stats["end"] = p
            if out:
                out("%05X  end" % b)
            break
        elif st in NAMES_8X:
            txt = "gate +%d" % NAMES_8X[st]
        elif st in RESTS_8X:
            txt = "rest %d" % RESTS_8X[st]
        else:
            stats["unknown"].append((b, st))
            txt = "UNKNOWN %02X" % st
        if out:
            out("%05X  %s" % (b, txt))
    return stats


def cmd_ton(path):
    ton, _ = split_pack(open(path, "rb").read())
    t = parse_ton(ton)
    print("mixer %X vl %X peg %X plfo %X, %d voices" % (
        t["mixer"], t["vl"], t["peg"], t["plfo"], len(t["voices"])))
    for i, v in enumerate(t["voices"]):
        print("voice %d @%X bend %d porta %d bias %d" % (i, v["off"], v["bend"], v["porta"], v["bias"]))
        for L in v["layers"]:
            print("   keys %3d-%3d sa %05X lsa %04X lea %04X %s loop%d eg %04X/%04X tl %02X "
                  "disdl %d dipan %02X base %d fine %d isel %04X vl %d peg %d/%d plfo %d/%d b27 %02X b28 %02X" % (
                      L["lo"], L["hi"], L["sa"], L["lsa"], L["lea"], "pcm8 " if L["pcm8"] else "pcm16",
                      L["loop"], L["eg1"], L["eg2"], L["tl"], L["disdl"], L["dipan"], L["base"],
                      L["fine"], L["isel"], L["vl"], L["peg"], L["pegi"], L["plfo"], L["plfoi"],
                      L["b27"], L["b28"]))


def cmd_seq(path, song=None):
    _, seq = split_pack(open(path, "rb").read())
    songs = seq_songs(seq)
    print("%d songs: %s" % (len(songs), " ".join("%X" % o for o in songs)))
    for i, so in enumerate(songs):
        if song is not None and i != song:
            continue
        st = parse_song(seq, so, out=print if song is not None else None)
        print("song %d @%X ppqn %d tempos %s tloop %X notes %d chans %s progs %s ccs %s "
              "calls %d loops %d end %s unknown %s" % (
                  i, so, st["ppqn"], st["tempos"], st["tloop"], st["notes"], sorted(st["chans"]),
                  sorted(st["progs"]), st["ccs"], st["calls"], st["loops"],
                  "%X" % st["end"] if st["end"] else None, st["unknown"][:5]))


def cmd_scan(d):
    bad = 0
    for f in sorted(os.listdir(d)):
        if not f.upper().endswith(".CDP"):
            continue
        data = open(os.path.join(d, f), "rb").read()
        try:
            segs = read_pack(data)
            ton, seq = split_pack(data)
            t = parse_ton(ton)
            nl = sum(len(v["layers"]) for v in t["voices"])
            line = "%-12s segs %d ton %6X voices %3d layers %3d" % (f, len(segs), len(ton), len(t["voices"]), nl)
            if seq:
                songs = seq_songs(seq)
                info = []
                for so in songs:
                    st = parse_song(seq, so)
                    if st["unknown"] or st["end"] is None:
                        bad += 1
                    info.append("%d%s" % (st["notes"], "!" if st["unknown"] or st["end"] is None else ""))
                line += " seq %5X songs %d notes %s" % (len(seq), len(songs), ",".join(info))
            print(line)
        except Exception as e:  # noqa: BLE001 - report and keep scanning
            bad += 1
            print("%-12s ERROR %s" % (f, e))
    print("bad", bad)


def main(argv):
    if len(argv) < 3:
        print(__doc__)
        return 1
    cmd = argv[1]
    if cmd == "pack":
        for s in read_pack(open(argv[2], "rb").read()):
            print("src %6X size %6X dst %08X cont %08X" % s)
    elif cmd == "ton":
        cmd_ton(argv[2])
    elif cmd == "seq":
        cmd_seq(argv[2], int(argv[3]) if len(argv) > 3 else None)
    elif cmd == "scan":
        cmd_scan(argv[2])
    else:
        print(__doc__)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
