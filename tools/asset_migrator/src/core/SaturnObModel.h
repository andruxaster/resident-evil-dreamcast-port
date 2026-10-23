#pragma once
// Sega Saturn room-object models (ENEMY/OBnn.DAT) -> PS1/PC TMD.
//
// The Saturn does not draw the omodel TMDs in a room file. It loads one
// ENEMY/OBnn.DAT per omodel slot from a per-room table (GAME4 0x060980F4) and
// gives each model a hardcoded transform in code (GAME4 0x060681E8: position
// switch -> 0x060691C4, scale switch -> 0x060691DC, rotation switch ->
// 0x060691F4). An OBnn.DAT is an MBN (the A_*.DAT prop mesh layout) packed as
// three big-endian offsets - mesh, texture, palette - with the mesh and the
// texture each an LZ77 stream (the module codec, SaturnLz77.h).
//
// The PC room-object path draws PS1 TMDs through the room's object_models
// table, so a model the port needs is converted here: the Saturn transform is
// baked into the vertices (the port's record supplies the room translation and
// rotation, as the Saturn's does), every polygon becomes mode-0x34 triangles
// (lit, Gouraud, textured, opaque - the commonest room-object primitive in the
// PC rooms) and, because SGL's Dual_Plane attribute draws both sides while the
// PC path back-face culls, each triangle is emitted in both windings.
// See docs/SATURN_PORT.md section 4.4.

#include <cstdint>
#include <string>
#include <vector>

namespace re1 {

// The per-model transform the Saturn applies in code, in SGL units (FIXED as
// double, angles in SGL 65536-per-turn units). SGL unit = PS1 unit / 4.
struct SaturnObTransform {
    double pos[3];
    uint16_t ang[3];
    double scl[3];
};

struct SaturnObInfo {
    int points = 0;
    int polygons = 0;
    int triangles = 0;     // written, both windings counted
    uint16_t colour = 0;   // PS1 RGB555 of palette entry 0
};

// OBnn.DAT -> a one-object PS1 TMD (id 0x41, flags 0). Every vertex samples
// texel (u, v) of a texture page addressed by `cba` / `tsb`.
bool convertSaturnObToTmd(const std::vector<uint8_t>& ob, const SaturnObTransform& xf,
                          uint8_t u, uint8_t v, uint16_t cba, uint16_t tsb,
                          std::vector<uint8_t>* tmd, SaturnObInfo* info,
                          std::string* error);

// A 16x16 8 bpp TIM whose every texel is CLUT index 1 = `colour` (index 0 is
// the PC engine's transparency key and stays 0x0000). CLUT at VRAM
// (clutX, clutY), image at (imgX, imgY).
std::vector<uint8_t> makeFlatTim8(uint16_t colour, uint16_t clutX, uint16_t clutY,
                                  uint16_t imgX, uint16_t imgY);

// Append a TMD and a TIM to a PC-layout .RDT and point object_models[slot] at
// them. The room loader resolves those two offsets against the RDT base, so
// data past the original end is reachable as it is.
bool patchRdtOmodel(std::vector<uint8_t>* rdt, int slot, const std::vector<uint8_t>& tmd,
                    const std::vector<uint8_t>& tim, std::string* error);

}  // namespace re1
