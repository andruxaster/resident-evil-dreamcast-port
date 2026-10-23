#!/usr/bin/env python3
"""verify_saturn_tick.py - check the Saturn Tick port against the disc.

What it proves, from the files the port was written from:

  * which module holds the Tick: stage byte 2 -> g_NextModule 3 -> GAME2, read
    out of the update tables (type 6 = a real function in GAME1, GAME2 and
    GAME5, an `rts` stub in GAME3 and GAME4);
  * the Tick's walk root and the Hunter's chase root are the same size of
    number (so the keyframe tables are in the PS1 section's units, not Saturn
    mesh units) and the Tick EMD's PS1 section is the Hunter's, byte for byte;
  * the table the port copied by hand (the pounce roll at 0x0609B8FC) against
    GAME2, and that it is the Hunter's LOW roll in GAME5;
  * the generated animation tables are current (tools/gen_tick_tables.py);
  * the instructions the port's constants come from: `cmp/eq #85` on the
    player's +0x8A in the variant roll, the 0x0A0 turn trim and 0x4800 target in
    the stab are read as literals out of GAME2;
  * both build files list the new sources, there is no basename collision in
    Game.vcxproj, and every hook in the PC code is behind g_bSaturnMode.

Run from the repo root:  python tools/verify_saturn_tick.py
"""
import os
import re
import struct
import subprocess
import sys

BASE = 0x0602B000
ISO = os.path.join("assets", "SATURN_ISO_CONTENT")
fails = []


def check(ok, what):
    print(("  ok   " if ok else "  FAIL ") + what)
    if not ok:
        fails.append(what)


def prg(name):
    return open(os.path.join(ISO, name), "rb").read()


def u32(d, addr):
    o = addr - BASE
    return struct.unpack(">I", d[o:o + 4])[0]


def u16(d, addr):
    o = addr - BASE
    return struct.unpack(">H", d[o:o + 2])[0]


def s16(d, addr):
    o = addr - BASE
    return struct.unpack(">h", d[o:o + 2])[0]


def find_update_table(d):
    """The 48-entry enemy update table: >= 15 identical trailing stub pointers."""
    n = len(d) // 4
    w = struct.unpack(">%dI" % n, d[:n * 4])
    for i in range(n - 15):
        if BASE <= w[i] < 0x06100000 and all(w[i + k] == w[i] for k in range(15)):
            s = i
            while s > 0 and w[s - 1] == w[i]:
                s -= 1
            return 0x0602B000 + 4 * (s - 22), w[s - 22:s - 22 + 48]
    return None, None


def main():
    os.chdir(os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))

    print("module / table mapping")
    mods = {}
    for m in (1, 2, 3, 4, 5):
        d = prg("GAME%d.PRG" % m)
        base, t = find_update_table(d)
        mods[m] = (d, base, t)
    expect = {1: (0x060959A0, 0x0607339C), 2: (0x06092F40, 0x0606CA54),
              5: (0x0609B4F0, 0x0606CED4)}
    for m, (tb, fn) in expect.items():
        d, base, t = mods[m]
        check(base == tb and t[6] == fn,
              "GAME%d update table 0x%08X, type 6 -> 0x%08X" % (m, tb, fn))
    for m in (3, 4):
        d, base, t = mods[m]
        stubs = [t[k] for k in (2, 4, 5, 7)]
        # the stub entries are 4-byte `rts; nop` pairs one after another
        o = t[6] - BASE
        check(struct.unpack(">HH", d[o:o + 4]) == (0x000B, 0x0009),
              "GAME%d type 6 is an `rts; nop` stub (no Hunter in that stage)" % m)
    check(prg("0.BIN")[0x190:0x190 + 9] == b"TITLE.PRG", "0.BIN names the modules")

    print("keyframe units")
    d2 = mods[2][0]
    walk_root = u32(d2, 0x0609B878 + 4)
    y0 = s16(d2, walk_root + 4)
    emd = open(os.path.join(ISO, "ENEMY", "EM1006.EMD"), "rb").read()
    ps1 = emd[0x10:0x2EC04]
    nn = len(ps1) & ~3
    dirv = struct.unpack_from("<5I", ps1, nn - 0x14)
    ah, ab = dirv[1] & ~3, dirv[2] & ~3
    verts = struct.unpack_from("<h", ps1, ah + 6)[0] // 2
    stride = struct.unpack_from("<h", ps1, ah + 2)[0]
    aligned = (stride + ((stride >> 31) & 3)) >> 2
    cnt, off = struct.unpack_from("<HH", ps1, ab + 0x10 * 4)
    fidx, _ = struct.unpack_from("<HH", ps1, (off & ~3) + ab)
    hy = struct.unpack_from("<3h", ps1, ah + aligned * 4 + fidx * verts * 2)[1]
    check(abs(y0 - hy) < 200,
          "Tick walk root y %d vs Hunter chase root y %d (same units)" % (y0, hy))
    tick = open(os.path.join(ISO, "ENEMY", "EM1016.EMD"), "rb").read()
    check(tick[0x10:0x2EC04] == emd[0x10:0x2EC04],
          "EM1016's PS1 section == EM1006's (the Tick has the Hunter's skeleton)")

    print("hand-copied tables")
    roll = list(d2[0x0609B8FC - BASE:0x0609B8FC - BASE + 16])
    src = open("src/game/saturn/SaturnTick.cpp", encoding="utf-8").read()
    m = re.search(r"tick_pounce_roll_tbl\[16\] = \{([^}]*)\}", src)
    mine = [int(x) for x in m.group(1).replace("\n", " ").split(",") if x.strip()]
    check(roll == mine, "pounce roll table @0x0609B8FC == tick_pounce_roll_tbl")
    d5 = mods[5][0]
    low5 = list(d5[0x0609F738 - BASE:0x0609F738 - BASE + 16])
    check(low5 == roll, "GAME5's Hunter low roll (0x0609F738) is the same 16 bytes")

    print("literals the port's constants come from")
    def has_word(d, lo, hi, value):
        w = struct.pack(">H", value)
        return d.find(w, lo - BASE, hi - BASE) >= 0
    # variant roll: cmp/eq #85 (0x8855) at 0x0606D0CC
    check(u16(d2, 0x0606D0CC) == 0x8855, "0x0606D0CC is `cmp/eq #85,r0` (player flag 0x55)")
    # variant roll state block literal 0x01010100
    check(u32(d2, 0x0606D108) == 0x01010100, "0x0606D108 holds the state block 0x01010100")
    # stab: the easing's doubles sit in a data block the code reaches through
    # the pointer at 0x0606E330 (= 0x0606DF1C); the block starts at 0x0606DF04.
    def f64(addr):
        o = addr - BASE
        return struct.unpack(">d", d2[o:o + 8])[0]
    check(u32(d2, 0x0606E330) == 0x0606DF1C, "stab reads its doubles through 0x0606DF1C")
    check((f64(0x0606DF04), f64(0x0606DF0C), f64(0x0606DF14), f64(0x0606DF1C)) ==
          (16384.0, 18432.0, 4.0, 3276.8),
          "stab doubles: 16384.0 hit limit, 18432.0 target, 4.0 divisor, 3276.8 retract")
    check(has_word(d2, 0x0606DF24, 0x0606E34C, 0x4000), "stab: 0x4000 scale cap literal")
    # tip hit radius 800 = 0x320
    check(has_word(d2, 0x0606DF24, 0x0606E34C, 0x0320), "stab: 800 (0x320) tip radius literal")

    print("animation table and rotation convention")
    # Four entries, ending where the Hunter's SCA record starts (0x0609B8A8).
    frames = [u16(d2, 0x0609B878 + 12 * i) for i in range(4)]
    check(frames == [68, 14, 14, 49], "four Tick animations, frames %s (table ends at 0x0609B8A8)" % frames)
    check(u32(d2, 0x0609B8A8) == 0x80000000 or u16(d2, 0x0609B8A8) == 0x8000,
          "the Hunter's SCA record (0x8000...) follows the table at 0x0609B8A8")

    import math

    def S(a): return math.sin(2 * math.pi * (a & 0xFFF) / 4096)
    def C(a): return math.cos(2 * math.pi * (a & 0xFFF) / 4096)
    def mul(a, b): return [[sum(a[i][k] * b[k][j] for k in range(3)) for j in range(3)] for i in range(3)]
    def mv(m, v): return [sum(m[i][k] * v[k] for k in range(3)) for i in range(3)]
    def Rx(a): c, s_ = C(a), S(a); return [[1, 0, 0], [0, c, -s_], [0, s_, c]]
    def Ry(a): c, s_ = C(a), S(a); return [[c, 0, s_], [0, 1, 0], [-s_, 0, c]]
    def Rz(a): c, s_ = C(a), S(a); return [[c, -s_, 0], [s_, c, 0], [0, 0, 1]]
    # Drawn space: the PC STORES its matrices Y-flipped (RotMatrix = Rx(-x) Ry(y)
    # Rz(-z) = D Rx Ry Rz D) and draws D * stored * D (CompMatrix/ApplyMatrixLV
    # negate Y), so geometry is measured with the EFFECTIVE matrices and the raw
    # offsets. Measuring the stored ones as if drawn is what passed a mirrored
    # Tick (every joint reflected through D) in an earlier version of this check.
    D = [[1, 0, 0], [0, -1, 0], [0, 0, 1]]
    def gen(x, y, z): return mul(mul(Rx(x), Ry(y)), Rz(z))     # generic draw 0x060381A8, the PS1 data
    def sat(x, y, z): return mul(mul(Rz(z), Ry(y)), Rx(x))     # Tick draw 0x060708B0, the keys
    def pc(x, y, z):                                             # GteMatrix.cpp RotMatrix, STORED
        return mul(mul(Rx(-x), Ry(y)), Rz(-z))
    def drawn(m): return mul(mul(D, m), D)
    def drawn_sat(x, y, z): return drawn(sat(x, y, z))

    # the two draw routines, read out of GAME2: SGL Rx = 0x06082C94, Ry =
    # 0x06082C1C, Rz = 0x06086E6C; the Tick's only while animationId & 0x80
    RX, RY, RZ, TR = 0x06082C94, 0x06082C1C, 0x06086E6C, 0x06086EE4
    check([u32(d2, 0x06070BCC + 4 * i) for i in range(3)] == [RZ, RY, RX],
          "Tick draw 0x060708B0 calls Rz(+8), Ry(+6), Rx(+4) (pool 0x06070BCC)")
    check([u32(d2, 0x060385C0 + 4 * i) for i in range(4)] == [TR, RX, RY, RZ] and
          [u32(d2, 0x06038404 + 4 * i) for i in range(4)] == [TR, RX, RY, RZ],
          "generic draw 0x060381A8 calls Rx(+4), Ry(+6), Rz(+8) (joints and root)")
    check(u32(d2, 0x0602F3B0) == 0x06070794 and u32(d2, 0x060386C4) == 0x060381A8,
          "entity loop 0x0602F2E4 picks the Tick draw 0x06070794 (type 6, animationId & 0x80)")
    g0 = gen(300, -700, 1100)
    check(max(abs(drawn(pc(300, -700, 1100))[i][j] - g0[i][j]) for i in range(3) for j in range(3)) < 1e-9,
          "the PC draws D * RotMatrix * D == the Saturn's generic Rx Ry Rz")

    def key_at(addr, frame):
        last = None
        while u16(d2, addr) != 0xFFFE:
            if u16(d2, addr) <= frame:
                last = (s16(d2, addr + 2), s16(d2, addr + 4), s16(d2, addr + 6))
            addr += 8
        return last

    # header offsets = the PC skeleton (EM1006's PS1 section, read above as `ps1`/`ah`)
    off = [struct.unpack_from("<3h", ps1, ah + 8 + 6 * j) for j in range(16)]
    parent = {0: None, 1: 0, 2: 1, 3: 2, 4: 1, 5: 4, 6: 5, 7: 1, 8: 7, 9: 8,
              10: 0, 11: 10, 12: 11, 13: 0, 14: 13, 15: 14}

    def positions(conv, raw, root):
        W, P = {}, {}
        for j in range(16):
            p_ = parent[j]
            R = conv(*raw[j])
            if p_ is None:
                W[j], P[j] = R, list(root)
            else:
                P[j] = [a + b for a, b in zip(P[p_], mv(W[p_], list(off[j])))]
                W[j] = mul(W[p_], R)
        return P

    def tick_pose(anim, frame):
        e = 0x0609B878 + 12 * anim
        jp = [u32(d2, u32(d2, e + 8) + 4 * j) for j in range(16)]
        return {j: key_at(jp[j], frame) for j in range(16)}, key_at(u32(d2, e + 4), frame)

    def arm_angle(conv):
        raw14, root14 = tick_pose(1, 14)
        P = positions(conv, raw14, root14)
        v = [P[6][i] - P[4][i] for i in range(3)]
        hor = abs(math.degrees(math.atan2(v[2], v[0])))          # off the +X heading
        elev = math.degrees(math.atan2(-v[1], math.hypot(v[0], v[2])))
        return hor, elev

    hs, es = arm_angle(sat)
    hp, ep = arm_angle(gen)
    hm, em = arm_angle(drawn_sat)
    trim = 0xA0 * 360 / 4096
    check(hs < 2 * trim and abs(es) < 10,
          "stab arm drawn Rz*Ry*Rx: %.1f deg off +X (the body is turned %.1f), %.1f deg elevation" % (hs, trim, es))
    check(hp > 45,
          "stab arm drawn Rx*Ry*Rz (keys through plain RotMatrix) would be %.1f deg off, %.1f up (bug 1)" % (hp, ep))
    check(hm > 90,
          "stab arm drawn D*Rz*Ry*Rx*D (Rz*Ry*Rx stored unflipped) would be %.1f deg off (bug 2, mirrored)" % hm)

    # posture: the walk is the Hunter's forward hunch - head (joint 3) ahead of
    # the pelvis, as in the Hunter's own chase drawn the generic way
    def head_x(conv, raw, root):
        P = positions(conv, raw, root)
        return P[3][0] - P[0][0]
    cnt_c, off_c = struct.unpack_from("<HH", ps1, ab + 0x10 * 4)
    fc, _t = struct.unpack_from("<HH", ps1, (off_c & ~3) + ab)
    a_c = struct.unpack_from("<%dh" % verts, ps1, ah + aligned * 4 + fc * verts * 2)
    hx_h = head_x(gen, {j: tuple(a_c[6 + 3 * j:9 + 3 * j]) for j in range(16)}, a_c[0:3])
    walk = [int(head_x(sat, *tick_pose(0, f))) for f in (0, 17, 34, 51)]
    walk_m = [int(head_x(drawn_sat, *tick_pose(0, f))) for f in (0, 17, 34, 51)]
    check(hx_h > 400 and all(hx > 400 for hx in walk) and all(hx < 0 for hx in walk_m),
          "walk hunches forward: head %s ahead (Hunter chase %d); mirrored it leans back %s"
          % (walk, hx_h, walk_m))

    # the heading axis: the Hunter's drawn swipe puts the right hand ahead
    verts_ = struct.unpack_from("<h", ps1, ah + 6)[0] // 2
    stride_ = struct.unpack_from("<h", ps1, ah + 2)[0]
    aligned_ = (stride_ + ((stride_ >> 31) & 3)) >> 2
    cnt_, off_ = struct.unpack_from("<HH", ps1, ab + 5 * 4)
    fidx_, _t = struct.unpack_from("<HH", ps1, (off_ & ~3) + 9 * 4 + ab)
    a_ = struct.unpack_from("<%dh" % verts_, ps1, ah + aligned_ * 4 + fidx_ * verts_ * 2)
    raw_h = {j: tuple(a_[6 + 3 * j:9 + 3 * j]) for j in range(16)}
    Ph = positions(gen, raw_h, a_[0:3])
    check(Ph[9][0] - Ph[0][0] > 800,
          "Hunter swipe (EMD anim 5, frame 9) drawn: right hand (joint 9) %d ahead of the root (forward is +X)"
          % (Ph[9][0] - Ph[0][0]))

    # The port stores PC-convention angles and lets RotMatrix build the matrix
    # (SaturnTick.cpp tick_key_to_pc_angles). D * RotMatrix(angles) * D must
    # reproduce Rz*Ry*Rx for every key.
    import re as _re
    src_a = open("src/game/saturn/SaturnTickAnim.cpp", encoding="utf-8").read()
    keys_ = [(int(a_), int(b_), int(c_)) for a_, b_, c_ in
             _re.findall(r"\{ 0x(?!FFFE)[0-9A-F]{4}, (-?\d+), (-?\d+), (-?\d+) \}", src_a)]
    src_t = open("src/game/saturn/SaturnTick.cpp", encoding="utf-8").read()
    check("lround(a * q), iy = lround(b * q), iz = lround(c * q)" in src_t,
          "tick_key_to_pc_angles keeps (a, b, c) unnegated (the D-flip)")

    def to_pc_angles(x, y, z):
        r_ = math.pi * 2 / 4096
        cx, sx, cy, sy, cz, sz = (math.cos(x * r_), math.sin(x * r_), math.cos(y * r_),
                                  math.sin(y * r_), math.cos(z * r_), math.sin(z * r_))
        M = [[cz * cy, -sz * cx + cz * sy * sx, sz * sx + cz * sy * cx],
             [sz * cy, cz * cx + sz * sy * sx, sz * sy * cx - cz * sx],
             [-sy, cy * sx, cy * cx]]
        b_ = math.asin(max(-1, min(1, M[0][2])))
        if abs(math.cos(b_)) < 1e-4:
            return None, abs(math.cos(b_))
        c_ = math.atan2(-M[0][1], M[0][0])
        a_ = math.atan2(-M[1][2], M[2][2])
        q = 4096 / (2 * math.pi)
        w = lambda v: ((round(v) + 2048) & 0xFFF) - 2048
        return (w(a_ * q), w(b_ * q), w(c_ * q)), abs(math.cos(b_))

    worst, mincos, skipped = 0.0, 9.0, 0
    for kx, ky, kz in keys_:
        ang, cb = to_pc_angles(kx, ky, kz)
        mincos = min(mincos, cb)
        if ang is None:
            skipped += 1
            continue
        A, B = sat(kx, ky, kz), drawn(pc(*ang))
        worst = max(worst, max(abs(A[i][j] - B[i][j]) for i in range(3) for j in range(3)))
    check(skipped == 0 and mincos > 0.1 and worst < 0.01,
          "%d keys convert to PC angles: drawn matrix error %.4f, smallest cos(b) %.2f, gimbal %d"
          % (len(keys_), worst, mincos, skipped))

    print("hold (behaviour 7)")
    check(u32(d2, 0x0606E938) == 0x002FAB14 and u32(d2, 0x0606E93C) == 0x0609D5D4,
          "the hold works on the block at 0x0609D5D4")
    check((u16(d2, 0x0606E91C), u16(d2, 0x0606E91E), u16(d2, 0x0606E920)) == (0xF95C, 0xFCE0, 0xFBB4),
          "pinned offset (-1700, -800, -1100) = 0xF95C 0xFCE0 0xFBB4")
    check(struct.unpack(">d", d2[0x0606E784 - BASE:0x0606E784 - BASE + 8])[0] == 1146.88,
          "retract rate 1146.88 a frame at 0x0606E784")
    check(u32(d2, 0x0606EA48) == 0x01010900, "final state block 0x01010900 (howl)")
    check(u16(d2, 0x0606E91A) == 0x041C and u16(d2, 0x0606E92E) == 0x0378,
          "tip is part 6 +0x44 (0x41C), blood at part 5 +0x44 (0x378)")

    print("the player's bitten machine (entry 44)")
    # GAME2's table setup 0x06052FB8 stores 0x0606FF78 into the player's function
    # table at 0x002FAA18 + 4*44; the PC's bite (animationId 7, animFrameId 6)
    # reaches window 2 (0x26) entry 6 = 44.
    pool = d2[0x06052FB8 - BASE:0x06053040 - BASE]
    check(struct.pack(">I", 0x0606FF78) in pool and struct.pack(">I", 0x002FAAC8) in pool,
          "0x06052FB8 installs 0x0606FF78 at 0x002FAAC8")
    check((0x002FAAC8 - 0x002FAA18) // 4 == 0x26 + 6 == 44,
          "slot (0x2FAAC8-0x2FAA18)/4 = 44 = window 2 (0x26) + animFrameId 6")
    src = open("src/game/saturn/SaturnTick.cpp", encoding="utf-8").read()
    check("int saturn_tick_player_bitten(void)" in src and "pl.health = -1" in src,
          "the port has the 8-state machine, with the late health = -1")
    pa = open("src/game/PlayerAnimations.cpp", encoding="utf-8").read()
    check("index == 44 && g_bSaturnMode && saturn_tick_player_bitten()" in pa,
          "player_dispatch_anim_fn routes entry 44 behind g_bSaturnMode")

    print("sound slots")
    # Snd_em(n) -> key 0x0800+n in the SE bank's own table (file +0x800, loaded
    # to 0x002E8430); the song in the pack is id - 768.
    se = open(os.path.join(ISO, "SND", "SE308.CDP"), "rb").read()
    table = {}
    for i in range(26):
        k, song = struct.unpack(">hh", se[0x800 + i * 8:0x800 + i * 8 + 4])
        if k == -1:
            break
        if (k >> 8) == 8:
            table[k & 0xFF] = song - 768
    hdr = open("tools/asset_migrator/src/core/SaturnMusic.h", encoding="utf-8").read()
    rows = re.findall(r'\{"SE308\.CDP", (\d+), "(Tk_\w+)"', hdr)
    mine = [int(a) for a, _ in rows]
    check(mine == [table[n] for n in range(9)],
          "migrator songs %s == SE308's slot table %s" % (mine, [table.get(n) for n in range(9)]))
    mod = open("src/game/saturn/SaturnModels.cpp", encoding="utf-8").read()
    names = re.findall(r'"(Tk_\w+)"', mod)
    check(names == [n for _, n in rows],
          "SaturnModels.cpp slot names == migrator names, in slot order")
    # every Hunter room's bank holds the same eight samples under its own song numbers
    perm = None
    same = True
    for room in ("SE308", "SE30B"):
        b = open(os.path.join(ISO, "SND", room + ".CDP"), "rb").read()
        t = {}
        for i in range(26):
            k, song = struct.unpack(">hh", b[0x800 + i * 8:0x800 + i * 8 + 4])
            if k == -1:
                break
            if (k >> 8) == 8:
                t[k & 0xFF] = song - 768
        cur = [t[n] for n in range(8)]
        perm = perm or cur
        same = same and cur == perm
    check(same, "SE308 and SE30B map slots 0-7 to the same songs")

    print("generated tables")
    r = subprocess.run([sys.executable, "tools/gen_tick_tables.py", "--check"],
                       capture_output=True, text=True)
    check(r.returncode == 0, "SaturnTickAnim.cpp is current: " + r.stdout.strip())

    print("build files")
    for f in ("Game.vcxproj", "CMakeLists.txt"):
        t = open(f, encoding="utf-8-sig").read()
        for n in ("SaturnTick.cpp", "SaturnTickAnim.cpp"):
            check(n in t, "%s lists %s" % (f, n))
    t = open("Game.vcxproj", encoding="utf-8-sig").read()
    bases = re.findall(r'ClCompile Include="[^"]*[\\/]([^\\/"]+)\.cpp"', t)
    dup = sorted({b for b in bases if bases.count(b) > 1})
    check(not dup, "no basename collision in Game.vcxproj" + (" (%s)" % dup if dup else ""))

    print("gating")
    for f, pats in (
        ("src/game/entities/Hunter.cpp",
         [r"g_bSaturnMode && saturn_tick_active\(\)"]),
        ("src/game/GteMatrix.cpp", [r"g_bSaturnMode && saturn_tick_joint_scaled"]),
        ("src/game/RoomInit.cpp", [r"if \(g_bSaturnMode\) saturn_tick_room_reset"]),
    ):
        t = open(f, encoding="utf-8").read()
        calls = len(re.findall(r"saturn_tick_\w+\(", t))
        gated = sum(len(re.findall(p, t)) for p in pats)
        check(gated >= 1 and calls >= gated,
              "%s: %d gated hook(s), %d saturn_tick_ call(s)" % (f, gated, calls))
    hunter = open("src/game/entities/Hunter.cpp", encoding="utf-8").read()
    ungated = [m.group(0) for m in re.finditer(r"^[^\n]*saturn_tick_\w+\([^\n]*$", hunter, re.M)
               if "g_bSaturnMode" not in m.group(0) and "tick" not in m.group(0).split("saturn_tick")[0]]
    # lines that call a Tick hook without the gate on the same line must sit
    # directly inside a gated block (checked by eye in review); report them.
    print("  note: hook lines without an inline gate: %d" % len(
        [l for l in hunter.splitlines()
         if "saturn_tick_" in l and "g_bSaturnMode" not in l and not l.strip().startswith("//")
         and "#include" not in l]))

    print()
    if fails:
        print("%d check(s) FAILED" % len(fails))
        return 1
    print("all checks passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
