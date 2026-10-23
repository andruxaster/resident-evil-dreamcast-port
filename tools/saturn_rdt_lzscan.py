"""Scan a Saturn .RDT for LZ77 stream offsets.

The room files are packed, and the decoder (tools/saturn_lz77.py) is verified.
If the packing is this same LZ77 applied per section, the section starts are
exactly the offsets that decode *and terminate* on a zero token.

Scoring on "decoded N bytes without violating the invariant" is far too weak:
high-entropy data has a first byte that reads as a long literal run, so most
offsets limp along 1:1 and hundreds of them clear a 1 KB bar.  A real stream
instead ends at a zero token, so that is the criterion used here.
"""
import sys
import importlib.util

sys.path.insert(0, "tools")
spec = importlib.util.spec_from_file_location("slz", "tools/saturn_lz77.py")
slz = importlib.util.module_from_spec(spec)
spec.loader.exec_module(slz)
from saturn_iso import Iso

NAME = sys.argv[1] if len(sys.argv) > 1 else "STAGE1/ROOM1060.RDT"
MAX_OUT = 0x100000      # 1 MB. A decoded room is 175-616 KB; the old 128 KB cap
                        # discarded the real hit at offset 0 as "limit".
MIN_OUT = 64

d = Iso().get(NAME)
print("%s: %d bytes (0x%X)" % (NAME, len(d), len(d)))

ends = []
partial = 0
for off in range(0, len(d) - 16):
    dec = slz.Decoder(d[off:])
    try:
        why = dec.run(limit=MAX_OUT)
    except ValueError:
        why = "violation"
    except IndexError:
        why = "overrun"
    if why == "end" and MIN_OUT <= len(dec.out) <= MAX_OUT:
        ends.append((len(dec.out), off, dec.sp))
    elif len(dec.out) >= 4096:
        partial += 1

print()
print("offsets terminating cleanly on a zero token: %d" % len(ends))
print("offsets reaching 4 KB without terminating (literal limpers): %d" % partial)
print()
if ends:
    ends.sort()
    for n, off, used in ends[:40]:
        ratio = n / used if used else 0
        print("  offset 0x%06X -> %7d bytes from %6d compressed (%.2fx)"
              % (off, n, used, ratio))
else:
    print("  none.  Either the sections are not this LZ77, or the streams are")
    print("  not at fixed offsets (an index table likely supplies them), in")
    print("  which case decoding needs the loader to walk that table first.")
