#pragma once
// The sound files the migrator writes for the game: 16-bit PCM WAV or Ogg
// Vorbis, with the loop region and the "written by the migrator" marker the
// game's loader looks for (src/system/AudioFile.h: a loop is honoured only in
// marked files). Shared by the PS1 audio import and the Saturn music.

#include <cstdint>
#include <string>
#include <vector>

namespace re1 {

enum class AudioFormat { Wav, Ogg };

// A decoded sound, interleaved 16-bit. loopStart/loopEnd are frames
// ([start, end)), -1 when the sound does not loop.
struct Clip {
    std::vector<int16_t> pcm;
    int channels = 1;
    int rate = 44100;
    long loopStart = -1;
    long loopEnd = -1;
    size_t frames() const { return pcm.size() / (size_t)channels; }
    bool loops() const { return loopStart >= 0 && loopEnd > loopStart; }
};

// 16-bit PCM WAV with the marker chunk "re1p" and, for a looping clip, a
// `smpl` chunk.
std::vector<uint8_t> buildWav(const Clip& c);

// Write a WAV through a temporary sibling and replace the destination only
// after the complete file has been written.
bool writeWavAtomically(const std::string& out, const Clip& c,
                        std::string* error);

// Write a WAV through a temporary sibling and replace the destination only
// after the complete file has been written.
bool writeWavAtomically(const std::string& out, const Clip& c,
                        std::string* error);

// Ogg Vorbis through ffmpeg (raw PCM on stdin), the loop and the marker as
// Vorbis comments: LOOPSTART / LOOPLENGTH (frames) and RE1PS1=1. Returns an
// empty string on success, else the error.
std::string encodeOgg(const std::string& ffmpeg, int quality, const Clip& c,
                      const std::string& out);

// Convert to the requested format, then retire the other-format sibling only
// after the new file is safely installed. Failed conversion preserves old files.
bool writeAudioClipAtomically(const Clip& c, AudioFormat format,
                              const std::string& ffmpeg, int quality,
                              const std::string& out,
                              const std::string& alternate,
                              std::string* error);

// Converts to the requested format, then removes the other-format sibling only
// after the new file is safely installed. A failed conversion leaves the old
// file(s) intact.
bool writeAudioClipAtomically(const Clip& c, AudioFormat format,
                              const std::string& ffmpeg, int quality,
                              const std::string& out,
                              const std::string& alternate,
                              std::string* error);

}  // namespace re1
