#!/usr/bin/env python3
"""Read a Sega Saturn track-1 image as an ISO9660 filesystem.

The Saturn disc of "Resident Evil" (USA) is a plain MODE1/2352 track; sector 0
is IP.BIN, sector 16 the ISO9660 Primary Volume Descriptor (system id
"SEGA SEGASATURN", volume id "RESIDENT_EVIL").  Everything above 2048 bytes of
a sector is sync / header / ECC and is discarded, so this is a normal ISO read
with a 16-byte offset - no Saturn-specific container involved.

The Japanese "Bio Hazard" disc (`--jpn`) is a cooked MODE1/2048 .iso
(volume id "BIOHAZARD"); the sector layout is detected from where the
"CD001" signature of the PVD sits, so either image (or `--track PATH`) works.

    python tools/saturn_iso.py [--jpn | --track PATH] <command> ...
    python tools/saturn_iso.py list
    python tools/saturn_iso.py tree
    python tools/saturn_iso.py info <path>
    python tools/saturn_iso.py cat <path> > file.bin
    python tools/saturn_iso.py extract <outdir> [--filter SUBSTR]
"""

import os
import struct
import sys

SECTOR = 2352
USER_OFFSET = 16
USER_SIZE = 2048
BLOCK = USER_SIZE

TRACK = os.path.join(
    "assets", "Resident Evil - Saturn (USA)",
    "Resident Evil (USA) (Track 1).bin")
TRACK_JPN = os.path.join(
    "assets", "Bio Hazard (J)(Saturn)",
    "Bio Hazard (J) T01 Mode1 2048.iso")


def detect_layout(f):
    """(sector size, user-data offset) of an image: 2048/0, 2352/16 or 2352/24."""
    for sector, offset in ((2048, 0), (2352, 16), (2352, 24)):
        f.seek(16 * sector + offset + 1)
        if f.read(5) == b"CD001":
            return sector, offset
    raise ValueError("no ISO9660 PVD at sector 16")


def path_key(p):
    p = p.replace("\\", "/")
    if not p.startswith("/"):
        p = "/" + p
    return p.upper()


class Iso:
    def __init__(self, track=TRACK):
        self.path = track
        self.f = open(track, "rb")
        self.sector, self.user_offset = detect_layout(self.f)
        n = os.path.getsize(track)
        self.sectors = n // self.sector
        self.pvd = self.read_lba(16)
        self.volsize = struct.unpack_from("<I", self.pvd, 80)[0]
        self.volid = self.pvd[40:72].decode("latin1").rstrip()
        self.sysid = self.pvd[8:40].decode("latin1").rstrip()
        self.set_size = struct.unpack_from("<H", self.pvd, 120)[0]
        self._list = {}
        self._walk()

    # -- raw -----------------------------------------------------------------
    def read_lba(self, lba, count=1):
        out = []
        self.f.seek(lba * self.sector)
        for _ in range(count):
            b = self.f.read(self.sector)
            if len(b) < self.sector:
                out.append(b"")
                continue
            out.append(b[self.user_offset:self.user_offset + USER_SIZE])
        return out if count > 1 else out[0]

    def read_file(self, lba, size):
        data = bytearray()
        while len(data) < size:
            chunk = self.read_lba(lba + len(data) // BLOCK)
            if not chunk:
                break
            data += chunk
        return bytes(data[:size])

    # -- iso9660 -------------------------------------------------------------
    def _parse_dir(self, lba, size):
        recs = []
        for blk in range((size + BLOCK - 1) // BLOCK):
            data = self.read_lba(lba + blk)
            off = 0
            while off < len(data):
                ln = data[off]
                if ln == 0:
                    break
                rec = data[off:off + ln]
                off += ln
                elba = struct.unpack_from("<I", rec, 2)[0]
                esize = struct.unpack_from("<I", rec, 10)[0]
                flags = rec[25]
                idlen = rec[32]
                name = rec[33:33 + idlen].decode("latin1")
                if ";" in name:          # ISO9660 ";1" version suffix
                    name = name.split(";", 1)[0]
                recs.append((name, elba, esize, flags))
        return recs

    def _walk(self):
        root = self.pvd[156:190]
        rlba = struct.unpack_from("<I", root, 2)[0]
        rsize = struct.unpack_from("<I", root, 10)[0]
        self._recurse("", rlba, rsize)

    def _recurse(self, prefix, lba, size):
        for name, elba, esize, flags in self._parse_dir(lba, size):
            if name in ("\x00", "\x01"):
                continue
            full = prefix + "/" + name
            self._list[path_key(full)] = (name, elba, esize, flags)
            if flags & 0x02 and name != "\x00":
                self._recurse(full, elba, esize)

    # -- api -----------------------------------------------------------------
    def entries(self):
        return sorted(self._list.items(), key=lambda kv: kv[0])

    def find(self, path):
        return self._list.get(path_key(path))

    def get(self, path):
        e = self.find(path)
        if not e:
            raise KeyError(path)
        return self.read_file(e[1], e[2])

    def folders(self):
        seen = set()
        for key in self._list:
            parent = key.rsplit("/", 1)[0]
            if parent:
                seen.add(parent)
        return sorted(seen)


def cmd_list(iso, args):
    for key, (name, lba, size, flags) in iso.entries():
        kind = "DIR " if flags & 0x02 else "    "
        print("%s %10d  LBA %7d  %s" % (kind, size, lba, key))
    total = sum(s for _, (_, _, s, f) in iso.entries() if not f & 0x02)
    print("--- %d entries, %d bytes in files" % (len(iso._list), total))


def cmd_tree(iso, args):
    for d in iso.folders():
        print("%s/" % d)
        for key, (name, lba, size, flags) in iso.entries():
            if key.rsplit("/", 1)[0] != d or flags & 0x02:
                continue
            print("    %-14s %10d" % (name, size))


def cmd_info(iso, args):
    e = iso.find(args[0])
    if not e:
        print("not found: %s" % args[0])
        return 1
    name, lba, size, flags = e
    print("path : %s" % args[0])
    print("name : %s" % name)
    print("lba  : %d  (byte %d)" % (lba, lba * iso.sector))
    print("size : %d" % size)
    print("dir  : %s" % bool(flags & 0x02))
    return 0


def cmd_cat(iso, args):
    e = iso.find(args[0])
    if not e:
        print("not found: %s" % args[0], file=sys.stderr)
        return 1
    sys.stdout.buffer.write(iso.get(args[0]))
    return 0


def cmd_extract(iso, args):
    out = args[0]
    filt = None
    if "--filter" in args:
        filt = args[args.index("--filter") + 1].upper()
    n = 0
    total = 0
    for key, (name, lba, size, flags) in iso.entries():
        if flags & 0x02:
            continue
        if filt and filt not in key:
            continue
        rel = key.lstrip("/")
        dst = os.path.join(out, *rel.split("/"))
        os.makedirs(os.path.dirname(dst), exist_ok=True)
        with open(dst, "wb") as g:
            g.write(iso.get(key))
        n += 1
        total += size
    print("extracted %d files, %d bytes -> %s" % (n, total, out))
    return 0


def main(argv):
    argv = list(argv)
    track = TRACK
    if "--jpn" in argv:
        argv.remove("--jpn")
        track = TRACK_JPN
    if "--track" in argv:
        i = argv.index("--track")
        track = argv[i + 1]
        del argv[i:i + 2]
    if not os.path.exists(track):
        print("missing %s" % track)
        return 1
    if len(argv) < 2:
        print(__doc__)
        return 1
    iso = Iso(track)
    cmd = argv[1]
    args = argv[2:]
    if cmd == "info" and not args:
        print("volume id : %s" % iso.volid)
        print("system id : %s" % iso.sysid)
        print("blocks    : %d (%d bytes)" % (iso.volsize, iso.volsize * BLOCK))
        print("sectors   : %d in track 1 (%d bytes, user data at +%d)"
              % (iso.sectors, iso.sector, iso.user_offset))
        print("set size  : %d" % iso.set_size)
        print("entries   : %d" % len(iso._list))
        print("folders   : %d" % len(iso.folders()))
        for d in iso.folders():
            print("    %s" % d)
        return 0
    table = dict(list=cmd_list, tree=cmd_tree, info=cmd_info, cat=cmd_cat,
                 extract=cmd_extract)
    if cmd not in table:
        print(__doc__)
        return 1
    return table[cmd](iso, args) or 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
