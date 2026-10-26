#pragma once
// The HD fonts: real TrueType fonts the game draws its text in with HD
// textures on (src/game/HdText.h; docs/HD_TEXTURES.md "HD fonts"). Each is
// named after the base tree's font file it stands in for:
//
//   HD/<USA|JPN>/Data/TITLEFNT.TTF   Archivo Black: the title menu (beside
//                                    Data/TITLEFNT.BIN, core/TitleFont.h) and
//                                    the title sheets' PRESS START, NEW GAME,
//                                    STANDARD / TRAINING / ADVANCED
//   HD/USA/Data/FONTUS.TTF           Archivo Narrow SemiBold: the 8x8 font of
//   HD/JPN/Data/FONT.TTF             Data/fontus.tim (FONT.TIM on the Japanese
//                                    tree) and the title's copyright lines
//   HD/USA/Data/FONTUS_14.TTF        Libertinus Serif SemiBold: the 8x14 game
//                                    text of the same sheet (messages, items)
//   HD/JPN/Data/FONT_14.TTF          M PLUS 2 SemiBold: the same for the
//                                    Japanese sheet
//
// All are under the SIL Open Font License 1.1 (fonts/, embedded as
// core/HdFont*.inc by tools/gen_hd_fonts.py). The Archivo fonts are by
// Omnibus-Type, unmodified; the two game-text fonts are subsets the script
// makes (Libertinus Serif to Latin-1, M PLUS 2 instanced at SemiBold and
// cut to the Japanese sheet's characters). Each file holds its licence in its
// name table, which the OFL accepts in place of a separate OFL.txt. Any
// TrueType font can take any of the places.
//
// The HD Pack tab writes them with every original-game import, the PC tab only
// when <target>/HD already exists: creating HD/ would switch HD textures on.
// Writing them removes the TITLEFNT.VEC outlines an earlier version put there.

#include "core/Types.h"

#include <cstdint>
#include <string>
#include <vector>

namespace re1 {

// The TITLEFNT.TTF image (Archivo Black).
std::vector<uint8_t> buildHdTitleFont();

// The FONTUS.TTF / FONT.TTF image (Archivo Narrow SemiBold).
std::vector<uint8_t> buildHdTextFont();

// True when `data` is a TrueType outline font whose table directory fits the
// file and has the tables the game's rasteriser needs. `tables` (optional)
// receives the table count.
bool checkHdFont(const std::vector<uint8_t>& data, int* tables);

// The 8x14 font's image for `tree` (Libertinus Serif, or M PLUS 2).
std::vector<uint8_t> buildHdText14Font(AssetVersion tree);

// Its HD file name: "FONTUS_14.TTF" or "FONT_14.TTF".
const char* hdText14FontName(AssetVersion tree);

// The 8x8 font's HD file name in `tree`: "FONTUS.TTF" or "FONT.TTF".
const char* hdTextFontName(AssetVersion tree);

// Writes the tree's three fonts into <targetRoot>/HD/<tree>/Data.
bool writeHdFonts(const std::string& targetRoot, AssetVersion tree,
                  const Progress& progress, std::string* error);

}  // namespace re1
