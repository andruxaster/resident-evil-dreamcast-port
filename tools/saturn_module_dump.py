#!/usr/bin/env python3
"""saturn_module_dump.py - dump a Saturn module's live image from RAM.

The eleven `.PRG` files on the disc are **partly** compressed.  Measured on
`GAME1.PRG`: the first ~26 KB is ordinary SH-2 code (5.9-6.6% branch density
across the first 96 KB, ~40% printable), and the tail past ~0x60000 is a
compressed payload that a stub at the front expands **in place** over its own
text - which is why live RAM matches the disc byte-for-byte up to +0x6710, then
diverges completely, and ends up 937,984 bytes against a 460,608-byte file.

So the disc image and the live image are complements, not alternatives, and the
live image is the superset.  Import that, at the module's load address, with
`SuperH:BE:32:SH-2`.  The emulator is a required part of the toolchain: the
expanded tail - the data tables and the build-path strings - only exists in RAM
after the module has run once.

This talks to Yaba Sanshiro's MCP debug server (127.0.0.1:9640, streamable-HTTP
JSON-RPC on `POST /mcp`, not registered in `.mcp.json`).

    python tools/saturn_module_dump.py list
    python tools/saturn_module_dump.py check TITLE          # disc vs live
    python tools/saturn_module_dump.py dump GAME1 [OUT]     # live image
    python tools/saturn_module_dump.py dump 0.BIN [OUT]     # the boot module

`dump` rejects an image whose branch density is at or below the ~0.44% random
floor, so a dump taken before a module has finished unpacking is refused rather
than silently imported.  It must *not* compare against the disc file's density:
the expanded tail legitimately has less branch density than the code prefix.
"""

import re
import argparse
import json
import os
import sys
import urllib.error
import urllib.request

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from saturn_iso import Iso

URL = "http://127.0.0.1:9640/mcp"
RAM_BASE = 0x06000000

# Load addresses, from tools/verify_saturn_ram.py (verified byte-for-byte
# against RAM at the moment of load).
OVERLAY_BASE = 0x0602B000
BOOT_BASE = 0x06004000
# How far to read from the overlay base.  TITLE.PRG unpacks to 0x64000, but
# GAME1.PRG is 460,608 bytes on the disc and will unpack to more, so read a
# generous span and let the trailing-zero trim find the real end.
OVERLAY_END = OVERLAY_BASE + 0x100000
BOOT_LEN = 86400
CHUNK = 0x10000

# SH-2 control-flow encodings, used to check that a dump is real code.
BRANCH = [(0x4F00, 0x4FFF), (0x8C00, 0x8CFF), (0x8900, 0x89FF), (0x8B00, 0x8BFF),
          (0x4AB0, 0x4ABF), (0x4B00, 0x4BFF), (0x000B, 0x000F), (0x0069, 0x006F),
          (0x0009, 0x0009)]


class Mcp(object):
    def __init__(self, url=URL, timeout=180):
        self.url = url
        self.timeout = timeout
        self.sid = None

    def _post(self, payload):
        req = urllib.request.Request(self.url, data=json.dumps(payload).encode(),
                                     method="POST")
        req.add_header("Content-Type", "application/json")
        req.add_header("Accept", "application/json, text/event-stream")
        if self.sid:
            req.add_header("Mcp-Session-Id", self.sid)
        try:
            with urllib.request.urlopen(req, timeout=self.timeout) as r:
                if r.headers.get("Mcp-Session-Id"):
                    self.sid = r.headers["Mcp-Session-Id"]
                body = r.read().decode("utf-8", "replace")
        except urllib.error.HTTPError as e:
            sid = e.headers.get("Mcp-Session-Id") if e.headers else None
            if sid and sid != self.sid:
                self.sid = sid
                return self._post(payload)
            raise
        if not body.strip():
            return {}
        if body.lstrip().startswith("event:"):
            for line in body.splitlines():
                if line.startswith("data:"):
                    return json.loads(line[5:].strip())
            raise RuntimeError("no data frame in SSE response")
        return json.loads(body)

    def handshake(self):
        self._post({"jsonrpc": "2.0", "id": 1, "method": "initialize",
                    "params": {"protocolVersion": "2025-06-18", "capabilities": {},
                               "clientInfo": {"name": "saturn_module_dump", "version": "1.0"}}})
        self._post({"jsonrpc": "2.0", "method": "notifications/initialized"})

    def call(self, name, args):
        r = self._post({"jsonrpc": "2.0", "id": 2, "method": "tools/call",
                        "params": {"name": name, "arguments": args}})
        if "error" in r:
            raise RuntimeError(r["error"])
        res = r["result"]
        if res.get("isError"):
            raise RuntimeError(res["content"][0]["text"])
        return res["content"][0]["text"]

    def status(self):
        return self.call("debug_status", {})

    def read(self, addr, length):
        txt = self.call("memory_read", {"address": "0x%08X" % addr,
                                        "length": length, "format": "hexdump"})
        # The emulator prints `AAAAAAAA: XX XX .. XX  ascii` - there is NO `|`
        # separator. Taking every 2-char token from the whole line also read
        # the ASCII column, whose pieces split on spaces, and any piece that
        # looked like hex (`AB`, `0F`) was appended as a byte. That shifted the
        # old GAME1.ram by 1-2 bytes in patches. Read exactly the hex column
        # and place each byte by its line address.
        got = {}
        for line in txt.splitlines():
            m = re.match(r"^([0-9A-Fa-f]{8}):((?: [0-9A-Fa-f]{2}){1,16})(?: |$)", line)
            if not m:
                continue
            base = int(m.group(1), 16)
            for k, t in enumerate(m.group(2).split()):
                got[base + k] = int(t, 16)
        out = bytearray()
        for a in range(addr, addr + length):
            if a not in got:
                break
            out.append(got[a])
        return bytes(out)


def control_flow(b):
    n = 0
    for o in range(0, len(b) - 1, 2):
        w = (b[o] << 8) | b[o + 1]
        for lo, hi in BRANCH:
            if lo <= w <= hi:
                n += 1
                break
    return n


def resolve(name):
    """name -> (file on disc, load address, slot length)."""
    iso = Iso()
    if name.upper() == "0.BIN":
        return iso, "0.BIN", BOOT_BASE, BOOT_LEN
    key = name.upper() if name.upper().endswith(".PRG") else name.upper() + ".PRG"
    e = iso.find(key)
    if not e:
        raise SystemExit("no such module on the disc: %s" % name)
    return iso, key.lstrip("/"), OVERLAY_BASE, OVERLAY_END - OVERLAY_BASE


def read_range(mcp, base, length):
    buf = bytearray()
    off = 0
    while off < length:
        got = mcp.read(base + off, min(CHUNK, length - off))
        if not got:
            break
        buf += got
        off += len(got)
    return bytes(buf)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("cmd", choices=["list", "status", "check", "dump"])
    ap.add_argument("module", nargs="?")
    ap.add_argument("out", nargs="?")
    ap.add_argument("--url", default=URL)
    args = ap.parse_args()

    if args.cmd == "list":
        iso = Iso()
        for k in sorted(iso._list):
            if k.upper().endswith(".PRG"):
                print("%-28s %8d bytes   overlay 0x%08X" % (k.lstrip("/"), iso._list[k][2], OVERLAY_BASE))
        print("%-28s %8d bytes   boot    0x%08X" % ("0.BIN", BOOT_LEN, BOOT_BASE))
        print()
        print("Each module loads verbatim at its base (only runtime data differs),")
        print("so the disc file can be imported directly.  To compare or dump the")
        print("live image, including BSS past the file's end:")
        print("    python tools/saturn_module_dump.py dump <module>")
        return 0

    if not args.module:
        ap.error("%s needs a module name" % args.cmd)
    iso, fname, base, span = resolve(args.module)
    disc = iso.get(fname)

    mcp = Mcp(args.url)
    try:
        mcp.handshake()
    except Exception as e:
        print("cannot reach the emulator at %s: %s" % (args.url, e))
        print("start Yaba Sanshiro with the Saturn disc and the module you want,")
        print("let it run so the module has unpacked, then retry.")
        return 1

    if args.cmd == "status":
        print(mcp.status())
        return 0

    print("%s: %d bytes on disc, load address 0x%08X" % (fname, len(disc), base))
    if args.cmd == "check":
        live = read_range(mcp, base, max(len(disc), 0x10000))
        n = min(len(disc), len(live))
        same = sum(1 for i in range(n) if disc[i] == live[i])
        print("  disc vs live, first %d bytes: %d equal (%.1f%%)" % (n, same, 100.0 * same / max(1, n)))
        print("  -> %s" % ("identical, so NOT compressed (or not yet unpacked)"
                            if same == n else
                            "differs, so the module is COMPRESSED on disc"))
        return 0

    print("  dumping the live slot, 0x%08X .. 0x%08X ..." % (base, base + span))
    live = read_range(mcp, base, span)
    # Trim the tail, but only a *fully* zero block: a block that is merely
    # mostly zero can be real data.  Trimming at "> 512 zeros in 4096" cut
    # 0.BIN's live image short (65,921 bytes against an 86,400-byte file) and
    # threw away live code.
    end = len(live)
    while end >= 0x1000 and live[end - 0x1000:end].count(0) == 0x1000:
        end -= 0x1000
    live = live[:end]

    # A module loads verbatim (GAME1: 309 of 460,608 bytes differ, all runtime
    # data); the live image is longer only by its BSS. (An older note here
    # called the modules "partly compressed" - that came from a byte-shifted
    # dump and was wrong.) The guard is therefore not a control-flow
    # comparison: the only real failure mode is a dump that is not code at all,
    # which the random baseline catches: ~0.44% of words.
    n = min(len(disc), len(live))
    prefix = 0
    for i in range(n):
        if disc[i] != live[i]:
            break
        prefix = i + 1
    d_cf, l_cf = control_flow(disc), control_flow(live)
    l_words = max(1, len(live) // 2)
    print("  live image: %d bytes (%.2fx the disc file)" % (len(live), len(live) / float(len(disc))))
    print("  verbatim code prefix shared with the disc: %d bytes (%.2fx the file)"
          % (prefix, prefix / float(len(disc))))
    print("  control-flow words: disc %d (%.2f%% of words), live %d (%.2f%%)"
          % (d_cf, 100.0 * d_cf / max(1, len(disc) // 2), l_cf, 100.0 * l_cf / l_words))
    if 100.0 * l_cf / l_words < 1.0:
        print("  REFUSING: the live image is not code (%.2f%% branch density is at"
              % (100.0 * l_cf / l_words))
        print("  or below the noise floor).  The module has probably not finished")
        print("  unpacking - run the game until this screen appears, then retry.")
        return 1
    out = args.out or ("assets/SATURN_RAM_DUMP/%s.ram" % os.path.splitext(fname)[0].replace(".", "_"))
    os.makedirs(os.path.dirname(out), exist_ok=True)
    open(out, "wb").write(live)
    print("  wrote %s (%d bytes)" % (out, len(live)))
    print("  import into Ghidra at 0x%08X with SuperH:BE:32:SH-2" % base)
    return 0


if __name__ == "__main__":
    sys.exit(main())
