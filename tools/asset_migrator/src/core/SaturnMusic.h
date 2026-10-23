#pragma once
// The Saturn's Battle Game music -> <overlay>/Sound, rendered from the disc's
// sequences by core/SaturnSound (the Sega sound driver + SCSP model).
//
// Only the music the PC has no counterpart for is written. In the Battle Game
// most arenas play BGM group 0x40 = /SND/BATTLE.CDP (GAME5 0x0604CA7E), a
// Saturn-only pack; the others reuse campaign groups the PC already ships
// (0x0F rest rooms, 0x06, 0x27, 0x23, and 0x34's Tyrant theme in arena 15 -
// GAME5's arena table 0x0609ACCC). RANKING.PRG adds /SND/NAME.CDP.

#include "core/AudioWriter.h"
#include "core/DiscImage.h"
#include "core/Types.h"

#include <string>
#include <vector>

namespace re1 {

struct SaturnMusicEntry {
    const char* pack;   // /SND/<pack>
    int song;           // song in its sequence bank = the BGM channel
    const char* name;   // Sound/<name>.wav|.ogg, the name the game asks for
    const char* what;
};

inline constexpr SaturnMusicEntry kSaturnMusic[] = {
    {"BATTLE.CDP", 0, "Bgm_58",
     "Battle Game arenas (group 0x40 channel 0, 25.3 s loop)"},
    {"BATTLE.CDP", 1, "Bgm_59",
     "Battle Game end-of-run fanfare (group 0x40 channel 1, GAME5 0x06088C10)"},
    // The campaign's Tyrant theme under the PC's own name: arena 15 and the lab
    // both play it, so it is part of the mandatory set (allBgm skips it).
    {"BGM34B.CDP", 2, "Bgm_24",
     "Tyrant battle (group 0x34 channel 2, 43.5 s loop; Battle Game arena 15 + lab)"},
    {"NAME.CDP", 0, "Bgm_5a", "Battle Game ranking / name entry (RANKING.PRG)"},
};

// The Tick's exclusive sound effects. The Saturn's room SE banks (/SND/SE<stage>
// <room>.CDP, same CD_PACK and Sega driver as the music) hold one single-note
// "song" per voice. WHICH song an enemy-sound slot plays is not the slot number:
// the bank's first segment (file +0x800, 0x120 bytes, loaded to RAM 0x002E8430
// by LoadRoomSeBank) is a table of 8-byte {key, song id, ...} records, and
// Snd_em(n) looks up key 0x0800 + n (GAME2 0x0605276C -> 0x06065560, group 8).
// Song ids are numbered from 768, so the song in the pack is id - 768.
//
// SE308.CDP (stage 3 room 8, the Tick's room - SATURN_TICK_STAGE) says:
//
//     slot   0   1   2   3   4   5   6   7   8
//     id    777 778 773 776 774 771 772 775 784
//     song    9  10   5   8   6   3   4   7  16
//
// The other Hunter rooms (SE30B, ...) hold the same eight samples in the same
// order shifted to their own song numbers (SE30B: 9 10 5 8 6 3 4 7, slot 8 = 19),
// and SE308 song 16 / SE30B song 19 render to the same 2.09 s sound, so one
// room's songs serve every Tick room. The first version of this table assumed
// "song N = slot N", which played the wrong samples (a door-like thud for the
// footstep). The names keep the PC's He_* meaning of each slot; slot 8 is the
// Tick's own (the arm striking the floor, Snd_em(8) in the stab). Written mono:
// the game pans enemy sounds itself (Snd_em -> pan_set).
struct SaturnSfxEntry {
    const char* pack;   // /SND/<pack>
    int song;           // song in the pack's sequence bank (table id - 768)
    const char* name;   // Sound/<name>.wav|.ogg
    const char* what;
};

inline constexpr SaturnSfxEntry kSaturnTickSfx[] = {
    {"SE308.CDP", 9, "Tk_walkA", "footstep A (slot 0 = id 777, Hunter He_walkA)"},
    {"SE308.CDP", 10, "Tk_walkB", "footstep B (slot 1 = id 778, Hunter He_walkB)"},
    {"SE308.CDP", 5, "Tk_jump", "jump (slot 2 = id 773, Hunter He_jump)"},
    {"SE308.CDP", 8, "Tk_att", "attack (slot 3 = id 776, Hunter He_att)"},
    {"SE308.CDP", 6, "Tk_land", "landing (slot 4 = id 774, Hunter He_land)"},
    {"SE308.CDP", 3, "Tk_smash", "smash (slot 5 = id 771, Hunter He_smash)"},
    {"SE308.CDP", 4, "Tk_dam", "damage (slot 6 = id 772, Hunter He_dam)"},
    {"SE308.CDP", 7, "Tk_Nout", "death (slot 7 = id 775, Hunter He_Nout)"},
    {"SE308.CDP", 16, "Tk_stuck", "arm strikes the floor (slot 8 = id 784, the stab)"},
};

struct SaturnMusicOptions {
    bool music = true;      // kSaturnMusic
    bool tickSfx = true;    // kSaturnTickSfx
    // Every campaign BGM the PC names (core/SaturnBgmTable.h), rendered from the
    // Saturn's own BGM<group>.CDP under the PC's file name, so Saturn mode plays
    // the Saturn's arrangement in every room. Optional: ~50 renders.
    bool allBgm = false;
    AudioFormat format = AudioFormat::Wav;
    int oggQuality = 6;
    std::string ffmpegPath = "ffmpeg";
};

// Renders kSaturnMusic (and kSaturnTickSfx) into <overlay>/Sound and writes Sound/AUDIO.TXT
// there, whose presence makes the game mix at 44.1 kHz (the Saturn renders
// are 44.1 kHz; the PC's 22.05 kHz mix would halve them). `written` receives
// overlay-relative paths.
bool migrateSaturnMusic(const DiscImage& img, const std::string& overlay,
                        const SaturnMusicOptions& opts, const Progress& progress,
                        std::vector<std::string>* written, std::string* error);

}  // namespace re1
