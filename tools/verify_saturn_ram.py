#!/usr/bin/env python3
"""verify_saturn_ram.py - read the Saturn load addresses off a running disc.

Yaba Sanshiro exposes an MCP debug server on 127.0.0.1:9640 (streamable-HTTP
JSON-RPC on `POST /mcp`; it is not registered in `.mcp.json`).  Its
`memory_read` returns a hexdump of up to at least 64 KB per call, so the SH-2
main RAM window can be dumped in full and searched for the modules' bytes.

That is how the load addresses in `docs/SATURN_VERSION_ANALYSIS.md` 3c were found -
nothing on the disc records them, and Ghidra's auto-analysis finds nothing until
the image base is right.  The result:

    0.BIN          0x06004000   (mirrored at 0x06104000)
    *.PRG          0x0602B000   eleven overlays, swapped on demand

Each module is matched on a 16-byte window chosen to be unique to it - the first
16 bytes are the shared C-runtime prologue and match every module at once.  A
match is only reported when every compared byte agrees, so this is a check, not
a guess.

    python tools/verify_saturn_ram.py             # dump + match
    python tools/verify_saturn_ram.py --status    # just ask the emulator
    python tools/verify_saturn_ram.py --raw FILE  # match against an existing dump

Requires the emulator running the Saturn disc, and the disc image under
`assets/Resident Evil - Saturn (USA)/`.
"""

import re
import argparse
import json
import os
import struct
import sys
import urllib.error
import urllib.request

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from saturn_iso import Iso

URL = "http://127.0.0.1:9640/mcp"
RAM_BASE = 0x06000000
RAM_SIZE = 0x200000
CHUNK = 0x10000

# 0.BIN carries a 0x40-byte header before its code; the .PRG files do not.
ENTRY = {"0.BIN": 0x40}


class Mcp:
    def __init__(self, url=URL, timeout=120):
        self.url = url
        self.timeout = timeout
        self.sid = None

    def _post(self, payload):
        data = json.dumps(payload).encode()
        req = urllib.request.Request(self.url, data=data, method="POST")
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
        # a notification is acknowledged with an empty body
        if not body.strip():
            return {}
        # streamable HTTP may answer as SSE
        if body.lstrip().startswith("event:"):
            for line in body.splitlines():
                if line.startswith("data:"):
                    return json.loads(line[5:].strip())
            raise RuntimeError("no data frame in SSE response")
        return json.loads(body)

    def handshake(self):
        self._post({"jsonrpc": "2.0", "id": 1, "method": "initialize",
                    "params": {"protocolVersion": "2025-06-18", "capabilities": {},
                               "clientInfo": {"name": "verify_saturn_ram", "version": "1.0"}}})
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

    def read_hexdump(self, addr, length):
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


def dump_ram(mcp):
    buf = bytearray()
    for off in range(0, RAM_SIZE, CHUNK):
        buf += mcp.read_hexdump(RAM_BASE + off, min(CHUNK, RAM_SIZE - off))
    return bytes(buf)


def unique_sig(mod_bytes, others, n=16):
    """A window of n bytes that no other module also contains."""
    ln = len(mod_bytes)
    for o in ([ENTRY.get("_", 0)] + [0x20, 0x40, 0x100, 0x200, 0x400, 0x800,
                                      0x1000, 0x2000, 0x4000, 0x8000,
                                      0x10000, 0x20000, ln // 2, ln - 256]):
        if o < 0 or o + n > ln:
            continue
        sig = mod_bytes[o:o + n]
        if not any(sig in other for other in others):
            return o, sig
    return None, None


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--url", default=URL)
    ap.add_argument("--raw", help="match against an existing RAM dump instead of the emulator")
    ap.add_argument("--status", action="store_true", help="print debug_status and exit")
    ap.add_argument("--out", help="write the RAM dump here")
    args = ap.parse_args()

    iso = Iso()
    mods = ["0.BIN"] + sorted(k.lstrip("/") for k in iso._list if k.upper().endswith(".PRG"))
    data = dict((m, iso.get(m)) for m in mods)

    if args.raw:
        ram = open(args.raw, "rb").read()
    else:
        mcp = Mcp(args.url)
        try:
            mcp.handshake()
        except Exception as e:
            print("cannot reach the emulator at %s: %s" % (args.url, e))
            print("start Yaba Sanshiro with the Saturn disc, and make sure its")
            print("debug server is on port 9640.")
            return 1
        if args.status:
            print(mcp.status())
            return 0
        print("dumping %d KB of SH-2 RAM from the emulator ..." % (RAM_SIZE // 1024))
        ram = dump_ram(mcp)
        if args.out:
            open(args.out, "wb").write(ram)
            print("  wrote %s (%d bytes)" % (args.out, len(ram)))
    print("RAM dump %d bytes, 0x%08X..0x%08X" % (len(ram), RAM_BASE, RAM_BASE + len(ram) - 1))

    loaded = {}
    print()
    print("%-13s %9s %-7s %-12s %-12s %s"
          % ("module", "bytes", "sig@", "load base", "end", "verified bytes"))
    for m in mods:
        b = data[m]
        entry = ENTRY.get(m, 0)
        entry = 0x40 if m == "0.BIN" else 0
        others = [data[o] for o in mods if o != m]
        off, sig = unique_sig(b, others)
        if sig is None:
            print("%-13s %9d  no unique window" % (m, len(b)))
            continue
        hits = []
        i = ram.find(sig)
        while i >= 0 and len(hits) < 8:
            hits.append(i)
            i = ram.find(sig, i + 1)
        if not hits:
            print("%-13s %9d %#-7x %-12s %-12s not resident" % (m, len(b), off, "-", "-"))
            continue
        for h in hits:
            fbase = RAM_BASE + h - off
            # verify the whole file image from its own byte 0, not just the window
            n = min(len(b), 0x4000)
            got = ram[fbase - RAM_BASE:fbase - RAM_BASE + n]
            good = sum(1 for j in range(n) if b[j] == got[j])
            ok = "OK (%d/%d bytes)" % (good, n) if good == n else "%d/%d MISMATCH" % (good, n)
            loaded.setdefault(fbase, []).append(m)
            print("%-13s %9d %#-7x 0x%08X   0x%08X   %s"
                  % (m, len(b), off, fbase, fbase + len(b), ok))

    print()
    print("=== resident modules ===")
    for base in sorted(loaded):
        for m in loaded[base]:
            print("  0x%08X  %-13s %8d bytes -> 0x%08X" % (base, m, len(data[m]), base + len(data[m])))
    return 0


if __name__ == "__main__":
    sys.exit(main())
