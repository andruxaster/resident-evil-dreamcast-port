#!/usr/bin/env python3
"""verify_saturn_battle.py - check the Saturn Battle Game port against the disc.

`src/game/saturn/SaturnBattle.cpp` transcribes three tables read off the Sega
Saturn disc (docs/SATURN_VERSION_ANALYSIS.md 4d/4e), verifies the Battle BGM's outer
CD_PACK segment table, and checks the wiring that keeps the Battle Game out of
the other two modes:

1. **The arena -> campaign room table** (GAME1 `0x06092B5C`,
   `g_BattleArenaRoomTable`): 15 big-endian u16 of
   `(0-based stage << 8) | room`. Every arena is a copy of one campaign room -
   all fifteen were matched camera by camera - and this is the table the game
   itself carries, so an arena's background, lighting and SE/PCM banks are that
   room's. This reads the table back out of GAME1.PRG and compares it to the
   port's, and checks that each campaign room it names really has a background
   pak in the base tree: a wrong entry is a wrong room's art, and the PC has no
   `Stage8/RC80*.PAK` to fall back to.

2. **The arena spawn table** (GAME5 `0x060958A8`): 15 records of three big-endian
   u16 - direction, x, z. Read back out of GAME5.PRG and compared to the port's.
   NOTE: `SATURN_VERSION_ANALYSIS.md` 4e gives the index as
   `0x060953E0 + 0xAE * stage + 6 * room`, which for the stage byte 7 resolves to
   `0x06095886` - a byte that is not on the table's 6-byte record grid, so it
   cannot be a record start. The block was located by content instead and
   `0x060958A8` is what the game uses; this verifier is what keeps that honest.

 3. **The wiring.** The stage id, the arena count, the unlock's card offset, both
   build systems, and that every call site is behind `g_bSaturnMode` or
   `g_saturnBattleActive` so OG and the Director's Cut can never reach any of it.
   The last one matters most for the run: the arena redirect in `load_room_bg`
   and `Room_LoadEnemySoundBanks` is keyed on `g_saturnBattleActive` alone, so a
    flag left set after a run would put the next campaign game in arena 1's room.
    Hence the explicit `saturn_battle_close_run()` check in `game_start`.

4. **The Battle BGM container.** `BATTLE.CDP`'s CD_PACK table, first tone
   layer's SCSP sample mapping, and two sequence-song headers are checked.

    python tools/verify_saturn_battle.py
"""
import os
import re
import struct
import subprocess
import sys

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

SATURN_SRC = os.path.join(REPO, 'src', 'game', 'saturn', 'SaturnBattle.cpp')
SATURN_HDR = os.path.join(REPO, 'src', 'game', 'saturn', 'SaturnBattle.h')
BIOCARD_HDR = os.path.join(REPO, 'src', 'game', 'BioCard.h')
TITLE_SRC = os.path.join(REPO, 'src', 'game', 'TitleScreen.cpp')
GAMESTART_SRC = os.path.join(REPO, 'src', 'game', 'GameStart.cpp')
ENDING_SRC = os.path.join(REPO, 'src', 'game', 'EndingScreen.cpp')
SAVELOAD_SRC = os.path.join(REPO, 'src', 'game', 'SaveLoadScreen.cpp')
ROOM_SRC = os.path.join(REPO, 'src', 'game', 'Room.cpp')
ROOMINIT_SRC = os.path.join(REPO, 'src', 'game', 'RoomInit.cpp')
SOUND_SRC = os.path.join(REPO, 'src', 'game', 'SoundSystem.cpp')
VCXPROJ = os.path.join(REPO, 'Game.vcxproj')
CMAKELISTS = os.path.join(REPO, 'CMakeLists.txt')

MODULES = os.path.join(REPO, 'assets', 'SATURN_ISO_CONTENT')
BATTLE_CDP = os.path.join(MODULES, 'SND', 'BATTLE.CDP')
ARENA_RDTS = os.path.join(REPO, 'assets', 'SATURN', 'Stage8')
BASE_ASSETS = os.path.join(REPO, 'assets', 'USA')

PRG_BASE = 0x0602B000

# GAME1 0x06092B5C: entry 0 is unused, entries 1..15 are the arenas.
ARENA_TABLE_ADDR = 0x06092B5E
# GAME5 0x060958A8: the stage-8 block of the 6-byte-per-room spawn table.
ARENA_SPAWN_ADDR = 0x060958A8
# GAME5, the global message table: the Battle Game's opening message.
BATTLE_MSG_ADDR = 0x0609E979
# The same message on the Japanese disc (Bio Hazard GAME5.PRG), past its \s\x03.
JPN_BATTLE_MSG_ADDR = 0x0609EB3A
# GAME5, one byte per room: bit 0 = "this arena has enemies".
FIGHT_TABLE_ADDR = 0x060959A0
# GAME5, STAGE8's BGM group and room BGM state, one byte per room each.
ARENA_BGM_GROUP_ADDR = 0x0609ACCC
ARENA_BGM_STATE_ADDR = 0x0609ACDC

ARENA_COUNT = 15
BATTLE_STAGE_ID = 7          # 0-indexed g_stageId; == STAGE8
FIRST_ROOM = 1               # the room byte IS the arena number
LAST_ROOM = 15
UNLOCK_CARD_OFFSET = 0x20F   # BioCardLayout; the Saturn's save byte 0xBF

failures = []
skips = []


def ok(msg):
    print('  ok   %s' % msg)


def fail(msg):
    failures.append(msg)
    print('  FAIL %s' % msg)


def check(cond, msg):
    if cond:
        ok(msg)
    else:
        fail(msg)
    return cond


def module_path(name):
    p = os.path.join(MODULES, name)
    return p if os.path.isfile(p) else None


def read_be16(path, addr, count):
    """`count` big-endian u16 at RAM address `addr` in a .PRG overlay."""
    off = addr - PRG_BASE
    with open(path, 'rb') as fh:
        data = fh.read()
    if off < 0 or off + count * 2 > len(data):
        return None
    return list(struct.unpack_from('>%dH' % count, data, off))


def read_cdp_segments(path):
    """Read CD_PACK Ver.1.0's outer BE32 segment records."""
    with open(path, 'rb') as fh:
        data = fh.read()
    if len(data) < 0x24 or data[:16] != b'CD_PACK Ver.1.0\0':
        return None
    marker = struct.unpack_from('>I', data, 0x10)[0]
    segments = []
    offset = 0x14
    while offset + 0x10 <= len(data):
        source, size, destination, continuation = struct.unpack_from('>4I', data, offset)
        if source > len(data) or size > len(data) - source:
            return None
        segments.append((source, size, destination, continuation))
        if continuation == 0xFFFFFFFF:
            return marker, segments, data
        offset += 0x10
    return None


def check_battle_cdp():
    print('Battle BGM CD_PACK container')
    if not check(os.path.isfile(BATTLE_CDP), 'BATTLE.CDP is present on the Saturn image'):
        return
    parsed = read_cdp_segments(BATTLE_CDP)
    if not check(parsed is not None, 'CD_PACK signature, bounds and terminator are valid'):
        return
    marker, segments, data = parsed
    check(marker == 0, 'CD_PACK reserved marker is zero')
    expected = [
        (0x800, 0x24196, 0x25A55000, 1),
        (0x25000, 0x14E2, 0x25A7A000, 0xFFFFFFFF),
    ]
    check(segments == expected,
          'BATTLE.CDP source ranges and SCSP destinations match the driver table')
    tone_offset, tone_size = segments[0][:2]
    tone = data[tone_offset:tone_offset + tone_size]
    mixer_offset, velocity_offset = struct.unpack_from('>2H', tone, 0)
    voice_count = (mixer_offset - 8) // 2 if mixer_offset >= 8 else 0
    check(mixer_offset == 0x1A and velocity_offset == 0x2C and voice_count == 9,
          'the first payload has the expected Sega tone-bank header')
    voice_offset = struct.unpack_from('>H', tone, 8 + 2)[0]
    check(voice_offset == 0x68, 'tone program 1 points to payload offset 0x68')
    voice = tone[voice_offset:voice_offset + 4]
    check(len(voice) == 4 and voice[2] == 1,
          'tone program 1 has the recovered layer-count field')
    layer = tone[voice_offset + 4:voice_offset + 0x24]
    check(len(layer) == 0x20 and (layer[0], layer[1]) == (0x00, 0x28),
          'tone program 1 has a key layer for notes 0x00..0x28')
    if len(layer) == 0x20:
        sample_ref = struct.unpack_from('>I', layer, 2)[0]
        loop_start, loop_end = struct.unpack_from('>2H', layer, 6)
        sample_address = 0x55000 + sample_ref
        control = (sample_address >> 16) & 0xFF
        scsp_sa = ((control & 0x0F) << 16) | (sample_address & 0xFFFF)
        source_sample = tone_offset + (scsp_sa - 0x55000)
        sample_bytes = (loop_end + 1) * (1 if control & 0x10 else 2)
        check(sample_ref == 0x00206660 and (loop_start, loop_end) == (0x1582, 0x162A),
              'tone layer sample reference and loop points match the sound driver')
        check(scsp_sa == 0x5B660 and (control & 0x10) == 0 and
              ((control >> 5) & 3) == 1,
              'SCSP address selects signed 16-bit PCM with normal looping')
        check(source_sample == 0x6E60 and
              data[source_sample:source_sample + 8] == bytes.fromhex('0000ffe4ffe2ffe5') and
              source_sample + sample_bytes <= tone_offset + tone_size,
              'the resolved sample bytes and LEA fit inside the first CDP segment')
    second_layer = tone[voice_offset + 0x24:voice_offset + 0x44]
    check(len(second_layer) == 0x20 and
          (second_layer[0], second_layer[1]) == (0x29, 0x7F),
          'tone program 1 has a second key layer for notes 0x29..0x7F')
    if len(second_layer) == 0x20:
        second_ref = struct.unpack_from('>I', second_layer, 2)[0]
        second_lsa, second_lea = struct.unpack_from('>2H', second_layer, 6)
        second_source = 0x55000 + second_ref
        second_control = (second_source >> 16) & 0xFF
        second_sa = ((second_control & 0x0F) << 16) | (second_source & 0xFFFF)
        second_file_offset = tone_offset + (second_sa - 0x55000)
        second_bytes = (second_lea + 1) * (1 if second_control & 0x10 else 2)
        check(second_ref == 0x002092BA and
              (second_lsa, second_lea) == (0x120D, 0x1260),
              'the second tone layer has its recovered sample reference and loop')
        check(second_sa == 0x5E2BA and (second_control & 0x10) == 0 and
              ((second_control >> 5) & 3) == 1 and second_file_offset == 0x9ABA and
              second_file_offset + second_bytes <= tone_offset + tone_size,
              'the second sample is signed 16-bit PCM with normal looping')

    layer_count = 0
    sample_formats = set()
    all_layers_fit = True
    for program in range(voice_count):
        program_offset = struct.unpack_from('>H', tone, 8 + program * 2)[0]
        minus_one = tone[program_offset + 2]
        if minus_one >= 0x80:
            continue
        count = minus_one + 1
        for layer_index in range(count):
            layer_offset = program_offset + 4 + layer_index * 0x20
            source_ref = struct.unpack_from('>I', tone, layer_offset + 2)[0]
            _, loop_end = struct.unpack_from('>2H', tone, layer_offset + 6)
            source = 0x55000 + source_ref
            control = (source >> 16) & 0xFF
            sample_address = ((control & 0x0F) << 16) | (source & 0xFFFF)
            bank_offset = sample_address - 0x55000
            bytes_per_sample = 1 if control & 0x10 else 2
            bytes_through_loop = (loop_end + 1) * bytes_per_sample
            layer_count += 1
            sample_formats.add(bytes_per_sample)
            if bank_offset < 0 or bank_offset + bytes_through_loop > tone_size:
                all_layers_fit = False
    check(layer_count == 15, 'all nine programs contain 15 tone layers')
    check(sample_formats == {1, 2}, 'the tone bank uses both SCSP PCM widths')
    check(all_layers_fit, 'all tone-layer sample ranges fit the first CDP segment')

    sequence_offset, sequence_size = segments[1][:2]
    sequence = data[sequence_offset:sequence_offset + sequence_size]
    if len(sequence) >= 10:
        song_count = struct.unpack_from('>H', sequence, 0)[0]
        song_offsets = [struct.unpack_from('>I', sequence, 2 + i * 4)[0]
                        for i in range(song_count)]
        check(song_count == 2 and song_offsets == [0x0A, 0x0EAE],
              'the second payload has two sequence-bank song offsets')
        song0 = sequence[song_offsets[0]:]
        song1 = sequence[song_offsets[1]:]
        check(len(song0) >= 0x18 and struct.unpack_from('>4H', song0, 0) ==
              (0x180, 2, 0x18, 0x10),
              'sequence song 0 header matches PPQN, tempo count, event and loop offsets')
        check(struct.unpack_from('>2I', song0, 0x10) == (0x2FFC, 0x000C0BDE),
              'sequence song 0 initial tempo record matches')
        check(len(song1) >= 0x10 and struct.unpack_from('>4H', song1, 0) ==
              (480, 1, 0x10, 0x08),
              'sequence song 1 header matches PPQN, tempo count, event and loop offsets')
        check(struct.unpack_from('>2I', song1, 0x08) == (0, 0x00051615),
              'sequence song 1 initial tempo record matches')


def port_table(src, name):
    """The C initialiser `name` in `src` as a list of ints."""
    m = re.search(re.escape(name) + r'\[[^\]]*\]\s*=\s*\{(.*?)\n\s*\};', src, re.S)
    if not m:
        return None
    body = re.sub(r'//.*', '', m.group(1))
    vals = [int(v, 0) for v in re.findall(r'0x[0-9A-Fa-f]+|\b\d+\b', body)]
    return vals


def port_spawn_table(src):
    """The C 2-D initialiser kArenaSpawn as a list of (dir, x, z)."""
    m = re.search(r'kArenaSpawn\[[^\]]*\]\[3\]\s*=\s*\{(.*?)\n\s*\};', src, re.S)
    if not m:
        return None
    body = re.sub(r'//.*', '', m.group(1))
    rows = re.findall(r'\{([^}]*)\}', body)
    out = []
    for r in rows:
        vals = [int(v, 0) for v in re.findall(r'0x[0-9A-Fa-f]+|\b\d+\b', r)]
        if len(vals) == 3:
            out.append(tuple(vals))
    return out


def check_arena_table(saturn_src):
    print('arena -> campaign room table (GAME1 0x%08X)' % ARENA_TABLE_ADDR)
    port = port_table(saturn_src, 'kArenaCampaignRoom')
    if not check(port is not None and len(port) == 1 + ARENA_COUNT,
                 'the port carries %d entries (1 unused + %d arenas)'
                 % (1 + ARENA_COUNT, ARENA_COUNT) if port else
                 'read kArenaCampaignRoom'):
        return

    game1 = module_path('GAME1.PRG')
    if game1 is None:
        skips.append('assets/SATURN_ISO_CONTENT/GAME1.PRG')
        print('  SKIP no GAME1.PRG, table not compared against the disc')
        return
    disc = read_be16(game1, ARENA_TABLE_ADDR, ARENA_COUNT)
    if not check(disc is not None, 'read the table out of GAME1.PRG'):
        return

    bad = [(i, port[i], disc[i - 1])
           for i in range(1, ARENA_COUNT + 1) if port[i] != disc[i - 1]]
    if bad:
        for idx, got, want in bad:
            fail('arena %d: port has stage %d room 0x%02X, the disc has stage %d'
                 ' room 0x%02X' % (idx, got >> 8, got & 0xFF, want >> 8, want & 0xFF))
    else:
        ok('all %d entries match GAME1.PRG' % ARENA_COUNT)

    # Every campaign room an arena names must have a background pak in the base
    # tree - the PC ships no Stage8 backgrounds, so this is the only source. The
    # name is RC<stage><roomHi><roomLo><camera>.PAK (the port's g_bgPathTemplate,
    # 0x004c2078), so camera 0 is the last digit.
    print('   and the campaign room each arena names has its own background')
    for idx in range(1, ARENA_COUNT + 1):
        pair = port[idx]
        stage, room = pair >> 8, pair & 0xFF
        pak = os.path.join(BASE_ASSETS, 'Stage%d' % (stage + 1),
                           'RC%d%X%X0.PAK' % (stage + 1, room >> 4, room & 0xF))
        if os.path.isfile(pak):
            ok('arena %2d -> stage %d room 0x%02X: Stage%d/RC%d%X%X0.PAK'
               % (idx, stage, room, stage + 1, stage + 1, room >> 4, room & 0xF))
        else:
            fail('arena %2d -> stage %d room 0x%02X: %s is missing, so the arena'
                 ' would load no background at all'
                 % (idx, stage, room,
                    os.path.relpath(pak, REPO).replace('\\', '/')))


def check_spawn_table(saturn_src):
    print('arena spawn table (GAME5 0x%08X)' % ARENA_SPAWN_ADDR)
    port = port_spawn_table(saturn_src)
    if not check(port is not None and len(port) == ARENA_COUNT,
                 'the port carries %d spawn records' % ARENA_COUNT):
        return

    game5 = module_path('GAME5.PRG')
    if game5 is None:
        skips.append('assets/SATURN_ISO_CONTENT/GAME5.PRG')
        print('  SKIP no GAME5.PRG, spawn table not compared against the disc')
        return
    off = ARENA_SPAWN_ADDR - PRG_BASE
    with open(game5, 'rb') as fh:
        data = fh.read()
    if off + ARENA_COUNT * 6 > len(data):
        fail('0x%08X is outside GAME5.PRG' % ARENA_SPAWN_ADDR)
        return
    disc = [struct.unpack_from('>3H', data, off + 6 * i) for i in range(ARENA_COUNT)]

    bad = [(i + 1, port[i], disc[i]) for i in range(ARENA_COUNT) if port[i] != disc[i]]
    if bad:
        for idx, got, want in bad:
            fail('arena %2d spawn: port (%d, %d, %d), the disc (%d, %d, %d)'
                 % ((idx,) + got + want))
    else:
        ok('all %d spawn records (direction, x, z) match GAME5.PRG' % ARENA_COUNT)

    # The spawn has to be inside the room it belongs to. Coarse but decisive: a
    # record whose direction is not a multiple of 0x400 has been transposed.
    odd = [i + 1 for i, r in enumerate(disc) if r[0] % 0x400]
    if odd:
        print('   note: arena(s) %s have a direction that is not a multiple of'
              ' 0x400 (the disc has one, 0x3E0) - transcribed verbatim'
              % ', '.join(str(i) for i in odd))
    else:
        ok('every direction is a multiple of 0x400')


ITEM_IDS = {
    'ITEM_KNIFE': 0x01, 'ITEM_BERETTA': 0x02, 'ITEM_SHOTGUN': 0x03,
    'ITEM_COLT_PYTHON_MAG': 0x05, 'ITEM_BAZOOKA_EXPLOSIVE': 0x07,
    'ITEM_CLIP': 0x0B, 'ITEM_SHELLS': 0x0C, 'ITEM_MAGNUM_ROUNDS': 0x0E,
    'ITEM_EXPLOSIVE_ROUNDS': 0x10, 'ITEM_ACID_ROUNDS': 0x11,
    'ITEM_FLAME_ROUNDS': 0x12, 'ITEM_FIRST_AID_SPRAY': 0x41,
}


def port_item_list(src, name):
    """A `{ ITEM_x, qty }` initialiser as a list of (id, qty)."""
    m = re.search(re.escape(name) + r'\[\]\s*=\s*\{(.*?)\n\};', src, re.S)
    if not m:
        return None
    body = re.sub(r'//.*', '', m.group(1))
    out = []
    for ident, qty in re.findall(r'\{\s*(ITEM_\w+)\s*,\s*(\d+)\s*\}', body):
        if ident not in ITEM_IDS:
            return None
        out.append((ITEM_IDS[ident], int(qty)))
    return out


def disc_item_list(data, addr):
    """GAME5's zero-terminated (id, qty) list at `addr`."""
    off = addr - PRG_BASE
    out = []
    while data[off] != 0:
        out.append((data[off], data[off + 1]))
        off += 2
    return out


def check_item_box_loadout(saturn_src):
    """The Battle Game's loadout is the ITEM BOX, and the inventory is empty.

    battle_run_init (GAME5 0x06088698) clears the 48 two-byte slots at
    0x002F8724 - the item box; the inventory is 0x002F8784, the block after it -
    and copies the character's list into them. GAME5's SetInitialItems
    (0x06045188) skips its knife/spray list when battle bit 31 is set and zeroes
    the inventory. The first port put the list in the inventory and left the
    loaded save's box alone: wrong weapons in hand, campaign items in the box.
    """
    print('item-box loadout (GAME5 battle_run_init 0x06088698)')
    fn = re.search(r'static void battle_apply_loadout\(void\)\s*\{(.*?)\n\}',
                   saturn_src, re.S)
    body = fn.group(1) if fn else ''
    check('g_itemboxSlots[i].Id = list[i].id;' in body,
          'the list is copied into the item box')
    check(re.search(r'for \(int i = 0; i < 48; i\+\+\)\s*\{\s*g_itemboxSlots\[i\]\.Id = ITEM_NONE;',
                    body) is not None,
          'all 48 box slots are cleared first')
    check('g_ItemsSlots[i].Id = ITEM_NONE;' in body and 'g_TotalHeldItems = 0;' in body
          and 'g_ItemsSlots[n]' not in body,
          'the inventory is emptied, not filled')
    check('g_EquippedItemId = 0;' in body, 'nothing is equipped')
    check('healthStatusFlags &= 0x9D' in body, 'health-status byte &= 0x9D (0x060886CA)')
    check('jill * -44 + 140' in body, 'HP starts at the maximum, 140 / 96')

    game5 = module_path('GAME5.PRG')
    if game5 is None:
        skips.append('assets/SATURN_ISO_CONTENT/GAME5.PRG')
        print('  SKIP no GAME5.PRG, lists not compared against the disc')
        return
    data = open(game5, 'rb').read()
    for name, addr in (('kChrisItemBox', 0x06088660), ('kJillItemBox', 0x06088677)):
        port = port_item_list(saturn_src, name)
        disc = disc_item_list(data, addr)
        check(port == disc, '%s matches GAME5 0x%08X (%d entries)' % (name, addr, len(disc))
              if port == disc else '%s: port %s, disc %s' % (name, port, disc))


VIRTUAL_DOOR_ADDR = 0x06095950


def check_virtual_doors(saturn_src):
    """The arena warp is a door transition with a 5-byte virtual door record.

    GAME5's room_transition_load (0x0602DBBC) has a battle branch that writes
    kArenaVirtualDoor[arena]'s five bytes into the globals a door record's
    +0x08/+0x09/+0x0A/+0x0B fields go to, then runs the ordinary door path. The
    first port called room_set() directly from the game loop: no door
    animation, no door sound, and the player's Y / animation left as they were.
    """
    print('arena warp virtual doors (GAME5 0x%08X)' % VIRTUAL_DOOR_ADDR)
    m = re.search(r'kArenaVirtualDoor\[[^\]]*\]\[5\]\s*=\s*\{(.*?)\n\};', saturn_src, re.S)
    rows = []
    if m:
        body = re.sub(r'//.*', '', m.group(1))
        for r in re.findall(r'\{([^}]*)\}', body):
            rows.append(tuple(int(v, 0) for v in re.findall(r'0x[0-9A-Fa-f]+|\b\d+\b', r)))
    if not check(len(rows) == 1 + ARENA_COUNT and all(len(r) == 5 for r in rows),
                 'the port carries 16 five-byte records'):
        return

    game5 = module_path('GAME5.PRG')
    if game5 is None:
        skips.append('assets/SATURN_ISO_CONTENT/GAME5.PRG')
        print('  SKIP no GAME5.PRG, virtual doors not compared against the disc')
    else:
        data = open(game5, 'rb').read()
        off = VIRTUAL_DOOR_ADDR - PRG_BASE
        disc = [tuple(data[off + 5 * i: off + 5 * i + 5]) for i in range(1 + ARENA_COUNT)]
        bad = [i for i in range(1 + ARENA_COUNT) if rows[i] != disc[i]]
        check(not bad, 'all 16 records match GAME5.PRG' if not bad
              else 'records %s differ from GAME5.PRG' % bad)
        check(all(r[2] < 0x22 and r[1] < 15 for r in disc),
              'every door type has a .dor file and every sound a room-sfx bank')

    # The record build lives in battle_warp(), which both the sequence's
    # arena + 1 step and the debug menu's arena jump go through, so the
    # transition itself is checked once and the two callers are checked for
    # WHICH arena they hand it (0x0602DDF4 reads the row before the room is
    # incremented, which is why the sequence indexes by the arena LEFT).
    warp = re.search(r'static void battle_warp\([^)]*\)\s*\{(.*?)\n\}', saturn_src, re.S)
    body = warp.group(1) if warp else ''
    check('door_begin_transition(rec);' in body and 'room_set()' not in body,
          'the warp hands a door record to door_begin_transition, not room_set()')
    check('rec[0x0D] = arena;' in body and 'kArenaSpawn[arena - 1]' in body,
          'the destination is the arena byte and the arrival comes from the spawn table')
    check('*(short*)(rec + 0x10)          = 0;' in body, 'the arrival Y is 0')
    check('(virtualDoor[4] & 0xC0) | (virtualDoor[3] & 0x3F)' in body,
          'byte [4] is the +0x0B flags, byte [3] the entry camera')
    check('s_battle.state = 2;' in body, 'the run waits in state 2 while the door runs')

    adv = re.search(r'static void battle_advance_room\(void\)\s*\{(.*?)\n\}', saturn_src, re.S)
    abody = adv.group(1) if adv else ''
    check('battle_warp((unsigned char)next, kArenaVirtualDoor[arena]);' in abody and
          'arena + 1' in abody,
          'the sequence steps to arena + 1 through kArenaVirtualDoor[arena]'
          ' (the arena being LEFT, 0x0602DDF4)')

    dbg = re.search(r'int saturn_battle_debug_warp\(int arena\)\s*\{(.*?)\n\}',
                    saturn_src, re.S)
    dbody = dbg.group(1) if dbg else ''
    check('!g_bSaturnMode || !g_saturnBattleActive' in dbody and
          'arena < SATURN_BATTLE_FIRST_ROOM' in dbody and
          'arena > SATURN_BATTLE_LAST_ROOM' in dbody,
          'the debug jump is refused outside a run and outside arenas 1..15')
    check('battle_warp((unsigned char)arena, kArenaVirtualDoor[arena]);' in dbody,
          'the debug jump goes through the same door record and spawn record')
    check('s_battle.state >= 3' in dbody and 's_battleFinished' in dbody,
          'the debug jump is refused during the end sequence')

    menu = open(os.path.join(REPO, 'src', 'game', 'DebugMenu.cpp'),
                encoding='utf-8', errors='replace').read()
    check('saturn_battle_debug_warp(s_dbgRoom);' in menu and
          'DebugBattleRunActive() && stage == SATURN_BATTLE_STAGE_ID' in menu,
          'the debug menu lists STAGE8 only during a run and jumps through the run')
    check('saturn_battle_end_run();' in menu,
          'leaving the arenas for a campaign stage ends the run first')


YAWN_SRC = os.path.join(REPO, 'src', 'game', 'entities', 'Yawn.cpp')


def check_yawn():
    """Arena 5's Yawn: 350 HP and no flee in the Battle Game ONLY.

    Both decisions are `and #0x80000000 ; tst ; bf` on the battle bit, and `bf`
    branches when the bit is SET. They were first read the other way round,
    which gave the Saturn's campaign Yawn 350 HP and no flee. The per-hit damage
    (flat 15) and the bite (flat 28) have no flag test on the Saturn at all.
    """
    print('arena 5 Yawn (GAME5 0x0607F29A / 0x06080048)')
    src = open(YAWN_SRC, encoding='utf-8', errors='replace').read()
    game5 = module_path('GAME5.PRG')
    if game5 is not None:
        data = open(game5, 'rb').read()
        def w(addr):
            return struct.unpack_from('>H', data, addr - PRG_BASE)[0]
        # 0607F2A0 bf/s -> 0x0607F2E8 (= 0x015E); fall-through 0x0BEA.
        check(w(0x0607F2A0) == 0x8F22 and w(0x0607F2B8) == 0x0BEA
              and w(0x0607F4C6) == 0x015E,
              'GAME5: the bit-SET branch loads 0x015E, the fall-through 0x0BEA')
        check(w(0x06080062) == 0x8B02 and w(0x06080086) == 0x0AEF,
              'GAME5: the flee is skipped when the battle bit is set')
        check(w(0x0607F8CC) == 0x73F1 and w(0x060805EE) == 0x71E4,
              'GAME5: segment hit is `add #-15`, bite is `add #-28`')
        # The spiders' venom splash (the PC's effect_projectile_hit_check,
        # 0x0043bd90): ids 3/4, then the battle bit - `bf` over the two rand()
        # calls and the `+0xDC |= 2` / `+0x174 = 0x96` pair when it is SET.
        check(struct.unpack_from('>I', data, 0x06049E74 - PRG_BASE)[0] == 0x060A67D0
              and struct.unpack_from('>I', data, 0x06049E78 - PRG_BASE)[0] == 0x80000000
              and w(0x06049D4A) == 0xD14A and w(0x06049D54) == 0x2228
              and w(0x06049D56) == 0x8B16
              and w(0x06049D78) == 0xE002 and w(0x06049D7A) == 0x220B
              and w(0x06049D82) == 0xE196,
              'GAME5: the spider venom (+0xDC |= 2, timer 0x96) is skipped when '
              'the battle bit is set')
    fx = open(os.path.join(REPO, 'src', 'game', 'EffectSystem.cpp'),
              encoding='utf-8', errors='replace').read()
    check(re.search(r'g_EnemiesList\[0\]\.id == 4\)\) &&\s*'
                    r'!\(g_bSaturnMode && g_saturnBattleActive\)\) \{[^}]*?'
                    r'healthStatusFlags \|= 2;', fx, re.S) is not None,
          'the spider venom does not poison in the Battle Game')
    check('} else if (g_bSaturnMode && g_saturnBattleActive) {\n' in src
          and 'ew(ENTITY, 0x88) = 0x015e;' in src,
          '350 HP only for the Battle Game')
    check("ew(ENTITY, 0x88) < 0xaf0 && !(g_bSaturnMode && g_saturnBattleActive)" in src,
          'the campaign Yawn still flees in Saturn mode')


def check_arena_extras(saturn_src):
    """Arena 12's Zombie Wesker, arena 15's gold Tyrant, and the results.

    All four are code in GAME5 / RANKING, not room data. Each port constant is
    pinned to the instruction word it was read from.
    """
    print('arena extras and results (GAME5 / RANKING.PRG)')
    game5 = module_path('GAME5.PRG')
    ranking = module_path('RANKING.PRG')
    if game5 is None or ranking is None:
        skips.append('assets/SATURN_ISO_CONTENT/GAME5.PRG + RANKING.PRG')
        print('  SKIP no GAME5.PRG / RANKING.PRG')
    else:
        g5 = open(game5, 'rb').read()
        rk = open(ranking, 'rb').read()

        def w(data, addr):
            return struct.unpack_from('>H', data, addr - PRG_BASE)[0]

        def l(data, addr):
            return struct.unpack_from('>I', data, addr - PRG_BASE)[0]
        check(w(g5, 0x0603C9EE) == 0x880C and w(g5, 0x0603C9F2) == 0xE117
              and w(g5, 0x0603C9F8) == 0xE109 and w(g5, 0x0603CAAE) == 0x2190,
              'GAME5 0x0603C9E8: arena 12 slot 0 -> id 0x17 + behaviour 9, id restored')
        table = [l(g5, 0x0609F30C + 4 * i) for i in range(19)]
        check(table[18] == table[9] == 0x0605EEA4,
              'GAME5 zombie table: behaviour 9 ([18]) is the chase ([9])')
        check(w(g5, 0x0605E968) == 0x8809 and w(g5, 0x0605E972) == 0x311C,
              'GAME5 0x0605E968: behaviour 9 doubles the zombie HP')
        check(bytes(g5[0x0609F3F8 - PRG_BASE:0x0609F3F8 - PRG_BASE + 10])
              == bytes([12, 12, 9, 9, 12, 12, 12, 12, 12, 12]),
              'GAME5 0x0609F3F8: the one zombie damage row, behaviour 9 = 12')
        check(w(g5, 0x0603BD10) == 0x880F and w(g5, 0x0603BD18) == 0xE1FB
              and w(g5, 0x0603BD1A) == 0xE703 and w(g5, 0x0603BD1C) == 0xE604,
              'GAME5 0x0603BD0A: arena 15 tints every CLUT +4 / +3 / -5')
        check(w(g5, 0x06079A8E) == 0x02BC and w(g5, 0x06079A1E) == 0x8918,
              'GAME5 0x06079A12: the battle Tyrant opens at 0x02BC = 700 HP')
        check(w(g5, 0x06088E7C) == 0x018F and w(rk, 0x0602D61A) == 0x00FA
              and l(rk, 0x0602D65C) == 0x00034B84,
              'the orbit lasts 0x18F frames; RANKING defaults 250 - 20*i, time 0x34B84')

    check('e->id = SATURN_ZOMBIE_WESKER_TYPE;' in saturn_src
          and 'e->behavior_flags = SATURN_ZOMBIE_WESKER_BEHAV;' in saturn_src,
          'the port swaps arena 12 slot 0 to em1017 with behaviour 9')
    roominit = open(ROOMINIT_SRC, encoding='utf-8', errors='replace').read()
    check('saturn_battle_model_override(ENTITY, (int)i)' in roominit
          and 'if (wesker) ENTITY->id = originalId;' in roominit,
          'RoomInit restores the zombie id after loading the Wesker model')
    check('int r = (v & 31) + 4;' in saturn_src and 'int g = ((v >> 5) & 31) + 3;' in saturn_src
          and 'int b = ((v >> 10) & 31) - 5;' in saturn_src,
          'the port tint is +4 / +3 / -5 with the STP bit kept')
    check("rec.score = (unsigned short)(250 - 20 * i);" in saturn_src
          and 'memcpy(rec.name, "CAP", 4);' in saturn_src,
          'the port ranking defaults are RANKING.PRG\'s')


def check_results_and_arenas_2(saturn_src):
    """Second play-test: Wesker's head, arena omodels, the results screens."""
    print('results art, Wesker head, arena omodels (GAME5 / RANKING / BATTLE.BIN)')
    game5, ranking = module_path('GAME5.PRG'), module_path('RANKING.PRG')
    battle = module_path('BATTLE.BIN')
    if game5 and ranking and battle:
        g5, rk, bb = (open(x, 'rb').read() for x in (game5, ranking, battle))

        def l(data, addr):
            return struct.unpack_from('>I', data, addr - PRG_BASE)[0]

        def w(data, addr):
            return struct.unpack_from('>H', data, addr - PRG_BASE)[0]
        check(l(g5, 0x0604AECC) == 0x00FF0F00 and l(g5, 0x0604AED0) == 0x00010900,
              'GAME5 0x0604AE5A: id 1 + behaviour 9 (Wesker) is exempt from the head explode')
        row = 0x0609CD9C - PRG_BASE + 5 * 0x10E
        rows = [bytes(g5[row + a * 9: row + a * 9 + 9]) for a in range(16)]
        check(all(r == bytes(9) for a, r in enumerate(rows) if a not in (1, 6, 11))
              and all(r[0] == 0x1F and r[1:] == bytes(8) for a, r in enumerate(rows) if a in (1, 6, 11)),
              'GAME5 OB table row 5: no omodel in any fight arena, OB31 in the rest rooms')
        check(w(rk, 0x0602C6BC) == 0x4009 and w(rk, 0x0602C6BE) == 0xC902,
              'RANKING 0x0602C6BA: the name/letter palette is (counter >> 2) & 2')
        check(bb[0x10000:0x10002] == b'BM' and bb[0:4] == b'X68 '
              and struct.unpack_from('>I', bb, 0x24)[0] == 0x1430
              and struct.unpack_from('>I', bb, 0x28)[0] == 0x1630,
              'BATTLE.BIN: SSCONV fonts/palette at 0x30/0x1430/0x1630, BMP backdrop at 0x10000')
    else:
        skips.append('GAME5.PRG / RANKING.PRG / BATTLE.BIN')
    wd = open(os.path.join(REPO, 'src', 'game', 'WeaponDamage.cpp'), encoding='utf-8').read()
    check('enemy->id == ENEMY_ZOMBIE_NAKED' in wd and 'goto hit_billboards;' in wd,
          'the port skips the head explode for Zombie Wesker')
    ro = open(os.path.join(REPO, 'src', 'game', 'saturn', 'SaturnRoomObjects.cpp'), encoding='utf-8').read()
    check('saturn_battle_omodel_has_model' in ro, 'the port hides the fight arenas\' omodels')
    for needle, label in (
        ('#define SAT_FONT_A_OFF        0x0030', 'font A offset'),
        ('#define SAT_FONT_PAL_OFF      0x1430', 'font palette offset'),
        ('#define SAT_FONT_B_OFF        0x1630', 'font B offset'),
        ('#define SAT_BMP_OFF           0x10000', 'backdrop BMP offset'),
        ('return (int)((s_resultsFrame >> 2) & 2);', 'the white/yellow flicker'),
        ('bgm_play_group_channel(SATURN_BGM_GROUP_RANKING, 0);', 'Bgm_5a (ranking) plays'),
    ):
        check(needle in saturn_src, 'port: ' + label)
    body = re.search(r'void saturn_ranking_state\(void\)\s*\{(.*?)\n\}', saturn_src, re.S)
    b = body.group(1) if body else ''
    # The music starts in battle_results_begin(), the prologue the finished run
    # and the death both share; the score screen (battle_results_finished) and
    # the death's table (battle_results_dead) must come after it.
    begin = re.search(r'void battle_results_begin\(void\)\s*\{(.*?)\n\}', saturn_src, re.S)
    check(begin is not None and 'SATURN_BGM_GROUP_RANKING' in begin.group(1) and
          0 <= b.find('battle_results_begin()') < b.find('battle_results_finished(') and
          b.find('battle_results_begin()') < b.find('battle_results_dead('),
          'the ranking music starts with the score screen')
    # saturn_ranking_state runs after saturn_battle_close_run() has cleared the
    # run, so Bgm_5a (ranking) must resolve before the battle-active gate - behind it
    # the results screens were silent.
    names = re.search(r'const char\* saturn_battle_bgm_name\([^)]*\)\s*\{(.*?)\n\}', saturn_src, re.S)
    nb = names.group(1) if names else ''
    check(0 <= nb.find('"Bgm_5a"') < nb.find('g_saturnBattleActive'),
          'Bgm_5a (ranking) resolves without a run in progress')
    # PORT DECISION: a run with the debug features on is scored but never ranked.
    # battle_ranking_place() shifts the table as it places, so it must not be
    # reached at all.
    fin = re.search(r'static int battle_results_finished\([^)]*\)\s*\{(.*?)\n\}', saturn_src, re.S)
    fb = fin.group(1) if fin else ''
    check('g_debugFeaturesEnabled' in fb and
          re.search(r'unranked\s*\?\s*-1\s*:\s*battle_ranking_place\(', fb) is not None,
          'debug mode: the score never reaches the ranking')


def check_clear_banner(saturn_src):
    """The clear banner's text, straight off GAME5.

    0x060888B2 calls the HUD text renderer at 0x0605B8D8 with (112, 112, 0) and
    the font block at 0x060A10B0. Thirteen bytes: five glyphs, a space (0x00),
    six more, and the 0x01 terminator.

    That is "STAGE CLEAR!" - an exclamation mark. "STAGE CLEARED" does not fit:
    CLEARED is seven glyphs and only six follow the space. The repeated glyphs
    settle it independently - E (0x21) is both STAGE's fifth letter and CLEAR's
    third, and A (0x1d) is STAGE's third and CLEAR's fourth, so the two words
    really do share letters, which a mis-split of the same bytes would break.

    This was transcribed as "STAGE CLEARED" in the port for a while, which is a
    12-vs-13 glyph mismatch the text check above could not see.
    """
    print('clear banner text (GAME5 0x060A10B0)')
    game5 = module_path('GAME5.PRG')
    if game5 is None:
        skips.append('assets/SATURN_ISO_CONTENT/GAME5.PRG')
        print('  SKIP no GAME5.PRG, the banner text not compared')
        return
    data = open(game5, 'rb').read()
    off = 0x060A10B0 - PRG_BASE
    raw = data[off:off + 13]
    check(raw == bytes((0x2f, 0x30, 0x1d, 0x23, 0x21, 0x00,
                        0x1f, 0x28, 0x21, 0x1d, 0x2e, 0x1a, 0x01)),
          'GAME5 0x060A10B0 is 5 glyphs, a space, 6 glyphs and the terminator:'
          ' "STAGE CLEAR!"')
    check(raw[4] == raw[8] and raw[2] == raw[9],
          'the E and A glyphs repeat across the two words, so the split is right')
    check(len('STAGE CLEAR!') == 12,
          '"STAGE CLEAR!" is the twelve glyphs the block holds')

    m = re.search(r's_stageClearedHudText\[\]\s*=\s*"([^"]*)"', saturn_src)
    # PrintText8x14 takes the game font's own codes: its '!' is cell 62 (ASCII
    # '>'), not 33 - printing a real '!' draws the wrong glyph.
    check(m is not None and m.group(1) == 'STAGE CLEAR>',
          'the port prints "STAGE CLEAR!" (font code 62 for the mark)')


def check_run_clock(saturn_src):
    """The run clock: where GAME5 counts it and what stops it.

    The counter (0x002FFFA0, 60ths) is advanced by the per-frame interrupt
    routine at 0x06036508 - the one that also advances the play timer - and by
    nothing else, so it runs in the inventory and the item box. Its only gate
    besides the battle bit is bit 0 of the battle block 0x060A67A0:

        0603659E  D13E 6012 C901 2008 8949   block bit 0 clear -> skip
        0603662A  D31E 6232 D125 7201        0x002FFFA0 += 1

    and that bit has two setters and three clearers:

        0602CF0A  6030 C980 .... 8BF8   the start-up waits for the message
        0602CF3E  6012 CB01 2102   set    end of the game's start-up
        0602EAC8  6012 CB01 2102   set    end of the room transition (0x0602DBBC)
        0602DBDA  E2FE .... 6312 2329 2132   clear, the transition's first act
        0602D9EE  E2FE .... 6312 2329 2132   clear, the player's death
                  (0x0602D9C8, called from 0x0602D950 once 0x0602D8C6 has
                  seen HP negative)
        060888BE  E1FE 6282 .... 2219 2822   clear, the arena is cleared

    The port once ticked the clock from the gameplay loop (so it stopped in the
    menus) and gated it on the state (so it ran through the 120-frame clear
    pause and the load of the next arena).
    """
    print('run clock (GAME5 0x06036508, battle block bit 0)')
    game5 = module_path('GAME5.PRG')
    if game5 is None:
        skips.append('assets/SATURN_ISO_CONTENT/GAME5.PRG')
        print('  SKIP no GAME5.PRG, the clock code not compared')
    else:
        data = open(game5, 'rb').read()

        def words(addr, count):
            return struct.unpack_from('>%dH' % count, data, addr - PRG_BASE)

        def lit(addr):
            return struct.unpack_from('>I', data, addr - PRG_BASE)[0]

        check(words(0x0603659E, 5) == (0xD13E, 0x6012, 0xC901, 0x2008, 0x8949)
              and lit(0x06036698) == 0x060A67A0,
              'the interrupt routine skips the count while block bit 0 is clear')
        check(words(0x0603662A, 4) == (0xD31E, 0x6232, 0xD125, 0x7201)
              and lit(0x060366A4) == 0x002FFFA0 and lit(0x060366C4) == 0x00057E03,
              'the count is 0x002FFFA0 += 1, capped past 0x57E03')
        # Every test between the gate and the count branches TO the count: they
        # only decide whether the m:ss text is redrawn.
        check(words(0x060365B2, 1) == (0x8B3A,) and words(0x060365BA, 1) == (0x8B36,)
              and words(0x060365C4, 1) == (0x8F31,) and words(0x060365E6, 1) == (0x8B20,),
              'the menu/redraw tests all fall through to the count')
        hits = [a for a in be32_hits(data, 0x002FFFA0)]
        check(len(hits) == 2,
              '0x002FFFA0 has two literal slots: the count and the run init')
        for addr, what in ((0x0602CF3E, "the game's start-up"),
                           (0x0602EAC8, 'the room transition')):
            check(words(addr, 3) == (0x6012, 0xCB01, 0x2102),
                  'bit 0 is set at the end of %s (0x%08X)' % (what, addr))
        check(words(0x0602CF0A, 2) == (0x6030, 0xC980) and words(0x0602CF16, 1) == (0x8BF8,)
              and lit(0x0602CF78) == 0x002F8665,
              "the start-up waits for the opening message (0x002F8665 bit 7) first")
        check(words(0x0602D8C6, 4) == (0x017D, 0x611F, 0x4111, 0x8904)
              and words(0x0602D950, 2) == (0xD119, 0x410B)
              and lit(0x0602D9B8) == 0x0602D9C8,
              'the game loop calls 0x0602D9C8 once HP is negative')
        for addr, what in ((0x0602DBDA, 'the room transition starts'),
                           (0x0602D9EE, 'the player dies')):
            w = words(addr, 5)
            check(w[0] == 0xE2FE and w[2:] == (0x6312, 0x2329, 0x2132),
                  'bit 0 is cleared when %s (0x%08X)' % (what, addr))
        w = words(0x060888BE, 5)
        check(w[0] == 0xE1FE and w[1] == 0x6282 and w[3:] == (0x2219, 0x2822),
              'bit 0 is cleared when the arena is cleared (0x060888BE)')

    check('s_battle.flags &= ~BB_CLOCK;' in saturn_src
          and re.search(r'BB_CLOCK\s*=\s*0x00000001u', saturn_src) is not None,
          'the port lowers the clock bit (bit 0) on the arena clear')
    tick = re.search(r'void saturn_battle_tick_run\(void\)\s*\{(.*?)\n\}',
                     saturn_src, re.S)
    check(tick is not None and '(s_battle.flags & BB_CLOCK) == 0' in tick.group(1)
          and 's_battle.state' not in tick.group(1),
          'the tick is gated on the clock bit and not on the state')
    check(tick is not None and 'plat_time_ms()' in tick.group(1)
          and re.search(r'#define BATTLE_RUN_FRAMES_PER_SEC\s+60\b', saturn_src) is not None,
          'the clock counts real time in 60ths, not calls')
    sync = re.search(r'static void battle_sync_arena\(void\)\s*\{(.*?)\n\}',
                     saturn_src, re.S)
    check(sync is not None and 'BB_CLOCK' not in
          re.sub(r'//[^\n]*', '', sync.group(1)),
          'battle_sync_arena() leaves the clock bit to the room load')

    def src(*rel):
        return open(os.path.join(REPO, 'src', 'game', *rel),
                    encoding='utf-8', errors='replace').read()

    main_loop = src('MainLoop.cpp')
    check(re.search(r'UpdateDemoTimer\(\);.*?saturn_battle_tick_run\(\);.*?'
                    r'TaskScheduler_Update\(\);', main_loop, re.S) is not None,
          'main_loop() ticks the clock every frame, ahead of the task pass')
    door = re.search(r'\nvoid room_transition_load\(void\)\s*\{(.*?)\n\}',
                     src('DoorSystem.cpp'), re.S)
    check(door is not None
          and re.search(r'saturn_battle_clock_run\(0\);.*?room_set\(\);.*?'
                        r'saturn_battle_clock_run\(1\);\s*$', door.group(1), re.S)
          is not None,
          'room_transition_load stops the clock first and restarts it last')
    check(re.search(r'int game_loop\(void\)\s*\{[^}]*?g_menu_choice_id & 0x80[^}]*?\}'
                    r'[^{}]*?saturn_battle_clock_run\(1\);', src('GameLoop.cpp'), re.S)
          is not None and 'saturn_battle_clock_run' not in src('GameStart.cpp'),
          'the clock starts after the opening message has closed, not at the room load')
    upd = re.search(r'void saturn_battle_update\(void\)\s*\{(.*?)\n\}', saturn_src, re.S)
    check(upd is not None and re.search(
              r'g_playerEntity\.health < 0\)\s*s_battle\.flags &= ~BB_CLOCK;',
              upd.group(1)) is not None,
          "the player's death stops the clock on its first frame")
    check(re.search(r'int saturn_battle_close_run\(void\)\s*\{[^}]*?'
                    r'saturn_battle_clock_run\(0\);', saturn_src, re.S) is not None,
          'leaving the game loop stops the clock')


def check_arena_arming(saturn_src):
    """The fight-room bit must follow the ROOM, not the transition.

    Two of the run's fifteen transitions are doors, not the warp: arenas 1, 6
    and 11 are the rest rooms and the only arenas with a door record
    (`dump_init_doors.py`: 8010->8020, 8060->8070, 80B0->80C0). A run therefore
    leaves arena 1 through a door, and `room_transition_load` knows nothing
    about the Battle block.

    So the bit has to be re-read for whichever arena is current, every frame.
    Arming it only at run start and inside the warp is a softlock: arena 1 never
    clears, so the warp never runs, so arena 2 is entered with the bit clear -
    and the state-0 clear test requires it, so clearing all three of arena 2's
    zombies does nothing at all.
    """
    print('per-arena arming (the door transitions)')
    fn = re.search(r'static void battle_sync_arena\(void\)\s*\{(.*?)\n\}',
                   saturn_src, re.S)
    check(fn is not None, 'the port has battle_sync_arena()')
    body = fn.group(1) if fn else ''
    check('s_battle.arena' in body and 'arena == s_battle.arena' in body,
          'battle_sync_arena() keys off the arena having changed')
    check('kArenaHasEnemies[arena]' in body
          and 'BB_FIGHT_ROOM' in body,
          'battle_sync_arena() is what sets and clears BB_FIGHT_ROOM')
    check(body.count('BB_FIGHT_ROOM') == 2,
          'battle_sync_arena() is the ONLY writer of BB_FIGHT_ROOM')
    check(saturn_src.count('s_battle.flags |= BB_FIGHT_ROOM') == 1
          and saturn_src.count('s_battle.flags &= ~BB_FIGHT_ROOM') == 1,
          'nothing outside battle_sync_arena() arms or disarms the fight-room '
          'bit, so it cannot go stale behind a door')

    upd = re.search(r'void saturn_battle_update\(void\)\s*\{(.*?)\n\}',
                    saturn_src, re.S)
    check(upd is not None
          and upd.group(1).index('battle_sync_arena();')
              < upd.group(1).index('s_battle.state == 0'),
          'saturn_battle_update() syncs the arena BEFORE the state-0 clear test')

    # Every arena that a door can reach must be a fight room, or the room the
    # player walks into could never clear even with the bit armed.
    for room in (2, 7, 12):
        check(kArenaHasEnemies_src(saturn_src, room),
              'arena %d, the room arena %d\'s door leads to, is a fight room'
              % (room, room - 1))


def kArenaHasEnemies_src(saturn_src, room):
    m = re.search(r'kArenaHasEnemies\[17\]\s*=\s*\{([^}]*)\}', saturn_src)
    if m is None:
        return False
    vals = [int(v) for v in re.findall(r'\b[01]\b', m.group(1))]
    return len(vals) > room and bool(vals[room] & 1)


def check_enemy_count_derivation(saturn_src):
    """"Enemies remaining" against the Saturn's own derivation.

    The Saturn does not read the engine's enemy counter straight into the clear
    test. GAME5 0x06055480 loads the Battle block's +8 field with the engine's
    enemy-count byte, and 0x060555C8-0x060555D6 then walks the entity slots and
    takes one off that field per slot whose health (Entity+0x88) is NEGATIVE:

        060555C8  901A  mov.w @(0x34,pc),r0     ; r0 = 0x88
        060555CA  011D  mov.w @(r0,r1),r1        ; r1 = ENTITY->health
        060555CC  4111  cmp/pl r1                ; T = health >= 0
        060555CE  8903  bt 0x060555D8            ; still alive -> skip
        060555D0  D20C  mov.l @(0x30,pc),r2      ; r2 = 0x060A67A8 (block + 8)
        060555D2  6121  mov.w @r2,r1
        060555D4  71FF  add #-1,r1
        060555D6  2211  mov.w r1,@r2             ; remaining--

    So the field is "slots allocated MINUS slots dead", and it only reaches zero
    once the last enemy is dead. Reading the raw engine counter instead is a
    softlock: nothing in this engine ever decrements it - a corpse keeps its
    slot with status_flags bit 0 set for the rest of the room - so the clear
    test can never come true. That was a real bug, and this is what keeps the
    derivation from being replaced by the counter again.
    """
    print('"enemies remaining" derivation (GAME5 0x06055480 / 0x060555C8)')
    game5 = module_path('GAME5.PRG')
    if game5 is None:
        skips.append('assets/SATURN_ISO_CONTENT/GAME5.PRG')
        print('  SKIP no GAME5.PRG, the counter derivation not compared')
        return
    data = open(game5, 'rb').read()

    def at(addr, n=2):
        off = addr - PRG_BASE
        return data[off:off + n] if 0 <= off <= len(data) - n else b''

    # The field's initialiser: block+8 = the engine's enemy-count byte. Both
    # PC-relative slots live after the loop, at 0x06055604 and 0x06055608.
    check(at(0x06055480) == b'\xD3\x60' and at(0x06055482) == b'\xD1\x61',
          'GAME5 0x06055480-0x06055482 load the +8 field and the engine counter')
    check(at(0x06055604, 4) == b'\x06\x0A\x67\xA8',
          'GAME5 0x06055604 holds 0x060A67A8 (the Battle block\'s +8 field)')
    check(at(0x06055608, 4) == b'\x00\x2F\x30\x22',
          'GAME5 0x06055608 holds 0x002F3022 (the engine enemy-count byte)')
    check(at(0x06055488) == b'\x61\x2C' and at(0x0605548A) == b'\x23\x11',
          'GAME5 0x06055488-0x0605548A zero-extend the byte counter and store it '
          'into +8')

    # The per-slot decrement, and the health guard that skips it.
    check(at(0x060555C8) == b'\x90\x1A' and at(0x060555CA) == b'\x01\x1D',
          'GAME5 0x060555C8 reads Entity+0x88 (health) off the slot')
    check(at(0x060555CC) == b'\x41\x11',
          'GAME5 0x060555CC is cmp/pl - the halfword sign test on health')
    check(at(0x060555CE) == b'\x89\x03',
          'GAME5 0x060555CE is bt +6, i.e. it SKIPS the decrement while '
          'health >= 0 and counts only the dead slots')
    check(at(0x060555D2) == b'\x61\x21' and at(0x060555D4) == b'\x71\xFF'
          and at(0x060555D6) == b'\x22\x11',
          'GAME5 0x060555D2-0x060555D6 is the decrement of the +8 field')
    check(at(0x060555D0) == b'\xD2\x0C' and at(0x06055604, 4) == b'\x06\x0A\x67\xA8',
          'the decrement targets the same +8 field the pass initialises')

    fn = re.search(r'static int battle_enemies_left\(void\)\s*\{(.*?)\n\}',
                   saturn_src, re.S)
    check(fn is not None and 'g_enemy_count' in fn.group(1)
          and 'health < 0' in fn.group(1)
          and 'ENTITY_STATUS_ACTIVE' in fn.group(1),
          'battle_enemies_left() is the slot count minus the dead ones '
          '(active slots whose health has gone negative)')
    # Two, not three: the arena warp used to re-read it after its own room_set();
    # it now hands off to the door transition, which loads the room later, and
    # the per-frame state-0 read picks up the new arena's count.
    check(saturn_src.count('s_battle.enemiesLeft = battle_enemies_left();') == 2,
          'both "enemies remaining" reads go through the derivation '
          '(run start, per frame)')
    check('s_battle.enemiesLeft = g_enemy_count;' not in saturn_src,
          'nothing reads the raw enemy counter as "enemies remaining" any more')


def check_constants(saturn_src, saturn_hdr):
    print('constants')
    check('#define SATURN_BATTLE_STAGE_ID    %d' % BATTLE_STAGE_ID in saturn_hdr,
          'SATURN_BATTLE_STAGE_ID is %d (STAGE8, 0-indexed)'
          % BATTLE_STAGE_ID)
    check('#define SATURN_BATTLE_ARENA_COUNT %d' % ARENA_COUNT in saturn_hdr,
          'SATURN_BATTLE_ARENA_COUNT is %d' % ARENA_COUNT)
    check(FIRST_ROOM == 1 and LAST_ROOM == ARENA_COUNT,
          'the room byte runs 1..%d with the arena number' % ARENA_COUNT)
    # STAGE_ARRANGE_FIRST must still be 7, or the stage-indexed tables read the
    # wrong row and get_stage_id() stops folding the battle stage correctly.
    globals_h = open(os.path.join(REPO, 'src', 'Globals.h'),
                     encoding='utf-8', errors='replace').read()
    check(re.search(r'#define STAGE_ARRANGE_FIRST\s+%d\b' % BATTLE_STAGE_ID,
                    globals_h) is not None,
          'STAGE_ARRANGE_FIRST is still %d, so stage %d folds to base row 0'
          % (BATTLE_STAGE_ID, BATTLE_STAGE_ID))

    # The unlock byte. The struct is static_assert'ed to 0x41C, so the offset is
    # the thing to pin, and the old padding field must be gone.
    bio = open(BIOCARD_HDR, encoding='utf-8', errors='replace').read()
    m = re.search(r'unsigned char saturnBattleUnlocked;\s*//\s*0x([0-9A-Fa-f]+)',
                  bio)
    check(m is not None and int(m.group(1), 16) == UNLOCK_CARD_OFFSET,
          'BioCardLayout::saturnBattleUnlocked is at 0x%02X' % UNLOCK_CARD_OFFSET)
    check('pad_0x20f' not in bio,
          'the old pad_0x20f padding field is gone, not shadowed')
    check(re.search(r'#define g_SaturnBattleUnlocked\s+\(g_BioCard\.saturnBattleUnlocked\)',
                    bio) is not None,
          'g_SaturnBattleUnlocked aliases that field')


def check_start_message(saturn_src):
    """The opening message, re-encoded and compared to GAME5's own bytes.

    GAME5 0x0609E979 holds the Battle Game's opening message inside the global
    message table (it is in GAME1..GAME5 alike, so it is common-source text, not
    something an arena's RDT carries - arena 8010's init script has no message
    at all). The port keeps its own copy as a STR(), so the only thing that keeps
    it honest is encoding the port's source and comparing: a reworded string, a
    dropped escape or a flipped flag bit all show up here.
    """
    print('opening message (GAME5 0x%08X)' % BATTLE_MSG_ADDR)
    # The Saturn's own text, as the STR() source that encodes to GAME5's bytes
    # (the port no longer carries this form; it shows s_gm63 instead, below).
    src = 'You must defeat  \\n all the enemies\\pTand enter into the\\nmysterious zone.   '

    # tools/verify_msg_encoding.py mirrors pft_detail::Encoded. It has no
    # __main__ guard, so import it with its self-test output swallowed.
    try:
        import contextlib
        import io
        sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
        with contextlib.redirect_stdout(io.StringIO()):
            from verify_msg_encoding import enc
    except ImportError:
        skips.append('verify_msg_encoding.py')
        print('  SKIP cannot import the encoder, message not compared')
        return
    body = enc(src)

    game5 = module_path('GAME5.PRG')
    if game5 is None:
        skips.append('assets/SATURN_ISO_CONTENT/GAME5.PRG')
        print('  SKIP no GAME5.PRG, message not compared against the disc')
        return
    off = BATTLE_MSG_ADDR - PRG_BASE
    with open(game5, 'rb') as fh:
        disc = fh.read()[off:off + len(body)]
    if disc == bytes(body):
        ok('the port\'s STR() encodes to the %d bytes at GAME5.PRG 0x%08X'
           % (len(body), BATTLE_MSG_ADDR))
    else:
        fail('the port\'s STR() does not match GAME5.PRG 0x%08X' % BATTLE_MSG_ADDR)
        print('       port  : %s' % bytes(body).hex(' '))
        print('       saturn: %s' % disc.hex(' '))

    # The string the port shows is global message 63 (Globals.cpp s_gm63), which
    # may differ from the Saturn's only in whitespace and the terminator: the
    # alignment padding is dropped and it ends with the auto-dismiss form instead
    # of a bare 0x01 (the port's own loading messages end that way).
    globals_src = open(os.path.join(REPO, 'src', 'Globals.cpp'),
                       encoding='utf-8', errors='replace').read()
    pm = re.search(r's_gm63\s*=\s*STR\(\s*((?:"(?:[^"\\]|\\.)*"\s*)+)\)',
                   globals_src, re.S)
    if check(pm is not None, 'read s_gm63 from Globals.cpp'):
        # The C source spells each STR() escape with two backslashes.
        port = ''.join(re.findall(r'"((?:[^"\\]|\\.)*)"', pm.group(1)))
        BS = '\\\\'
        sat = src.replace('\\n', ' ').replace('\\pT', ' ')
        pwords = port[:-len(BS + 'dT')] if port.endswith(BS + 'dT') else port
        pwords = pwords.replace(BS + 'n', ' ').replace(BS + 'pT', ' ')
        check(pwords.split() == sat.split(),
              'the port string says the Saturn text, differing only in padding and '
              'the auto-dismiss terminator')
        check(port.endswith(BS + 'dT'), 'and it ends in the auto-dismiss form')
        check(re.search(r'\(unsigned char\*\)s_gm63\.bytes,\s*//\s*\[63\]', globals_src)
              is not None, 'and it sits in global_messages[63]')
        check_start_message_jpn(src, port)


def check_start_message_jpn(src, usa_port):
    """The JPN twin of the opening message (global_messages_jpn[63]).

    The Japanese Saturn disc keeps the English sentence, but in the JPN font's
    encoding (Bio Hazard GAME5.PRG 0x0609EB37: `04 03 00`, then the text, whose
    '.' is F8 1C rather than the USA's 0x79). The JPN build reads its global
    messages from global_messages_jpn, so without its own [63] a JPN Battle Game
    would show a NULL message.
    """
    jtt = open(os.path.join(REPO, 'src', 'game', 'JpnTextTables.cpp'),
               encoding='utf-8-sig', errors='replace').read()
    m = re.search(r's_gmjp_sat63\s*=\s*STR_JP\(u8"((?:[^"\\]|\\.)*)"\)', jtt)
    if not check(m is not None, 'read s_gmjp_sat63 from JpnTextTables.cpp'):
        return
    # STR_JP takes only the raw \d\xNN delay form: a plain operand is not
    # consumed, so `\dT` printed a 'T' and never dismissed (play-test,
    # 2026-10-08). 0x30 is what the USA STR encodes 'T' to.
    BS = '\\\\'
    check(m.group(1) == usa_port.replace(BS + 'dT', BS + 'd' + BS + 'x30'),
          'the JPN entry spells the USA s_gm63 text, its \\dT delay as \\d\\x30')
    check(re.search(r'\\\\d(?!\\\\x[0-9A-Fa-f]{2})', jtt) is None,
          'no STR_JP in JpnTextTables.cpp uses a \\d delay STR_JP cannot read')
    check(re.search(r'\(unsigned char\*\)s_gmjp_sat63\.bytes,\s*//\s*\[ 63\]', jtt)
          is not None, 'and it sits in global_messages_jpn[63]')
    try:
        sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
        import jpn_msg_decode
        import saturn_iso
    except ImportError as e:
        skips.append('jpn_msg_decode.py / saturn_iso.py (%s)' % e)
        return
    if not os.path.isfile(os.path.join(REPO, saturn_iso.TRACK_JPN)):
        skips.append(saturn_iso.TRACK_JPN)
        print('  SKIP no JPN Saturn image, the JPN message not compared')
        return
    cwd = os.getcwd()
    os.chdir(REPO)
    try:
        game5 = saturn_iso.Iso(saturn_iso.TRACK_JPN).get('/GAME5.PRG')
    finally:
        os.chdir(cwd)
    # The JPN '.' is two bytes, so the disc pads with one space fewer: the
    # string keeps the USA one's length.
    body = jpn_msg_decode.encode(src.rstrip(' ') + '  ')
    off = JPN_BATTLE_MSG_ADDR - PRG_BASE
    if game5[off - 3:off] + game5[off:off + len(body)] == b'\x04\x03\x00' + body:
        ok('the JPN encoding of the Saturn text is the %d bytes at JPN GAME5.PRG 0x%08X'
           % (len(body), JPN_BATTLE_MSG_ADDR))
    else:
        fail('the JPN encoding does not match JPN GAME5.PRG 0x%08X' % JPN_BATTLE_MSG_ADDR)
        print('       port  : %s' % body.hex(' '))
        print('       saturn: %s' % game5[off:off + len(body)].hex(' '))


def check_arena_sequence(saturn_src):
    """The per-frame arena machine: the fight-room table and the two tables'
    framing.

    The "has enemies" table is 17 single BYTES at GAME5 0x060959A0, bit 0 per
    room - not u16 pairs, which is what the analysis doc's "17 bytes" first
    invites and which yields an irregular table that matches nothing. Bit 0 is
    clear at exactly arenas 1, 6 and 11, the three rest rooms, and those are the
    only arenas with a door record (dump_init_doors.py: 8010->8020, 8060->8070,
    80B0->80C0). That is the check.
    """
    print('arena sequence')
    game5 = module_path('GAME5.PRG')
    if game5 is None:
        skips.append('assets/SATURN_ISO_CONTENT/GAME5.PRG')
        print('  SKIP no GAME5.PRG, the fight-room table not compared')
    else:
        with open(game5, 'rb') as fh:
            fh.seek(FIGHT_TABLE_ADDR - PRG_BASE)
            disc = list(fh.read(17))
        m = re.search(r'kArenaHasEnemies\[17\]\s*=\s*\{([^}]*)\}', saturn_src, re.S)
        if not check(m is not None, 'read kArenaHasEnemies'):
            return
        port = [int(v) for v in re.findall(r'\b[01]\b', m.group(1))]
        check(port == disc,
              'kArenaHasEnemies matches GAME5.PRG 0x%08X: %s'
              % (FIGHT_TABLE_ADDR, ' '.join('%02x' % b for b in disc)))
        rests = [i for i, v in enumerate(port) if not (v & 1)]
        check(rests == [0, 1, 6, 11, 16],
              'bit 0 is clear at rooms %s - the three rest rooms 1/6/11, plus '
              '0 and 16 which are not arenas' % rests)
        # And the rest rooms are the arenas that own a door record.
        for room in (1, 6, 11):
            rdt = os.path.join(ARENA_RDTS, 'ROOM80%X0.RDT' % room)
            if not os.path.isfile(rdt):
                continue
            door = door_dest(rdt)
            check(door == room + 1,
                  'arena %d (a rest room) has a door to %s' % (room, door))

    for label, needle in (
        ('the clear pause is 120 frames', '#define BATTLE_CLEAR_PAUSE_FRAMES 0x78'),
        ('the out-of-ammo grace is 0xB3 frames', '#define BATTLE_NO_AMMO_GRACE      0xB3'),
        ('the run clock saturates at 99:59', '#define BATTLE_MAX_RUN_FRAMES    0x57E04'),
        ('"enemies remaining" is derived, not raw g_enemy_count',
         's_battle.enemiesLeft = battle_enemies_left();'),
        ('the port carries the "STAGE CLEAR!" HUD text',
         's_stageClearedHudText[] = "STAGE CLEAR>"'),
        ('the advance moves to arena + 1', 'const int next = arena + 1;'),
        ('the run ends after arena 15', '>= SATURN_BATTLE_ARENA_COUNT)'),
        ('the fight-room bit is re-armed per arena', 'battle_sync_arena();'),
    ):
        check(needle in saturn_src, label)
    check('g_playerEntity.health = 1000;' in saturn_src
          and 'g_playerEntity.health = s_battleSavedHp;' in saturn_src,
          'HP is held at full for the pause and the real value is restored after')
    check(re.search(r'void saturn_battle_update\(void\).*?s_battle\.state == 1',
                    saturn_src, re.S) is not None,
          'saturn_battle_update() has the cleared state')
    draw_hud = re.search(r'void saturn_battle_draw_hud\(void\)\s*\{(.*?)\n\}',
                         saturn_src, re.S)
    check(draw_hud is not None and 'saturn_battle_run_frames()' in draw_hud.group(1)
          and 'PrintText8x14' in draw_hud.group(1),
          'the HUD draws the elapsed run clock with PrintText8x14')
    check(draw_hud is not None and 's_battle.state == 1' in draw_hud.group(1)
          and 's_stageClearedHudText' in draw_hud.group(1),
          'the clear banner is drawn during the 120-frame pause')
    check('s_battleRunFrames = BATTLE_MAX_RUN_FRAMES' in saturn_src,
          'the elapsed frame counter stops at the Saturn 99:59 limit')
    check_run_clock(saturn_src)
    check_enemy_count_derivation(saturn_src)
    check_arena_arming(saturn_src)
    check_clear_banner(saturn_src)

    game_loop = open(os.path.join(REPO, 'src', 'game', 'saturn', 'SaturnTyrant.cpp'),
                     encoding='utf-8', errors='replace').read()
    check(re.search(r'g_saturnBattleActive\s*\)\s*\{'
                    r'\s*saturn_battle_update\(\);.*?saturn_battle_run_finished\(\)\)\s*\{'
                    r'\s*return 1;\s*\}\s*saturn_battle_draw_hud\(\);',
                    game_loop, re.S) is not None,
          'the game loop advances, leaves at the run end, and draws the Battle HUD')
    check('saturn_battle_tick_run' not in game_loop,
          'the run clock is not ticked from the gameplay loop (it would stop in the menus)')
    real_game_loop = open(os.path.join(REPO, 'src', 'game', 'GameLoop.cpp'),
                          encoding='utf-8', errors='replace').read()
    check(re.search(r'Battle mode has no self-destruct timer\..*?'
                    r'if \(g_bSaturnMode && g_saturnBattleActive\) \{\s*'
                    r'goto LAB_00480d7c;', real_game_loop, re.S) is not None,
          'Battle mode bypasses the self-destruct countdown branch')


def door_dest(rdt):
    """The dest field of a rest room's door_set record, or None."""
    try:
        out = subprocess.run([sys.executable,
                              os.path.join(REPO, 'tools', 'dump_init_doors.py'), rdt],
                             capture_output=True, text=True, timeout=60)
    except (OSError, subprocess.SubprocessError):
        return None
    m = re.search(r'door_set\b.*?dest=([0-9a-fA-F]+)', out.stdout)
    return int(m.group(1), 16) if m else None


def check_gating():
    print('gating - OG and DC must not be able to reach any of it')
    room = open(ROOM_SRC, encoding='utf-8', errors='replace').read()
    check(room.count('saturn_battle_asset_room(') == 2,
          'load_room_bg redirects both branches (mode 0 and the cache): %d call(s)'
          % room.count('saturn_battle_asset_room('))
    sound = open(SOUND_SRC, encoding='utf-8', errors='replace').read()
    check('saturn_battle_asset_room(' in sound,
          'Room_LoadEnemySoundBanks takes the arena row')
    roominit = open(ROOMINIT_SRC, encoding='utf-8', errors='replace').read()
    check(re.search(r'g_bSaturnMode\s*&&\s*g_saturnBattleActive\s*\)\s*\{\s*'
                    r'variant\s*=\s*0', roominit) is not None,
          'LoadRoomRdt forces the RDT variant digit to 0 in battle mode')

    saturn_src = open(SATURN_SRC, encoding='utf-8', errors='replace').read()
    for fn, ret in (('saturn_battle_unlock', 'void'),
                    ('saturn_battle_sync_from_card', 'void'),
                    ('saturn_battle_preserve', 'void'),
                    ('saturn_battle_begin_run', 'void')):
        body = re.search(r'\n%s %s\(void\)\s*\{(.*?)\n\}' % (ret, fn), saturn_src, re.S)
        if not check(body is not None, 'found %s()' % fn):
            continue
        first = body.group(1).strip().splitlines()
        first = [ln for ln in first if ln.strip() and not ln.strip().startswith('//')]
        guarded = bool(first) and ('g_bSaturnMode' in first[0] or
                                   'g_saturnBattleActive' in first[0])
        check(guarded, '%s() returns immediately outside Saturn mode' % fn)

    # The run-end clear. Without it the arena redirect outlives the run and the
    # next campaign game loads arena 1's background.
    gs = open(GAMESTART_SRC, encoding='utf-8', errors='replace').read()
    tail = gs.split('end_game_status = game_loop();')
    check(len(tail) == 2 and 'saturn_battle_close_run();' in tail[1].split('Task_chain')[0],
          'game_start closes the run before any of its exits')
    close = re.search(r'int saturn_battle_close_run\(void\)\s*\{(.*?)\n\}', saturn_src, re.S)
    cb = close.group(1) if close else ''
    check(close is not None and 'battle_compute_result(' in cb and
          cb.rfind('saturn_battle_end_run();') > cb.rfind('battle_compute_result(') and
          cb.count('saturn_battle_end_run();') == 1 and
          cb.find('saturn_battle_end_run();') > cb.rfind('}'),
          'the run is scored BEFORE battle mode is cleared, and cleared on every path')
    # RANKING, not the title or the ending, follows every run (GAME5 0x0602BD28).
    t1 = tail[1] if len(tail) == 2 else ''
    check(0 <= t1.find('Task_chain((void*)saturn_ranking_state);') <
          t1.find('Task_chain((void*)title_state);'),
          'game_start chains saturn_ranking_state before its campaign exits')
    check('saturn_battle_begin_run();' in gs,
          'game_start applies the run loadout and spawn point')
    check(gs.index('saturn_battle_begin_run();') < gs.index('LoadHeldItemsImages();'),
          'the run loadout is in place before the inventory images are built')
    # The stage and the room live in the card, and game_start rewrites the whole
    # card from data\bio_card.dat. If they were set from the title they would be
    # wiped, and the run would load the new game's room 106 with arena 1's
    # background - which is exactly what it did.
    card_reset = gs.index('memcpy(&g_BioCardData[0], g_loadDataDestPointer, 1052)')
    begin = gs.index('saturn_battle_begin_run();')
    check(card_reset < begin,
          'the run is set up AFTER the 1052-byte bio_card reset, not before it')
    check(re.search(r'void saturn_battle_start_run\(void\)\s*\{[^}]*\}', saturn_src, re.S)
          is not None and 'g_stageId' not in re.search(
              r'void saturn_battle_start_run\(void\)\s*\{(.*?)\n\}', saturn_src, re.S).group(1),
          'saturn_battle_start_run() does not touch g_stageId')
    begin_body = re.search(r'void saturn_battle_begin_run\(void\)\s*\{(.*?)\n\}',
                           saturn_src, re.S).group(1)
    check('g_stageId' in begin_body and 'g_roomId' in begin_body,
          'saturn_battle_begin_run() sets the stage and the room')
    check(begin < gs.index('init_room();'),
          'the stage and room are set before init_room()')

    # The opening message is an ordinary global message (checked in
    # check_start_message), so nothing in SaturnBattle.cpp primes it any more.
    check('set_message_display' not in saturn_src,
          'SaturnBattle.cpp no longer primes the opening message itself')
    roominit = open(ROOMINIT_SRC, encoding='utf-8', errors='replace').read()

    # The unlock's three touch points.
    ending = open(ENDING_SRC, encoding='utf-8', errors='replace').read()
    check('saturn_battle_unlock();' in ending, 'the ending raises the unlock')
    ei = ending.find('saturn_battle_unlock();')
    es = ending.find('LoadSaveGameState(0, (int)s_saveScratch, 0, 1, 1);')
    check(0 <= ei < es, 'the unlock is raised before the ending writes the save')
    save = open(SAVELOAD_SRC, encoding='utf-8', errors='replace').read()
    check('saturn_battle_sync_from_card();' in save,
          'loading a save picks the unlock back up from the card')

    # The title's third item. The Saturn's port-2 X+Y+Z+Start code is NOT
    # ported; [Game] BattleGame is the replacement, so nothing in the game may
    # still poll for a key combo.
    title = open(TITLE_SRC, encoding='utf-8', errors='replace').read()
    check('saturn_battle_poll_cheat' not in saturn_src + title,
          'the port-2 X+Y+Z+Start cheat is gone, replaced by [Game] BattleGame')
    check('plat_key_state' not in saturn_src,
          'SaturnBattle.cpp reaches the platform layer for nothing')
    cfg = open(os.path.join(REPO, 'src', 'system', 'ConfigFile.cpp'),
               encoding='utf-8', errors='replace').read()
    check('ReadInt(path, "Game", "BattleGame", 0)' in cfg,
          'config.ini [Game] BattleGame is read')
    check('"BattleGame=%d\\n"' in cfg,
          'config.ini writes [Game] BattleGame back out')
    check(re.search(r'g_bSaturnMode\s*&&\s*g_bSaturnBattleGame', saturn_src) is not None,
          'the config override is gated on Saturn mode')
    check(re.search(r'case SATURN_TITLE_BATTLE_ITEM:', title) is not None,
          'the third item has its own dispatch case')
    check('LoadFile(GAME_DATA_ROOT "data\\\\fontus.tim"' in title,
          "the BATTLE GAME row is built from the port's own 8x14 font")
    # The row must stay unreachable if its page failed to build, or the cursor
    # would ask display_texture for a page that was never loaded.
    check(re.search(r'title_menu_last_item\(\)[^;]*s_battlePageReady', title) is not None,
          'the cursor only reaches row 3 once its page exists')


def check_build_files():
    print('build wiring')
    vcx = open(VCXPROJ, encoding='utf-8', errors='replace').read()
    sources = re.findall(r'<ClCompile Include="([^"]+)"', vcx)
    check(any(s.replace('\\', '/') == 'src/game/saturn/SaturnBattle.cpp'
              for s in sources),
          'Game.vcxproj lists src/game/saturn/SaturnBattle.cpp')
    by_name = {}
    for s in sources:
        by_name.setdefault(os.path.basename(s), []).append(s)
    dupes = {n: v for n, v in by_name.items() if len(v) > 1}
    if dupes:
        for name, group in sorted(dupes.items()):
            fail('%s.cpp basename collides in Game.vcxproj: %s - they share one'
                 ' .obj and one overwrites the other'
                 % (name, ', '.join(group)))
    else:
        ok('all %d Game.vcxproj sources have distinct basenames' % len(sources))

    cml = open(CMAKELISTS, encoding='utf-8', errors='replace').read()
    check('src/game/saturn/SaturnBattle.cpp' in cml,
          'CMakeLists.txt lists src/game/saturn/SaturnBattle.cpp')


def check_arena_rooms():
    """One RDT per arena, and the room byte has to be the ARENA NUMBER.

    The 15 files in the overlay are the check on the room byte. LoadRoomRdt
    builds "room<stage><roomHi><roomLo><variant>", so the port's g_roomId is
    split into two nibbles. Arena 1's file ROOM8010 is therefore stage 8, room
    0x01, variant 0 - the room byte is 1, the arena number, not 0x10. Reading the
    two middle digits as one byte asks for ROOM8100, which is in no tree, and
    that is not a cosmetic error: the load fails and room_set() then walks a
    stale buffer. This walks the 15 files, derives the room byte each one
    implies, and requires arena N's file to imply N.
    """
    print('arena room files in the overlay, and the room byte they imply')
    if not os.path.isdir(ARENA_RDTS):
        skips.append('assets/SATURN/Stage8')
        print('  SKIP no assets/SATURN/Stage8')
        return
    have = {n.lower() for n in os.listdir(ARENA_RDTS)}
    for arena in range(1, ARENA_COUNT + 1):
        name = 'ROOM80%X0.RDT' % arena
        if name.lower() not in have:
            fail('assets/SATURN/Stage8/%s is missing' % name)
            continue
        # "ROOM8010.RDT" -> strip ROOM and .RDT -> "8010" -> stage 8, room 0x01.
        digits = name[4:-4]
        stage = int(digits[0], 16)
        room = int(digits[1:3], 16)
        variant = int(digits[3], 16)
        if stage != BATTLE_STAGE_ID + 1 or room != arena or variant != 0:
            fail('%s is stage %d room 0x%02X variant %d; arena %d must be stage %d'
                 ' room 0x%02X variant 0'
                 % (name, stage, room, variant, arena,
                    BATTLE_STAGE_ID + 1, arena))
        else:
            ok('%s -> stage %d, room 0x%02X (= arena %d), variant 0'
               % (name, stage, room, arena))


def check_room_byte():
    """g_roomId must be the arena number, and the constants must say so."""
    print('room byte')
    hdr = open(SATURN_HDR, encoding='utf-8', errors='replace').read()
    src = open(SATURN_SRC, encoding='utf-8', errors='replace').read()
    check('#define SATURN_BATTLE_FIRST_ROOM  %d' % FIRST_ROOM in hdr,
          'SATURN_BATTLE_FIRST_ROOM is %d (arena 1)' % FIRST_ROOM)
    check('#define SATURN_BATTLE_LAST_ROOM   %d' % LAST_ROOM in hdr,
          'SATURN_BATTLE_LAST_ROOM is %d (arena %d)' % (LAST_ROOM, LAST_ROOM))
    begin = re.search(r'void saturn_battle_begin_run\(void\)\s*\{(.*?)\n\}', src, re.S)
    body = begin.group(1) if begin else ''
    check('g_roomId = SATURN_BATTLE_FIRST_ROOM;' in body,
          'saturn_battle_begin_run() starts the run at arena 1')
    # The room_set guard: without it a missing RDT is an access violation, not a
    # message. This is what the first Battle Game play-test hit.
    roominit = open(ROOMINIT_SRC, encoding='utf-8', errors='replace').read()
    check('s_rdtLoadFailed = 1;' in roominit
          and re.search(r'void room_set\(void\)[^{]*\{.*?Room_RdtLoadFailed\(\)\s*\)\s*\{',
                        roominit, re.S) is not None,
          'room_set() abandons the room when LoadRoomRdt() found no file')


def check_arena_index_is_derived():
    """The arena index must come from the room byte, not a stored counter.

    Every table is indexed by the room byte - arena 1's own door is dest=0x02 -
    so a stored counter that a room transition has to advance is the one thing
    that can go stale. When it did, every later arena loaded arena 1's
    background: the room changed, the index did not.
    """
    print('arena index')
    hdr = open(SATURN_HDR, encoding='utf-8', errors='replace').read()
    src = open(SATURN_SRC, encoding='utf-8', errors='replace').read()
    check('g_saturnBattleIndex' not in src and 'g_saturnBattleIndex' not in hdr,
          'there is no stored g_saturnBattleIndex to fall out of step')
    fn = re.search(r'int saturn_battle_arena_index\(void\)\s*\{(.*?)\n\}', src, re.S)
    check(fn is not None and 'g_roomId' in fn.group(1),
          'saturn_battle_arena_index() returns the room byte, clamped to 1..%d'
          % ARENA_COUNT)
    # Nothing outside the module may read an index; every consumer goes through
    # the redirect, so the bg and the sound banks cannot disagree with it.
    for path, name in ((ROOM_SRC, 'load_room_bg'),
                       (SOUND_SRC, 'Room_LoadEnemySoundBanks')):
        body = open(path, encoding='utf-8', errors='replace').read()
        check('g_saturnBattleIndex' not in body,
              '%s takes its arena from saturn_battle_asset_room()' % name)


def check_start_flow():
    """TITLE -> load screen -> GAME5, and the message on the loading screen.

    Two things the first play-test got wrong, both worth pinning:

    * the third item chained game_start directly, so the run took the NEW GAME
      path - the card was reset from data\\bio_card.dat, the character was lost
      and the opening narration was the new game's. The Saturn goes through
      BU_LOAD, the load screen, and runs as whoever the chosen save was.
    * the opening message was shown from the game loop, i.e. inside the room.
      The Saturn shows it where the game shows its own start-of-game line, on
      the black screen before the arena.
    """
    print('start flow')
    title = open(TITLE_SRC, encoding='utf-8', errors='replace').read()
    dispatch = re.search(r'case SATURN_TITLE_BATTLE_ITEM:(.*?)case 2:', title, re.S)
    if not check(dispatch is not None, 'the third item has its own case before case 2'):
        return
    body = dispatch.group(1)
    check('Task_chain((void*)saturn_battle_load_state);' in body,
          'the third item chains the Battle Game load state')
    check('Task_chain((void*)game_start)' not in body,
          'the third item does not chain game_start itself')
    # The ranking owns "continue?"; title_state no longer reads a hand-off flag
    # or clears battle mode on entry.
    check('saturn_battle_take_continue' not in title and
          'saturn_battle_end_run' not in title,
          'title_state holds no Battle Game run logic')

    saturn_src = open(SATURN_SRC, encoding='utf-8', errors='replace').read()
    load = re.search(r'void saturn_battle_load_state\(void\)\s*\{(.*?)\n\}', saturn_src, re.S)
    lb = load.group(1) if load else ''
    check(load is not None and
          0 <= lb.find('LoadSaveGameState(1, 0x80180000, 0, 1, 0);') <
          lb.find('saturn_battle_start_run();') <
          lb.find('Task_chain((void*)game_start);'),
          'BU_LOAD -> GAME5: the load screen, THEN battle mode, then game_start '
          "(the screen's EXIT chains the title and leaves nothing armed)")
    rank = re.search(r'void saturn_ranking_state\(void\)\s*\{(.*?)\n\}', saturn_src, re.S)
    rb = rank.group(1) if rank else ''
    check('Task_chain((void*)title_state);' in rb and
          'Task_chain((void*)saturn_battle_load_state);' in rb,
          'RANKING exits to the title (NO) or to BU_LOAD (YES) - 0x0602E764')

    roominit = open(ROOMINIT_SRC, encoding='utf-8', errors='replace').read()
    loader = re.search(r'void display_game_loading_message\(void\)\s*\{(.*?)\n\}',
                       roominit, re.S)
    check(loader is not None, 'display_game_loading_message() exists')
    if loader:
        lb = loader.group(1)
        check('set_message_display(0x7f, 0);' in lb,
              'the opening message (global message 63) replaces the 0x5B "NEW GAME" line')
        check(lb.index('set_message_display(0x7f, 0);') < lb.rindex('0x5b'),
              'and it is checked BEFORE the new-game message is set')
    gameloop = open(os.path.join(REPO, 'src', 'game', 'GameLoop.cpp'),
                    encoding='utf-8', errors='replace').read()
    check('0x7f' not in gameloop and 'saturn_battle_show_start_message' not in gameloop,
          'the opening message is not shown from the game loop any more')
    tyrant = open(os.path.join(REPO, 'src', 'game', 'saturn', 'SaturnTyrant.cpp'),
                  encoding='utf-8', errors='replace').read()
    check('saturn_game_loop_update()' in gameloop and 'saturn_battle_update();' in tyrant,
          'the arena sequence IS driven from the game loop (saturn_game_loop_update)')


def check_battle_music(saturn_src):
    """The arena BGM tables, against GAME5; the track names, against the migrator.

    GAME5's BGM loader (0x0604C9F0) takes STAGE8's group from a per-room byte
    table at 0x0609ACCC and the channel bits from the one at 0x0609ACDC; group
    0x40 loads /SND/BATTLE.CDP (0x0604CA7E: cmp/eq #64). The port's two tables
    must be those bytes, and the files the game asks for must be the ones the
    asset migrator writes (core/SaturnMusic.h kSaturnMusic).
    """
    print('battle music (GAME5 0x%08X / 0x%08X)' % (ARENA_BGM_GROUP_ADDR, ARENA_BGM_STATE_ADDR))
    groups = port_table(saturn_src, 'kArenaBgmGroup')
    states = port_table(saturn_src, 'kArenaBgmStateInit')
    if not check(groups is not None and len(groups) == 32 and
                 states is not None and len(states) == 32,
                 'kArenaBgmGroup / kArenaBgmStateInit are 32 rooms each'):
        return
    check(groups[16:] == [0xFF] * 16 and states[16:] == [0xFF] * 16,
          'rooms 16..31 (no arena) have no BGM')
    game5 = module_path('GAME5.PRG')
    if not game5:
        skips.append('assets/SATURN_ISO_CONTENT/GAME5.PRG')
        print('  SKIP no GAME5.PRG, BGM tables not compared against the disc')
    else:
        data = open(game5, 'rb').read()
        g = list(data[ARENA_BGM_GROUP_ADDR - PRG_BASE:][:16])
        s = list(data[ARENA_BGM_STATE_ADDR - PRG_BASE:][:16])
        check(groups[:16] == g, 'arena BGM groups match GAME5.PRG: %s' % ' '.join('%02X' % v for v in g))
        check(states[:16] == s, 'arena BGM states match GAME5.PRG: %s' % ' '.join('%02X' % v for v in s))
        cmp_at = 0x0604CA7E - PRG_BASE
        check(data[cmp_at:cmp_at + 2] == b'\x88\x40', 'GAME5 0x0604CA7E is cmp/eq #0x40 (the BATTLE.CDP group)')
        s_at = 0x0604C9B0 - PRG_BASE
        check(data[s_at:s_at + 16] == b'/SND/BATTLE.CDP\x00', 'GAME5 0x0604C9B0 is "/SND/BATTLE.CDP"')
    hdr = open(SATURN_HDR, encoding='utf-8', errors='replace').read()
    check(re.search(r'#define SATURN_BGM_GROUP_BATTLE\s+0x40\b', hdr) is not None,
          'SATURN_BGM_GROUP_BATTLE is 0x40')
    m = re.search(r'kNames\[3\]\s*=\s*\{([^}]*)\}', saturn_src)
    names = re.findall(r'"([^"]+)"', m.group(1)) if m else []
    music = open(os.path.join(REPO, 'tools', 'asset_migrator', 'src', 'core', 'SaturnMusic.h'),
                 encoding='utf-8', errors='replace').read()
    written = dict(((p, int(n)), name) for p, n, name in
                   re.findall(r'\{"([A-Z0-9]+\.CDP)", (\d), "([A-Za-z0-9_]+)"', music))
    check(names == [written.get(('BATTLE.CDP', 0)), written.get(('BATTLE.CDP', 1))],
          'the game\'s group 0x40 names %s are the migrator\'s BATTLE.CDP songs 0/1' % names)
    check(written.get(('BGM34B.CDP', 2)) == 'Bgm_24',
          "the migrator exports BGM34B.CDP song 2 under the PC's name Bgm_24 (mandatory set)")
    name_body = re.search(r'const char\* saturn_battle_bgm_name\([^)]*\)\s*\{(.*?)\n\}',
                          saturn_src, re.S)
    # The Saturn's arena 15 plays the STOCK Tyrant theme (user play-test,
    # 2026-09-30): group 0x34 must fall through to the PC's Bgm_24.
    check(bool(name_body) and 'return "Bgm_24"' not in name_body.group(1),
          "arena 15 plays the campaign's own Tyrant theme (group 0x34, Bgm_24)")
    check(bool(name_body) and 'g_bSaturnMode' in name_body.group(1) and
          'g_saturnBattleActive' in name_body.group(1),
          'the Saturn-only BGM names are gated on Saturn Battle mode')
    body = re.search(r'static void battle_begin_finale\(void\)\s*\{(.*?)\n\}', saturn_src, re.S)
    fanfare = body and re.search(r's_battle\.state = 3;.*?bgm_play_group_channel\(SATURN_BGM_GROUP_BATTLE, 1\)',
                                 body.group(1), re.S)
    check(bool(fanfare), 'the end of the run (state 3) plays group 0x40 channel 1, the fanfare')
    for fn in ('saturn_battle_bgm_group', 'saturn_battle_bgm_states'):
        fb = re.search(fn + r'\([^)]*\)\s*\{(.*?)\n\}', saturn_src, re.S)
        check(bool(fb) and 'g_saturnBattleActive' in fb.group(1) and 'g_bSaturnMode' in fb.group(1),
              '%s is gated on g_bSaturnMode && g_saturnBattleActive' % fn)


JPN_MODULES = os.path.join(REPO, 'assets', 'SATURN_JPN_ISO_CONTENT')
JPN_LOGO_DRAW = 0x0602D2E4      # Bio Hazard RANKING.PRG
USA_LOGO_DRAW = 0x0602D33C      # the USA module's copy, never called


def be32_hits(data, value):
    """Every 2-aligned big-endian dword equal to `value` (literal-pool slots)."""
    key = struct.pack('>I', value)
    return [i for i in range(0, len(data) - 3, 2) if data[i:i + 4] == key]


def check_jpn_results(saturn_src):
    """The Japanese release's results screens: its labels, prompts and logo.

    Bio Hazard's RANKING.PRG loads no WAPON3.BIN / SAVE2.BIN - it uploads the
    labels and prompts BATTLE.BIN carries itself (+0x35000, +0x31EB0), which the
    USA module overwrote - and draws a 3D "BIO HAZARD" from an SGL model inside
    the module. Both modules hold the model; only the Japanese one calls the
    draw. The port gates all three on the JPN tree, which is also what makes it
    read SATURN_JPN.
    """
    print('JPN results screens (Bio Hazard RANKING.PRG / BATTLE.BIN)')
    for name, want in (('SAT_JPN_LABELS_OFF', 0x35000), ('SAT_JPN_PROMPT_OFF', 0x31EB0),
                       ('SAT_JPN_PROMPT_PAL_OFF', 0x32EB0), ('SAT_LOGO_ROOT_JPN', 0x0603C18C)):
        m = re.search(r'#define\s+%s\s+(0x[0-9A-Fa-f]+)u?' % name, saturn_src)
        check(m is not None and int(m.group(1), 16) == want, '%s = 0x%X' % (name, want))
    overlay = open(os.path.join(REPO, 'tools', 'asset_migrator', 'src', 'core',
                                'SaturnOverlay.cpp'), encoding='utf-8').read()
    jf = re.search(r'jpnFiles\[\]\s*=\s*\{([^}]*)\}', overlay)
    check(jf is not None and '"RANKING.PRG"' in jf.group(1) and 'WAPON3' not in jf.group(1),
          'the migrator copies RANKING.PRG (the logo) and no WAPON3.BIN for the JPN disc')
    asset = open(os.path.join(REPO, 'src', 'system', 'AssetPath.cpp'), encoding='utf-8').read()
    check('"SATURN_JPN"' in asset and 's_assetIsJpn && _stricmp(mode, "SATURN")' in asset,
          'the SATURN mode reads SATURN_JPN on the JPN tree')

    rk_jpn = os.path.join(JPN_MODULES, 'RANKING.PRG')
    rk_usa = module_path('RANKING.PRG')
    bat = os.path.join(JPN_MODULES, 'BATTLE.BIN')
    if not (os.path.isfile(rk_jpn) and os.path.isfile(bat)):
        skips.append('assets/SATURN_JPN_ISO_CONTENT (python tools/saturn_iso.py --jpn extract ...)')
        print('  SKIP no extracted JPN disc, the module and BATTLE.BIN not compared')
        return
    jpn = open(rk_jpn, 'rb').read()
    m = re.search(r'#define\s+SAT_PRG_SIZE_JPN\s+(\d+)', saturn_src)
    check(m is not None and int(m.group(1)) == len(jpn),
          'SAT_PRG_SIZE_JPN is the JPN module\'s size (%d)' % len(jpn))
    check(b'WAPON3.BIN' not in jpn and b'SAVE2.BIN' not in jpn and b'BATTLE.BIN' in jpn,
          'the JPN module names BATTLE.BIN and neither WAPON3.BIN nor SAVE2.BIN')
    check(len(be32_hits(jpn, JPN_LOGO_DRAW)) == 7,
          'JPN RANKING calls the logo draw 0x%08X from 7 places' % JPN_LOGO_DRAW)
    if rk_usa:
        usa = open(rk_usa, 'rb').read()
        check(len(be32_hits(usa, USA_LOGO_DRAW)) == 0,
              'the USA module never calls its copy (0x%08X)' % USA_LOGO_DRAW)
    sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
    import saturn_ranking_logo
    region, parts = saturn_ranking_logo.load(rk_jpn)
    check(region == 'JPN' and len(parts) == 9 and sum(len(q) for _, q in parts) == 406,
          'the logo: 9 parts, 406 polygons (tools/saturn_ranking_logo.py)')
    b = open(bat, 'rb').read()
    lab = b[0x35000:]
    check(lab[:16] == b'X68 SSCONV v0.00' and
          struct.unpack_from('>2I', lab, 0x20) == (0x28, 0x18A8),
          'BATTLE.BIN +0x35000 has WAPON3.BIN\'s layout (cells +0x28, palette +0x18A8)')
    # The prompts: four 128x16 rows, the YES / NO letters where the port cuts them.
    pix = []
    for v in b[0x31EB0:0x32EB0]:
        pix += [v >> 4, v & 15]

    def span(row):
        xs = [x for y in range(row * 16, row * 16 + 16) for x in range(128) if pix[y * 128 + x]]
        return min(xs), max(xs)
    check(span(2) == (38, 81) and span(3) == (46, 74),
          'the YES / NO letters span u 38..81 / 46..74 of their rows')
    check('sat_draw(0, 38, SAT_PROMPT_V + 32, 44,' in saturn_src and
          'sat_draw(0, 46, SAT_PROMPT_V + 48, 29,' in saturn_src,
          'and the port cuts them there')


def main():
    if not os.path.isfile(SATURN_SRC):
        print('no %s' % SATURN_SRC, file=sys.stderr)
        return 1
    saturn_src = open(SATURN_SRC, encoding='utf-8', errors='replace').read()
    saturn_hdr = open(SATURN_HDR, encoding='utf-8', errors='replace').read()

    check_arena_table(saturn_src)
    check_spawn_table(saturn_src)
    check_item_box_loadout(saturn_src)
    check_virtual_doors(saturn_src)
    check_yawn()
    check_arena_extras(saturn_src)
    check_results_and_arenas_2(saturn_src)
    check_start_message(saturn_src)
    check_constants(saturn_src, saturn_hdr)
    check_gating()
    check_build_files()
    check_room_byte()
    check_arena_index_is_derived()
    check_start_flow()
    check_arena_sequence(saturn_src)
    check_battle_cdp()
    check_battle_music(saturn_src)
    check_arena_rooms()
    check_jpn_results(saturn_src)

    print()
    for s in skips:
        print('skipped: %s' % s)
    if failures:
        print('%d check(s) FAILED' % len(failures))
        return 1
    print('ok: the Saturn Battle Game port matches the disc')
    return 0


if __name__ == '__main__':
    sys.exit(main())
