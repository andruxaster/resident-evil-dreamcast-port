"""Saturn LZ77 - validated decoder.

Parameters captured live on a room load (room 106):
    src = 0x25E01990   (r4 at entry to 0x602B040)
    dst = 0x25C0D800   (r5 at entry)
plus the two sign-extended masks read out of r8/r9: 0xFFFFE000 / 0xFFFFFF00.

Bit reader (from the listing, with BF/S polarity = branches when T == 0):
    r6 = *r4++            first byte is the bit buffer, r4 already at the 2nd
    r7 = 9                first DT yields 8, so no refill before the 1st bit
    per iteration: r7--; if r7 != 0 -> just r6 >>= 1; else r6 = *r4++, r7 = 8,
                   r6 >>= 1.  One bit consumed per iteration, LSB first.
    set bit -> literal, taken from r4 and r4++.
    clear  -> one more bit selects the form.
    short (0): two more bits -> length = bits + 2; one source byte | 0xFFFFFF00
    long  (1): two source bytes, token = (hi<<8)|lo; token 0 ends the stream;
               length = (hi & 7) + 2 if that field != 0, else the *next* source
               byte used verbatim (the branch skips the add #2);
               offset = (token >> 3) | 0xFFFFE000
    copy: byte at a time, both pointers post-incremented, so distance 1 is a
          run.  The reference is dst-relative to the CURRENT r5.
"""


def s16(v):
    return v - 0x10000 if v & 0x8000 else v


class Decoder:
    def __init__(self, src, seed=b"", long_mask=0xFFFFE000, short_mask=0xFFFFFF00,
                 short_bias=2, zero_field_bias=1):
        self.src = src
        self.out = bytearray(seed)
        self.r6 = src[0]
        self.sp = 1
        self.r7 = 9
        self.long_mask = long_mask
        self.short_mask = short_mask
        self.short_bias = short_bias
        self.zero_field_bias = zero_field_bias
        self.tokens = 0
        self.trace = []

    def _bit(self):
        self.r7 -= 1
        if self.r7 == 0:
            self.r6 = self.src[self.sp]
            self.sp += 1
            self.r7 = 8
        # BT tests the C flag produced by SHLR, i.e. the bit that was bit 0
        # BEFORE the shift - not the new bit 0.
        bit = self.r6 & 1
        self.r6 >>= 1
        return bit

    def run(self, limit=None):
        while True:
            if limit is not None and len(self.out) >= limit:
                return "limit"
            while self._bit():
                self.out.append(self.src[self.sp])
                self.sp += 1
            long_form = self._bit()
            if long_form:
                # `extu.b r0,r2` puts the FIRST byte in the low half and
                # `shll8 r1` shifts the SECOND byte left into the high half,
                # so `or r1,r2` = (second << 8) | first: the token is
                # LITTLE-endian, and `and #7,r0` takes the length field from
                # the first byte.
                first = self.src[self.sp]
                second = self.src[self.sp + 1]
                self.sp += 2
                token = first | (second << 8)
                if token == 0:
                    return "end"
                field = first & 7
                if field != 0:
                    length = field + 2
                else:
                    length = self.src[self.sp] + self.zero_field_bias
                    self.sp += 1
                offset = (token >> 3) | self.long_mask
            else:                                            # short form
                b1 = self._bit()
                b2 = self._bit()
                length = ((1 if b1 else 0) << 1 | (1 if b2 else 0)) + self.short_bias
                byte = self.src[self.sp]
                self.sp += 1
                # sext8(byte) | 0xFFFFFF00 -> -256..-1  (r9, sign extended)
                offset = ((byte - 256) if byte > 127 else byte) & 0xFFFFFFFF
                offset |= self.short_mask
            offset = offset - 0x100000000 if offset >= 0x80000000 else offset
            pos = len(self.out) + offset
            self.tokens += 1
            self.trace.append((self.tokens, self.sp, len(self.out), offset,
                               length, "long" if long_form else "short"))
            if pos < 0:
                # A valid distance can never exceed the bytes written so far:
                # memory below dst is unmapped, so dst is the true start of the
                # output region.  Report the first violation with context.
                lo = max(0, self.sp - 12)
                raise ValueError(
                    "back-ref %d short: token %d, form %s, len %d, "
                    "needs %d bytes of history, have %d\n"
                    "  sp=%d src=[%s]\n  src[%d:]=%s\n"
                    "  out[-16:]=%s"
                    % (-pos, self.tokens, "long" if long_form else "short",
                       length, -offset, len(self.out), self.sp,
                       " ".join("%02X" % b for b in self.src[lo:self.sp]),
                       lo, " ".join("%02X" % b for b in self.src[lo:lo + 24]),
                       " ".join("%02X" % b for b in self.out[-16:])))
            for _ in range(length):
                if pos >= len(self.out):
                    raise ValueError("back-ref past end: out=%d pos=%d"
                                     % (len(self.out), pos))
                self.out.append(self.out[pos])
                pos += 1


if __name__ == "__main__":
    import sys
    sys.path.insert(0, "tools")
    from verify_saturn_ram import Mcp
    m = Mcp()
    m.handshake()
    SRC, DST = 0x25E29C2C, 0x25E60040
    raw = m.read_hexdump(SRC, 0x2000)
    src = bytes.fromhex(raw.replace("\n", " ").split("|")[0].split(":")[-1].strip().replace(" ", "")) \
        if False else None
    # use memory_read which returns a hexdump; parse the ASCII-clean hex column
    txt = str(m.call("memory_read", {"address": hex(SRC), "length": 0x2000}))
    hexs = []
    for line in txt.splitlines():
        if ":" in line and line.count(" ") >= 8:
            head = line.split(":")[-1]
            for tok in head.split():
                if len(tok) == 2:
                    try:
                        int(tok, 16)
                        hexs.append(tok)
                    except ValueError:
                        pass
    src = bytes(int(h, 16) for h in hexs)
    print("parsed %d source bytes from 0x%08X" % (len(src), SRC))

    d = Decoder(src)
    try:
        why = d.run(limit=0x400)
    except Exception as e:
        why = "error: %s" % e
    print("stopped: %s  after %d tokens, %d source bytes, %d output bytes"
          % (why, d.tokens, d.sp, len(d.out)))
    print("first 64 output bytes: %s" % d.out[:64].hex(" "))
