# Simulate the zone-graph walkers zone_walk_cw (0x0045fae0) / zone_walk_ccw
# (0x0045fdb0) exactly as the original runs them, over the real RDT bytes.
#
# The model the walkers use (see the scratch note in EntityCommon.cpp):
#   - ONE zone array, idx[] at 0x00be0ee0. The walkers also address it at
#     0x00be0ee1, so idx[i+1] is depth i's scan cursor (the last neighbour of
#     idx[i] tried): seeded 0xFF (cw) / zone count (ccw) when depth i is
#     entered, set to the target on a goal hit. GAME4 0x06062810 (the Saturn's
#     cw walker) spells the same cursor out as idx[i+1].
#   - ONE point array, P[] at 0x00be0efc (stride 4). 0x00be0f00 + 4i reads
#     P(i+1): where the walk entered the zone at depth i (P(1) = the entity).
#   - flags / edges are read straight from the RDT bytes, so a zone index past
#     the table (an entity or target outside every zone = 0xFF) reads what the
#     original reads.
#
# An older version of this file modelled the scratch as a 37-byte block that
# kept only two points (every deeper distance read 0), and compared it with a
# "bounded" port rewrite that split the arrays. That rewrite could not reach a
# lower-numbered neighbour on a cw walk - the Saturn's second lab Tyrant, from
# ROOM5130 zone 12, found no path to the terminal and circled. Both are gone.
#
#   python tools/sim_zone_walk.py              every USA room: termination + depth
#   python tools/sim_zone_walk.py ROOM.RDT A B  one walk pair, both directions,
#                                               target at zone B's centre
import struct, glob, math, sys

def isqrt(v):
    return int(math.isqrt(v)) if v > 0 else 0

class Room:
    def __init__(self, path):
        self.d = open(path, 'rb').read()
        if len(self.d) < 0x5C:
            raise ValueError('stub RDT')
        self.off = struct.unpack_from('<I', self.d, 0x58)[0]
        self.count = self.d[self.off] if 0 < self.off < len(self.d) else 0

    def ent(self, i):
        """Zone record i: x1, z1, x2, z2, field, adjacency flags. Past the end
        of the file reads as zeros (the original would read whatever follows
        the RDT in memory)."""
        b = self.off + 2 + i * 12
        if b + 12 > len(self.d):
            return (0,) * 6
        return struct.unpack_from('<6H', self.d, b)

def shared_edge(R, a, b, D):
    """walk_zone_shared_edge 0x004602b0 into D = [displacement, distance_z];
    a component the original leaves alone keeps its previous value."""
    A, B = R.ent(a), R.ent(b)
    if B[2] == A[0]:
        D[0] = A[0]; D[1] = (max(A[1], B[1]) + min(A[3], B[3])) >> 1; return
    if A[2] == B[0]:
        D[0] = B[0]; D[1] = (max(A[1], B[1]) + min(A[3], B[3])) >> 1; return
    if B[3] == A[1]:
        D[1] = A[1]
    elif A[3] == B[1]:
        D[1] = B[1]
    D[0] = (max(A[0], B[0]) + min(A[2], B[2])) >> 1

def walk(R, cw, target, tx, tz, start, entity, cap=2_000_000):
    """Returns (first step zone or 0xFF, iterations, deepest depth), or
    ('HANG', ...) past `cap` iterations."""
    idx = [0] * 64; P = [(0, 0)] * 64; best = [0] * 64
    seed = 0xFF if cw else R.count
    idx[0] = start; idx[1] = seed; P[1] = entity
    tbit = 1 << (target & 31)
    bestd = 0xFFFFFFFF; dist = 0; s = 0; it = 0; deepest = 0
    D = [0, 0]
    def dd(a, b):
        return isqrt((a[0] - b[0]) ** 2 + (a[1] - b[1]) ** 2)
    while True:
        it += 1
        if it > cap:
            return ('HANG', it, deepest)
        i = s
        deepest = max(deepest, s)
        flags = R.ent(idx[i])[5]
        dec = True
        if flags & tbit:
            idx[i + 1] = target
            shared_edge(R, idx[i], target, D)
            dist += dd(P[i + 1], D) + dd(D, (tx, tz))
            if dist < bestd:
                bestd = dist
                for n in range(s + 1, 0, -1):
                    best[n] = idx[n]
            if s:
                dist -= dd(D, (tx, tz)) + dd(P[i + 1], D) + dd(P[i], P[i + 1])
        else:
            if cw:
                mask = ~((1 << (((idx[i + 1] + 1) & 0xFF) & 31)) - 1)
            else:
                mask = (1 << (idx[i + 1] & 31)) - 1
            if (flags & mask) == 0 or s + 2 >= 32:     # 32 = the port's array size
                if s:
                    dist -= dd(P[i], P[i + 1])
            else:
                s += 1
                while True:
                    idx[s] = (idx[s] + (1 if cw else -1)) & 0xFF
                    if flags & (1 << (idx[s] & 31)):
                        break
                z = idx[s]
                if not any(idx[b] == z for b in range(s - 2, -1, -1)):
                    shared_edge(R, idx[s], idx[s - 1], D)
                    d = dd(P[s], D)
                    dist += d
                    if dist < bestd:
                        P[s + 1] = (D[0], D[1]); idx[s + 1] = seed; dec = False
                    else:
                        dist -= d
        if dec:
            s -= 1
        if s < 0:
            return (best[1] if bestd != 0xFFFFFFFF else 0xFF, it, deepest)

def centre(R, z):
    E = R.ent(z if z != 0xFF else 0)
    return (E[0] + E[2]) >> 1, (E[1] + E[3]) >> 1

def main():
    if len(sys.argv) == 4:
        R = Room(sys.argv[1]); a = int(sys.argv[2], 0); b = int(sys.argv[3], 0)
        for cw in (True, False):
            r = walk(R, cw, b, *centre(R, b), a, centre(R, a))
            print('cw ' if cw else 'ccw', 'first step', r[0], 'iterations', r[1], 'depth', r[2])
        return
    total = hangs = deepest = 0
    worst = (0, None)
    for f in sorted(glob.glob('assets/USA/**/ROOM*.RDT', recursive=True)):
        try:
            R = Room(f)
        except Exception:
            continue
        if R.count < 2:
            continue
        zones = list(range(R.count)) + [0xFF]
        for a in zones:
            for b in zones:
                if a == b:
                    continue
                for cw in (True, False):
                    total += 1
                    r = walk(R, cw, b, *centre(R, b), a, (0x1000, 0x1000))
                    if r[0] == 'HANG':
                        hangs += 1
                        print('HANG', f, a, b, 'cw' if cw else 'ccw')
                    deepest = max(deepest, r[2])
                    if r[1] > worst[0]:
                        worst = (r[1], (f, a, b, 'cw' if cw else 'ccw'))
    print(f'walks: {total}  hangs: {hangs}  deepest: {deepest}')
    print(f'most iterations: {worst[0]} at {worst[1]}')

if __name__ == '__main__':
    main()
