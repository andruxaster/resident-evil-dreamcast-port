#pragma once
// The Saturn's FMV: MOVIE.CPK -> <overlay>/Movie/<PC name>.mp4.
//
// MOVIE.CPK is not one movie. It is 28 Sega FILM 1.06 files (Cinepak 320x224
// at 15 fps, PCM stereo) laid end to end, each starting on a 2048-byte sector,
// played by MOVIE.PRG. A FILM header says how long its own file is (header
// length + the end of the furthest STAB sample), so the chain is walked from
// byte 0 and has to cover the whole file - which it does, on both the USA and
// the Japanese disc, with the same order on both.
//
// Each movie is written under the name the port's FMV table gives it
// (src/video/VideoPlayback.cpp, the USA or the JPN table by region), so the
// game picks it up from the SATURN overlay before the base tree's .avi.
// Two things do not map one to one:
//   * the prologue is THREE files on the Saturn (movies 2, 3, 4). Movie 3 is
//     the beat the PS1 drops for Jill (SLUS_001.70 PlayFMV cuts frames
//     2430..2591, and movie 2 is exactly 2430 frames), so the Saturn plays it
//     as its own file. They are joined into one PU/PJ.mp4 and the game cuts
//     the joined file at the Saturn's boundaries (kSaturnPrologueCut in
//     VideoPlayback.cpp, from the frame counts this module logs);
//   * the Saturn has three staff rolls (Chris, Jill, both), with the credits
//     in the picture like the PC's stfc_r/stfj_r/stfz_r. It has nothing for the
//     PC's fourth (staf_r), which stays the base tree's.
// There is no subtitle or ending-credits data to carry over in the PS1 form
// (DATA/JIMAKU*.RGB, DATA/STAFF*.STF): the Japanese disc's prologue has its
// subtitles drawn into the video, and the staff rolls carry their credits.

#include "core/DiscImage.h"
#include "core/SaturnRegion.h"
#include "core/Types.h"

#include <cstdint>
#include <string>
#include <vector>

namespace re1 {

// One FILM file inside MOVIE.CPK.
struct SaturnCpkMovie {
    uint32_t offset = 0;       // byte offset in MOVIE.CPK (sector aligned)
    uint32_t size = 0;         // header + sample data, unpadded
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t timeBase = 0;     // STAB ticks per second
    uint32_t videoFrames = 0;  // STAB entries that are not audio
    uint32_t audioChunks = 0;
};

// Walk MOVIE.CPK's FILM chain. False when the file is not a chain of FILM
// files that ends exactly at its end.
bool parseSaturnCpk(const DiscImage& img, const DiscEntry& cpk,
                    std::vector<SaturnCpkMovie>* out, std::string* error);

// Convert every movie into <overlayDir>/Movie. `written` receives the overlay
// paths (Movie/<name>.mp4).
bool migrateSaturnMovies(const DiscImage& img, SaturnRegion region,
                         const std::string& overlayDir,
                         const std::string& ffmpegPath,
                         const Progress& progress,
                         std::vector<std::string>* written,
                         std::string* error);

}  // namespace re1
