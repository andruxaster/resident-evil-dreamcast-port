#!/usr/bin/env python3
"""
saturn_wardrobe_rdt.py - the Saturn wardrobe (room 11C / 61C) as an RDT patch.

The Saturn does NOT run the wardrobe from its room script. GAME5 replaces
room-action handler 9 with 0x060490B8, which picks an outfit from the player's
yaw and asks one of three questions from its own global message table
(0x0609E9C8 entries 62-64); the yes-answer is its post-action 4 (0x0605CA84),
which writes scenario flags 112/113 and starts event 1. The disc's RDT event 0
is the PS1 one-shot costume script and never runs.

This tool expresses the same thing as SCD, the way the Director's Cut's arrange
wardrobe (Stage8/ROOM81C0, events 0 and 3) does its own two mannequins, so the
port needs no Saturn wardrobe code - only the outfit loader that reads 112/113:

  area 4 (z 1900-3400; examining it faces -z, yaw 1..0x7FF on the Saturn)
      -> event 0: offers outfit 2 (flags 112 + 113), message C0;
                  wearing it -> message C1, back to the default outfit
  area 5 (z 4000-5600; faces +z, the Saturn's other yaw half)
      -> event 3: offers outfit 1 (flag 112 only), message C2;
                  wearing it -> message C1, back to the default outfit

The three texts are the Saturn's, byte for byte (GAME5 0x0609E8BC / 0x0609E8F9 /
0x0609E93A), with the tail `08 02 0A 04 01 00` (yes/no, yes = post-action 4)
swapped for the room-message form `08 00 01 01` (yes/no, no post-action: the
script reads the answer with `state_byte_test 5,0,0`, as event 0 always did).

Layout. The init script changes one byte in place (area 5's event index 0 -> 3,
the DC's own edit). The new message table and event table are written as one
blob inserted at the VAB body (header +0x90), the last block the room loader
keeps: from there on LoadRoomRdt's buffer is reused for the omodel/item slot
records, so nothing may be appended past it. Every file offset at or past the
insertion point moves: the header slots, the camera mask pointers, the omodel
and item {TMD, TIM} pairs and the effect-sprite offsets. The old event and
message blocks stay where they were, unreferenced.

The asset migrator does the same in C++ (core/SaturnWardrobe.cpp); `check`
compares the two.

The Japanese disc ("Bio Hazard") gets its own three questions (GAME5
0x0609EAB7 / 0x0609EAE2 / 0x0609EB0D) and its rooms keep their Japanese
messages 0-2; everything else is the same patch. The migrator writes that disc
to SATURN_JPN.

Usage:
    saturn_wardrobe_rdt.py patch IN.RDT OUT.RDT [jpn]
    saturn_wardrobe_rdt.py check [ROOT]     # ROOT defaults to assets/SATURN
    saturn_wardrobe_rdt.py check --jpn [ROOT]   # assets/SATURN_JPN, JPN disc
"""

import os
import struct
import sys

# The Saturn's three questions (GAME5 0x0609E8BC / 0x0609E8F9 / 0x0609E93A),
# without their `08 02 0A 04 01 00` post-action tail.
SAT_C0 = bytes.fromhex(  # "This costume looks cool. / Will you change your outfit?"
    "02 30 44 45 4f 00 3f 4b 4f 50 51 49 41 00 48 4b 4b 47 4f 00 3f 4b 4b 48 79"
    " 03 00 33 45 48 48 00 55 4b 51 00 3f 44 3d 4a 43 41 00 55 4b 51 4e 02 4b 51"
    " 50 42 45 50 1b")
SAT_C1 = bytes.fromhex(  # "This costume is my favorite. / Will you change your outfit?"
    "02 30 44 45 4f 00 3f 4b 4f 50 51 49 41 00 45 4f 00 49 55 00 42 3d 52 4b 4e"
    " 45 50 41 79 03 00 33 45 48 48 00 55 4b 51 00 3f 44 3d 4a 43 41 00 55 4b 51"
    " 4e 02 4b 51 50 42 45 50 1b")
SAT_C2 = bytes.fromhex(  # "This fits perfectly.   / Will you change your outfit?"
    "02 30 44 45 4f 00 42 45 50 4f 00 4c 41 4e 42 41 3f 50 48 55 79 00 00 00 03"
    " 00 33 45 48 48 00 55 4b 51 00 3f 44 3d 4a 43 41 00 55 4b 51 4e 02 4b 51 50"
    " 42 45 50 1b")
# The Japanese disc's (Bio Hazard GAME5 0x0609EAB7 / 0x0609EAE2 / 0x0609EB0D),
# in the PC Biohazard.exe encoding, without their `02 08 02 0A 04 01 00` tail -
# one line break more than the USA's, dropped so the question ends at its '?'
# like the room's own yes/no (message 1).
SAT_JPN_C0 = bytes.fromhex(  # "とてもカッコイイコスチュームがある / コスチュームチェンジしますか?"
    "02 6a 69 79 ac f6 b0 a8 a8 b0 b3 b7 f4 3b c7 85 57 7f 00 03 00 b0 b3 b7 f4"
    " 3b c7 b7 f1 d4 db 62 75 63 5c 1b")
SAT_JPN_C1 = bytes.fromhex(  # " みたことのあるコスチュームがある / コスチュームチェンジしますか?"
    "02 00 76 66 60 6a 6f 57 7f b0 b3 b7 f4 3b c7 85 57 7f 00 03 00 b0 b3 b7 f4"
    " 3b c7 b7 f1 d4 db 62 75 63 5c 1b")
SAT_JPN_C2 = bytes.fromhex(  # "  サイズのピッタリなものがある / コスチュームチェンジしますか?"
    "02 00 00 b1 a8 dc 6f ea f6 b6 ce 6b 79 6f 85 57 7f 00 03 00 b0 b3 b7 f4 3b"
    " c7 b7 f1 d4 db 62 75 63 5c 1b")
QUESTIONS = {
    'USA': (SAT_C0, SAT_C1, SAT_C2),
    'JPN': (SAT_JPN_C0, SAT_JPN_C1, SAT_JPN_C2),
}
# Where each release's GAME5.PRG (base 0x0602B000) keeps them, and their tail.
GAME5_QUESTIONS = {
    'USA': (0x0609E8BC, bytes.fromhex("08 02 0A 04 01 00")),
    'JPN': (0x0609EAB7, bytes.fromhex("02 08 02 0A 04 01 00")),
}
ROOM_YESNO_TAIL = bytes.fromhex("08 00 01 01")


def region_of_game5(game5):
    """'USA' / 'JPN' from where the questions sit, as the migrator decides."""
    for region, (addr, tail) in GAME5_QUESTIONS.items():
        off = addr - 0x0602B000
        want = b''.join(q + tail for q in QUESTIONS[region])
        if game5[off:off + len(want)] == want:
            return region
    return None

FLAG_OUTFIT_1 = 0x70   # 112, g_ScenarioFlags (SCD bank 0)
FLAG_OUTFIT_2 = 0x71   # 113

# Area 5's room_action_set as the disc has it (handler 9, event 0).
AREA5_ACTION = bytes.fromhex("0d 05 f0 23 a0 0f 10 0e 40 06 09 81 09 00 00 00 00 00")
AREA5_EVENT_BYTE = 14
# Event 0's yes-branch head: if; state_byte_test 5,0,0; state_byte_set 9,<model>.
EVENT0_YES_HEAD = bytes.fromhex("01 12 06 05 00 00 08 09")

# Header pointer slots, RDT +0x48 .. +0x90.
H_EVENTS, H_MESSAGES = 8, 11
H_EFF_INDEX, H_EFF_DATA, H_EFF_SPRITE, H_VAB_BODY = 13, 14, 15, 18


def u16(v):
    return struct.pack('<H', v)


def run_scd(cmds):
    """Event opcode 06: [06][len] then one u16-sized SCD block and a u16 0.
    The block holds the commands and a block_end, padded to 4 bytes."""
    body = bytes(cmds) + b'\x00'
    size = (2 + len(body) + 3) & ~3
    block = u16(size) + body + b'\x00' * (size - 2 - len(body))
    out = bytes([0x06, 2 + size + 2]) + block + u16(0)
    assert out[1] == len(out) and len(out) < 256
    return out


def if_else(conds, then, other):
    """cmd_if pushes (after its header) + L as the false target; cmd_else jumps
    (its own position) + L. Several conditions in one if are an AND."""
    then_part = bytes(conds) + bytes(then)
    els = bytes([0x02, 2 + len(other)]) + bytes(other)
    return bytes([0x01, len(then_part) + 2]) + then_part + els


def bit_test(flag, want_set):
    return bytes([0x04, 0x00, flag, 0x00 if want_set else 0x01])


def bit_op(flag, on):
    return bytes([0x05, 0x00, flag, 0x00 if on else 0x01])


def wardrobe_event(model_byte, wearing_conds, wear_ops, offer_msg, back_msg):
    msg_bits_off = bytes.fromhex("05 06 1F 01 05 06 1D 01 05 06 19 01")
    msg_bits_on = bytes.fromhex("05 06 1F 00 05 06 1D 00 05 06 19 00")
    ask = msg_bits_off + if_else(wearing_conds,
                                 bytes([0x0B, back_msg, 0x45, 0x00]),
                                 bytes([0x0B, offer_msg, 0x45, 0x00]))
    # The disc's wait for the answer: call `state_byte_test 5,2,0x80` until it
    # passes (event 0 +0x28, unchanged).
    wait = bytes.fromhex("FC 06 06 05 02 80 FE FD")
    apply_ = if_else(
        bytes.fromhex("06 05 00 00"),
        bytes([0x08, 0x09, model_byte, 0x00]) +
        if_else(wearing_conds,
                bit_op(FLAG_OUTFIT_1, False) + bit_op(FLAG_OUTFIT_2, False),
                wear_ops) +
        bytes.fromhex("14 00 09 01"),
        msg_bits_on)
    return run_scd(ask) + wait + run_scd(apply_) + b'\x00\xff'


def patch(d, region='USA'):
    d = bytearray(d)
    hdr = list(struct.unpack_from('<19I', d, 0x48))

    # init: area 5 -> event 3
    init = bytes(d[hdr[6]:hdr[7]])
    hits = [i for i in range(len(init)) if init.startswith(AREA5_ACTION, i)]
    if len(hits) != 1:
        raise ValueError("init script: expected one handler-9 area-5 action, found %d" % len(hits))
    d[hdr[6] + hits[0] + AREA5_EVENT_BYTE] = 3

    # events: the old table and its scripts
    ev = hdr[H_EVENTS]
    offs = []
    while True:
        o = struct.unpack_from('<I', d, ev + 4 * len(offs))[0]
        if o == 0:
            break
        offs.append(o)
    if len(offs) != 3:
        raise ValueError("expected the Saturn's 3 event scripts, found %d" % len(offs))
    ends = sorted(offs) + [hdr[9] - ev]
    scripts = [bytes(d[ev + o:ev + ends[ends.index(o) + 1]]) for o in offs]
    head = scripts[0].find(EVENT0_YES_HEAD)
    if head < 0:
        raise ValueError("event 0: yes-branch not found")
    model_byte = scripts[0][head + len(EVENT0_YES_HEAD)]
    if model_byte not in (8, 9):
        raise ValueError("event 0: model byte %d" % model_byte)

    ev0 = wardrobe_event(model_byte,
                         bit_test(FLAG_OUTFIT_1, True) + bit_test(FLAG_OUTFIT_2, True),
                         bit_op(FLAG_OUTFIT_1, True) + bit_op(FLAG_OUTFIT_2, True),
                         0x83, 0x84)
    ev3 = wardrobe_event(model_byte,
                         bit_test(FLAG_OUTFIT_1, True) + bit_test(FLAG_OUTFIT_2, False),
                         bit_op(FLAG_OUTFIT_1, True) + bit_op(FLAG_OUTFIT_2, False),
                         0x85, 0x84)
    new_scripts = [ev0, scripts[1], scripts[2], ev3]

    # messages: the old table and its texts
    mb = hdr[H_MESSAGES]
    count = struct.unpack_from('<H', d, mb)[0] // 2
    moffs = list(struct.unpack_from('<%dH' % count, d, mb))
    mend = hdr[12] - mb
    bounds = moffs + [mend]
    msgs = [bytes(d[mb + bounds[i]:mb + bounds[i + 1]]) for i in range(count)]
    if count != 3:
        raise ValueError("expected 3 room messages, found %d" % count)
    msgs += [q + ROOM_YESNO_TAIL for q in QUESTIONS[region]]

    def pad4(b):
        return b + b'\x00' * (-len(b) % 4)

    mtab = b''
    pos = 2 * len(msgs)
    for m in msgs:
        mtab += u16(pos)
        pos += len(m)
    mblob = pad4(mtab + b''.join(msgs))

    etab = b''
    pos = 4 * (len(new_scripts) + 1)
    for s in new_scripts:
        etab += struct.pack('<I', pos)
        pos += len(s)
    etab += struct.pack('<I', 0)
    eblob = pad4(etab + b''.join(new_scripts))

    blob = mblob + eblob
    x = hdr[H_VAB_BODY]
    if x % 4 or x == 0 or x > len(d):
        raise ValueError("VAB body offset 0x%x" % x)
    n = len(blob)

    def mv(v):
        return v + n if v >= x else v

    # every file offset at/after x moves; table positions come from the
    # header as loaded (`at`), the moved values are written back at the end
    at = list(hdr)
    hdr = [mv(v) if v else 0 for v in hdr]
    for c in range(d[1]):
        p = 0x94 + c * 0x2C
        a, b = struct.unpack_from('<2I', d, p)
        struct.pack_into('<2I', d, p, mv(a), mv(b))
    for tab, cnt in ((at[2], d[2]), (at[3], d[3])):
        for i in range(cnt):
            a, b = struct.unpack_from('<2I', d, tab + 8 * i)
            struct.pack_into('<2I', d, tab + 8 * i, mv(a) if a else 0, mv(b) if b else 0)
    for base, rd in ((at[H_EFF_DATA], True), (at[H_EFF_SPRITE], False)):
        for i in range(8):
            if rd and d[at[H_EFF_INDEX] + i] == 0xFF:
                break
            if base < 4 * 8:
                break
            p = base - 4 * i
            v = struct.unpack_from('<i', d, p)[0]
            if v >= x:
                struct.pack_into('<i', d, p, v + n)
    hdr[H_MESSAGES] = x
    hdr[H_EVENTS] = x + len(mblob)
    struct.pack_into('<19I', d, 0x48, *hdr)
    return bytes(d[:x]) + blob + bytes(d[x:])


def main(argv):
    if len(argv) >= 4 and argv[1] == 'patch':
        region = 'JPN' if len(argv) > 4 and argv[4].lower() == 'jpn' else 'USA'
        out = patch(open(argv[2], 'rb').read(), region)
        open(argv[3], 'wb').write(out)
        print("%s -> %s (%d bytes)" % (argv[2], argv[3], len(out)))
        return 0
    if len(argv) >= 2 and argv[1] == 'check':
        args = argv[2:]
        region = 'USA'
        if '--jpn' in args:
            args.remove('--jpn')
            region = 'JPN'
        root = args[0] if args else os.path.join(
            'assets', 'SATURN_JPN' if region == 'JPN' else 'SATURN')
        disc = os.path.join('assets', 'SATURN_JPN_ISO_CONTENT' if region == 'JPN'
                            else 'SATURN_ISO_CONTENT')
        game5 = os.path.join(disc, 'GAME5.PRG')
        if os.path.exists(game5):
            found = region_of_game5(open(game5, 'rb').read())
            print("%s: %s questions" % (game5, found or "NEITHER release's"))
            if found != region:
                return 1
        bad = 0
        sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
        import saturn_room
        for stage, room in (('1', '1C'), ('6', '1C')):
            for v in '01':
                name = 'ROOM%s%s%s.RDT' % (stage, room, v)
                have = os.path.join(root, 'Stage' + stage, name)
                src = os.path.join(disc, 'STAGE' + stage, name)
                if not os.path.exists(have):
                    print("MISSING %s" % have)
                    bad += 1
                    continue
                want = patch(saturn_room.decode_rdt(src), region)
                got = open(have, 'rb').read()
                # the migrator also relabels TIM transparency; compare the
                # script-bearing ranges and the header, which that never touches
                h = 0x48
                same = got[:0x94] == want[:0x94]
                gh = struct.unpack_from('<19I', got, h)
                for s in (6, H_MESSAGES, H_EVENTS):
                    a = gh[s]
                    same &= got[a:a + 0x200] == want[a:a + 0x200]
                print("%-4s %s" % ('OK' if same else 'DIFF', have))
                bad += 0 if same else 1
        return 1 if bad else 0
    print(__doc__)
    return 2


if __name__ == '__main__':
    sys.exit(main(sys.argv))
