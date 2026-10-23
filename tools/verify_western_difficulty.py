#!/usr/bin/env python3
"""verify_western_difficulty.py - the evidence behind [Game] WesternDifficulty.

Western difficulty (src/Globals.h western_difficulty()) is what the PS1 USA
release, the Saturn USA release and the Director's Cut ship as their only rules.
This re-derives the table claims from the original binaries; run from the repo
root. Needs assets/og_ResidentEvil.exe, assets/PSX/SLUS_001.70, the PS1 USA disc
image and the extracted Saturn disc (assets/SATURN_ISO_CONTENT). Needs capstone
for the overlay checks.

  1. Weapon hit table: the PS1 USA table (SLUS_001.70 0x8008bbe0, 200 x 12 B)
     equals the PC's SECOND-playthrough table (0x004bbffe) in damage and hit
     state, differs from the first-playthrough table (0x004bb698) in 71 records,
     and is byte-identical in all five Saturn GAME*.PRG modules (big-endian).
     => no new table is needed; raising SCENARIO_FLAG_SECOND_PLAYTHROUGH selects it.
     The Japanese Saturn ("Bio Hazard", assets/SATURN_JPN_ISO_CONTENT) is the
     other way round: all five of its GAME*.PRG hold the PC's FIRST-playthrough
     table (Japanese rules), so western_difficulty() is off for Saturn mode on
     the JPN tree.
  2. The USA stage overlays never call Flg_ck (0x80048ef4): every enemy runs one
     fixed variant, so there is no first-playthrough branch to port.
  3. Zombie bite: the only table in the USA overlays is the PC's HARD row
     (12,12,9,9,12,...), at STAGE1 0x80122388 and the same offset in the others.
  4. Yawn hit damage (the one exception): enemy-0 health RMW is a flat -0xf (and
     -0x41) in the USA and DC overlays - the PC's FIRST-playthrough branch.

Exit status 1 if a check fails.
"""
import glob
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

N = 200
ok = True


def check(name, cond, detail=""):
    global ok
    print("  [%s] %s %s" % ("ok" if cond else "FAIL", name, detail))
    ok = ok and cond


def pc_off(va):
    # .rdata VA 0x4af000 raw 0xae600; .data VA 0x4b1000 raw 0xb0400
    for vs, raw, size in ((0x4AF000, 0xAE600, 0x2000), (0x4B1000, 0xB0400, 0x100000)):
        if vs <= va < vs + size:
            return raw + va - vs
    raise ValueError(hex(va))


def weapon_tables():
    print("1. weapon hit tables")
    pc = open("assets/og_ResidentEvil.exe", "rb").read()
    ps = open("assets/PSX/SLUS_001.70", "rb").read()
    tx = struct.unpack_from("<I", ps, 0x18)[0]
    ps_base = 0x800 + 0x8008BBE0 - tx
    first = [struct.unpack_from("<3hhBBBB", pc, pc_off(0x4BB698) + i * 12) for i in range(N)]
    second = [struct.unpack_from("<hhB", pc, pc_off(0x4BBFFE) + i * 12) for i in range(N)]
    psx = [struct.unpack_from("<3hhBBBB", ps, ps_base + i * 12) for i in range(N)]
    d_first = sum(1 for i in range(N) if first[i][3] != psx[i][3] or first[i][6] != psx[i][6])
    d_second = sum(1 for i in range(N) if second[i][0] != psx[i][3] or second[i][2] != psx[i][6])
    check("PS1 USA == PC second-playthrough table", d_second == 0, "(%d differ)" % d_second)
    check("PS1 USA != PC first-playthrough table", d_first == 71, "(%d differ)" % d_first)

    def be(r):
        return struct.pack(">3hh4B", *struct.unpack("<3hh4B", r))

    recs = [ps[ps_base + i * 12: ps_base + i * 12 + 12] for i in range(N)]
    probe = b"".join(be(recs[i]) for i in (20, 21, 22))
    for prg in sorted(glob.glob("assets/SATURN_ISO_CONTENT/GAME*.PRG")):
        d = open(prg, "rb").read()
        pos = d.find(probe)
        same = pos >= 0 and all(d[pos - 240 + i * 12: pos - 240 + i * 12 + 12] == be(recs[i])
                                for i in range(N))
        check("Saturn %s table == PS1 USA" % os.path.basename(prg), same,
              "@ file 0x%x" % (pos - 240) if pos >= 0 else "(not found)")

    # The Japanese Saturn: same record layout, located by the coordinates (the
    # first three halfwords), which both difficulty tables share.
    jpn = sorted(glob.glob("assets/SATURN_JPN_ISO_CONTENT/GAME*.PRG"))
    if not jpn:
        print("  [skip] no assets/SATURN_JPN_ISO_CONTENT (saturn_iso.py --jpn extract)")
    for prg in jpn:
        d = open(prg, "rb").read()
        start = 0
        while True:
            pos = d.find(be(recs[0])[:6], start)
            if pos < 0 or all(d[pos + 12 * i:pos + 12 * i + 6] == be(recs[i])[:6]
                              for i in range(3)):
                break
            start = pos + 2
        if pos < 0:
            check("JPN Saturn %s table found" % os.path.basename(prg), False)
            continue
        rows = [struct.unpack(">3hh4B", d[pos + 12 * i:pos + 12 * i + 12]) for i in range(N)]
        d_first = sum(1 for i in range(N) if rows[i][3] != first[i][3] or rows[i][6] != first[i][6])
        d_second = sum(1 for i in range(N) if rows[i][3] != second[i][0] or rows[i][6] != second[i][2])
        check("JPN Saturn %s table == PC first playthrough" % os.path.basename(prg),
              d_first == 0 and d_second == 71,
              "(%d differ from first, %d from second)" % (d_first, d_second))
    src = open(os.path.join("src", "Globals.h"), encoding="utf-8").read()
    check("western_difficulty() leaves the JPN Saturn out",
          "(g_bSaturnMode && GetAssetVersion() == 0)" in src)


def overlays():
    import saturn_room
    print("2/3. USA stage overlays")
    iso = saturn_room.ps1_iso()
    flg_ck_call = struct.pack("<I", 0x0C000000 | ((0x80048EF4 & 0x0FFFFFFF) >> 2))
    # the PS1 table is only 11 bytes before code follows it (rows 9+ are 0)
    hard = bytes([12, 12, 9, 9, 12, 12, 12, 12, 12, 0, 0])
    norm = bytes([10, 10, 6, 6, 10, 10, 10, 10, 10, 0, 0])
    calls = 0
    hard_n = norm_n = 0
    for p, (n, lba, size, fl) in iso.entries():
        if fl == 0 and p.upper().endswith(".EXE") and "STAGE" in p.upper():
            d = iso.get(p)
            calls += d.count(flg_ck_call)
            hard_n += hard in d
            norm_n += norm in d
    check("no Flg_ck call in any USA STAGEn.EXE", calls == 0, "(%d calls)" % calls)
    check("zombie bite table: HARD row present, NORMAL row absent",
          hard_n >= 6 and norm_n == 0, "(hard in %d overlays, normal in %d)" % (hard_n, norm_n))


def yawn():
    try:
        from capstone import Cs, CS_ARCH_MIPS, CS_MODE_MIPS32, CS_MODE_LITTLE_ENDIAN
    except ImportError:
        print("4. Yawn: capstone missing, skipped")
        return
    import saturn_room
    print("4. Yawn hit damage (USA overlays)")
    md = Cs(CS_ARCH_MIPS, CS_MODE_MIPS32 | CS_MODE_LITTLE_ENDIAN)
    md.skipdata = True
    iso = saturn_room.ps1_iso()
    for st in ("STAGE2", "STAGE7"):
        d = iso.get("/PSX/PROG2/%s.EXE" % st)
        base = struct.unpack_from("<I", d, 0x18)[0]
        size = struct.unpack_from("<I", d, 0x1C)[0]
        ins = list(md.disasm(d[0x800:0x800 + size], base))
        vals = []
        for i, x in enumerate(ins):
            # enemy-0 health (0x800c532c) read-modify-write
            if x.mnemonic == "sh" and "0x532c(" in x.op_str:
                vals += [y.op_str.split(",")[-1].strip() for y in ins[max(0, i - 6):i]
                         if y.mnemonic == "addiu" and not y.op_str.startswith("$sp")]
        check("%s Yawn hit is flat -0x41 / -0xf" % st, vals == ["-0x41", "-0xf"], str(vals))


if __name__ == "__main__":
    weapon_tables()
    overlays()
    yawn()
    print("PASS" if ok else "FAIL")
    sys.exit(0 if ok else 1)
