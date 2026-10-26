// Which Sega Saturn release a disc is.
//
// "Resident Evil" (USA, volume RESIDENT_EVIL, raw MODE1/2352) and "Bio Hazard"
// (Japan, volume BIO_HAZARD, often a cooked MODE1/2048 .iso) name their files
// the same way; what differs is content. The Japanese disc:
//   * has Japanese room text, in the very encoding the PC Biohazard.exe uses
//     (data\FONT.TIM), so its rooms only read right with [Assets] Version=JPN;
//   * has no WAPON3.BIN / SAVE2.BIN: RANKING.PRG uses the Japanese item labels
//     and yes/no prompts BATTLE.BIN carries itself (+0x35000, +0x31EB0), which
//     the USA module overwrote with those two files;
//   * draws the "BIO HAZARD" 3D logo behind the results screens - a model both
//     discs' RANKING.PRG hold, and only the Japanese one draws;
//   * keeps the first-playthrough (non-western) weapon table in all five GAME*.PRG.
// The overlay therefore goes to its own folder, SATURN_JPN, which the game reads
// in place of SATURN when the JPN tree is selected (src/system/AssetPath.cpp).
#pragma once

namespace re1 {

enum class SaturnRegion { Usa, Jpn };

inline const char* saturnRegionName(SaturnRegion r) {
    return r == SaturnRegion::Jpn ? "Japan (Bio Hazard)" : "USA (Resident Evil)";
}

// The overlay folder a disc of this region is written to.
inline const char* saturnOverlayFolder(SaturnRegion r) {
    return r == SaturnRegion::Jpn ? "SATURN_JPN" : "SATURN";
}

}  // namespace re1
