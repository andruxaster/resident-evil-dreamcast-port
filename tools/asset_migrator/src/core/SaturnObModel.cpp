// SaturnObModel.cpp - ENEMY/OBnn.DAT -> PS1/PC TMD. See SaturnObModel.h.
#include "core/SaturnObModel.h"

#include "core/SaturnLz77.h"

#include <cmath>
#include <cstring>

namespace re1 {

namespace {

uint32_t be32(const std::vector<uint8_t>& d, size_t o) {
    return o + 4 <= d.size()
               ? (uint32_t(d[o]) << 24) | (uint32_t(d[o + 1]) << 16) |
                     (uint32_t(d[o + 2]) << 8) | d[o + 3]
               : 0;
}
uint16_t be16(const std::vector<uint8_t>& d, size_t o) {
    return o + 2 <= d.size() ? uint16_t((d[o] << 8) | d[o + 1]) : 0;
}
void put16(std::vector<uint8_t>& d, uint16_t v) {
    d.push_back(uint8_t(v));
    d.push_back(uint8_t(v >> 8));
}
void put32(std::vector<uint8_t>& d, uint32_t v) {
    put16(d, uint16_t(v));
    put16(d, uint16_t(v >> 16));
}
void set32(std::vector<uint8_t>& d, size_t o, uint32_t v) {
    d[o] = uint8_t(v);
    d[o + 1] = uint8_t(v >> 8);
    d[o + 2] = uint8_t(v >> 16);
    d[o + 3] = uint8_t(v >> 24);
}
uint32_t le32(const std::vector<uint8_t>& d, size_t o) {
    return o + 4 <= d.size()
               ? uint32_t(d[o]) | (uint32_t(d[o + 1]) << 8) |
                     (uint32_t(d[o + 2]) << 16) | (uint32_t(d[o + 3]) << 24)
               : 0;
}
double fixedAt(const std::vector<uint8_t>& d, size_t o) {
    return int32_t(be32(d, o)) / 65536.0;
}
int16_t clamp16(double v) {
    const double r = std::floor(v + 0.5);
    return int16_t(r < -32768 ? -32768 : r > 32767 ? 32767 : r);
}

struct V3 {
    double x, y, z;
};

}  // namespace

bool convertSaturnObToTmd(const std::vector<uint8_t>& ob, const SaturnObTransform& xf,
                          uint8_t u, uint8_t v, uint16_t cba, uint16_t tsb,
                          std::vector<uint8_t>* tmd, SaturnObInfo* info,
                          std::string* error) {
    auto fail = [&](const std::string& why) {
        if (error) *error = why;
        return false;
    };
    // Container: three big-endian offsets - mesh, texture, palette.
    const uint32_t meshOff = be32(ob, 0), texOff = be32(ob, 4), palOff = be32(ob, 8);
    if (meshOff != 12 || !(meshOff < texOff && texOff <= palOff && palOff + 32 <= ob.size()))
        return fail("not an OBnn container");
    std::vector<uint8_t> m;
    std::string why;
    if (!saturnLz77Decode(ob.data() + meshOff, texOff - meshOff, &m, nullptr, &why))
        return fail("mesh LZ77: " + why);

    // MBN mesh (offsets mesh-relative): +0 object count, +16 XPDATA offsets.
    if (be32(m, 0) != 1) return fail("expected exactly one object");
    const uint32_t xp = be32(m, 16);
    const uint32_t pnt = be32(m, xp), npt = be32(m, xp + 4);
    const uint32_t pl = be32(m, xp + 8), npl = be32(m, xp + 12);
    if (!npt || !npl || npt > 1024 || npl > 1024 || pnt + 12 * npt > m.size() ||
        pl + 20 * npl > m.size())
        return fail("XPDATA out of range");

    // Bake: SGL draws S * (T_obj * R_obj * p) under the record's own T * R, so
    // in the record's frame a point lands at 4 * S * (pos + RotY(p)) PS1 units
    // (SGL unit = PS1 / 4). The OB models the port converts rotate about Y
    // only; the PS1 and SGL Y rotations share the matrix
    // [c 0 s; 0 1 0; -s 0 c].
    if (xf.ang[0] || xf.ang[2]) return fail("only Y rotations are supported");
    const double a = xf.ang[1] * (2.0 * 3.14159265358979323846 / 65536.0);
    const double c = std::cos(a), s = std::sin(a);
    std::vector<V3> pts(npt);
    for (uint32_t k = 0; k < npt; ++k) {
        const double x = fixedAt(m, pnt + 12 * k), y = fixedAt(m, pnt + 12 * k + 4),
                     z = fixedAt(m, pnt + 12 * k + 8);
        const double rx = c * x + s * z, rz = -s * x + c * z;
        pts[k] = {4.0 * xf.scl[0] * (xf.pos[0] + rx), 4.0 * xf.scl[1] * (xf.pos[1] + y),
                  4.0 * xf.scl[2] * (xf.pos[2] + rz)};
    }

    // Triangles: a POLYGON is 3 FIXED normal + 4 u16 indices, v3 == v2 is a
    // triangle. Each face goes out in both windings with the matching normal.
    struct Tri {
        uint16_t v[3];
        int16_t n[3];
    };
    std::vector<Tri> tris;
    auto addTri = [&](uint16_t i0, uint16_t i1, uint16_t i2) {
        const V3 &p0 = pts[i0], &p1 = pts[i1], &p2 = pts[i2];
        const V3 e1{p1.x - p0.x, p1.y - p0.y, p1.z - p0.z};
        const V3 e2{p2.x - p0.x, p2.y - p0.y, p2.z - p0.z};
        V3 n{e1.y * e2.z - e1.z * e2.y, e1.z * e2.x - e1.x * e2.z, e1.x * e2.y - e1.y * e2.x};
        const double len = std::sqrt(n.x * n.x + n.y * n.y + n.z * n.z);
        if (len < 1e-9) return;   // degenerate
        const int16_t nx = clamp16(n.x / len * 4096), ny = clamp16(n.y / len * 4096),
                      nz = clamp16(n.z / len * 4096);
        tris.push_back({{i0, i1, i2}, {nx, ny, nz}});
        tris.push_back({{i0, i2, i1}, {int16_t(-nx), int16_t(-ny), int16_t(-nz)}});
    };
    for (uint32_t k = 0; k < npl; ++k) {
        uint16_t idx[4];
        for (int j = 0; j < 4; ++j) idx[j] = be16(m, pl + 20 * k + 12 + 2 * j);
        for (int j = 0; j < 4; ++j)
            if (idx[j] >= npt) return fail("polygon index out of range");
        addTri(idx[0], idx[1], idx[2]);
        if (idx[3] != idx[2]) addTri(idx[0], idx[2], idx[3]);
    }
    if (tris.empty()) return fail("no triangles");

    // TMD: header, one object table, primitives, vertices, normals. The object
    // table's offsets are relative to the object table itself (flags 0), as the
    // room TMDs in the PC .RDT files are.
    const uint32_t nPrim = uint32_t(tris.size()), nNorm = nPrim;
    const uint32_t primBytes = nPrim * (4 + 6 * 4);
    const uint32_t primTop = 28, vertTop = primTop + primBytes, normTop = vertTop + npt * 8;
    std::vector<uint8_t>& t = *tmd;
    t.clear();
    put32(t, 0x41);
    put32(t, 0);
    put32(t, 1);
    put32(t, vertTop);
    put32(t, npt);
    put32(t, normTop);
    put32(t, nNorm);
    put32(t, primTop);
    put32(t, nPrim);
    put32(t, 0);   // scale
    for (uint32_t i = 0; i < nPrim; ++i) {
        const Tri& tr = tris[i];
        t.push_back(9);      // olen
        t.push_back(6);      // ilen
        t.push_back(0);      // flag: lit, single-sided
        t.push_back(0x34);   // mode: 3 vertices, textured, Gouraud, opaque
        put16(t, uint16_t(u | (v << 8)));
        put16(t, cba);
        put16(t, uint16_t(u | (v << 8)));
        put16(t, tsb);
        put16(t, uint16_t(u | (v << 8)));
        put16(t, 0);
        for (int j = 0; j < 3; ++j) {
            put16(t, uint16_t(i));   // one normal per face
            put16(t, tr.v[j]);
        }
    }
    for (const V3& p : pts) {
        put16(t, uint16_t(clamp16(p.x)));
        put16(t, uint16_t(clamp16(p.y)));
        put16(t, uint16_t(clamp16(p.z)));
        put16(t, 0);
    }
    for (const Tri& tr : tris) {
        put16(t, uint16_t(tr.n[0]));
        put16(t, uint16_t(tr.n[1]));
        put16(t, uint16_t(tr.n[2]));
        put16(t, 0);
    }

    if (info) {
        info->points = int(npt);
        info->polygons = int(npl);
        info->triangles = int(nPrim);
        // Saturn RGB555 has R in bits 0-4 like the PS1; bit 15 is the Saturn's
        // opaque flag, not STP.
        info->colour = uint16_t(be16(ob, palOff) & 0x7FFF);
    }
    return true;
}

std::vector<uint8_t> makeFlatTim8(uint16_t colour, uint16_t clutX, uint16_t clutY,
                                  uint16_t imgX, uint16_t imgY) {
    std::vector<uint8_t> t;
    put32(t, 0x10);
    put32(t, 9);   // 8 bpp, CLUT present
    put32(t, 12 + 256 * 2);
    put16(t, clutX);
    put16(t, clutY);
    put16(t, 256);
    put16(t, 1);
    for (int i = 0; i < 256; ++i) put16(t, i == 1 ? (colour ? colour : 0x0421) : 0);
    const uint16_t w = 16, h = 16;
    put32(t, 12 + w * h);
    put16(t, imgX);
    put16(t, imgY);
    put16(t, w / 2);   // width in 16-bit VRAM words
    put16(t, h);
    for (int i = 0; i < w * h; ++i) t.push_back(1);
    return t;
}

bool patchRdtOmodel(std::vector<uint8_t>* rdt, int slot, const std::vector<uint8_t>& tmd,
                    const std::vector<uint8_t>& tim, std::string* error) {
    std::vector<uint8_t>& r = *rdt;
    if (r.size() < 0x94) {
        if (error) *error = "RDT too small";
        return false;
    }
    const int count = r[2];
    const uint32_t table = le32(r, 0x50);
    if (slot < 0 || slot >= count || table + 8 * count > r.size()) {
        if (error) *error = "omodel slot out of range";
        return false;
    }
    while (r.size() % 4) r.push_back(0);
    const uint32_t tmdOff = uint32_t(r.size());
    r.insert(r.end(), tmd.begin(), tmd.end());
    while (r.size() % 4) r.push_back(0);
    const uint32_t timOff = uint32_t(r.size());
    r.insert(r.end(), tim.begin(), tim.end());
    set32(r, table + 8 * slot, tmdOff);
    set32(r, table + 8 * slot + 4, timOff);
    return true;
}

}  // namespace re1
