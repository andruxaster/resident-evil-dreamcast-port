#include "core/AudioWriter.h"

#include "core/Process.h"
#include "core/Util.h"

#include <atomic>
#include <cstring>
#include <filesystem>

namespace re1 {
namespace {

namespace fs = std::filesystem;
std::atomic<unsigned long> g_tempCounter{0};

std::string uniqueSibling(const std::string& out, const char* role,
                          const char* extension) {
    const fs::path target(out);
    const fs::path parent = target.parent_path();
    const std::string stem = target.stem().string();
    for (int attempt = 0; attempt < 1000; ++attempt) {
        const unsigned long n = g_tempCounter.fetch_add(1);
        const fs::path candidate = parent /
            (stem + ".re1am-" + role + "-" + std::to_string(n) + extension);
        std::error_code ec;
        if (!fs::exists(candidate, ec) && !ec) return candidate.string();
    }
    return std::string();
}

void removeIfExists(const std::string& path) {
    std::error_code ec;
    fs::remove(path, ec);
}

bool replaceFileAtomically(const std::string& temp, const std::string& out,
                           std::string* error) {
    std::error_code ec;
    if (!fs::exists(temp, ec) || ec) {
        if (error) *error = "temporary audio output is missing: " + temp;
        return false;
    }
    const uintmax_t size = fs::file_size(temp, ec);
    if (ec || size == 0) {
        if (error) *error = "temporary audio output is empty or unreadable: " + temp;
        return false;
    }

    // POSIX rename replaces atomically. On Windows the destination must first
    // move aside; keep it until the new file is in place and restore on failure.
    ec.clear();
    fs::rename(temp, out, ec);
    if (!ec) return true;

    ec.clear();
    if (!fs::exists(out, ec) || ec) {
        if (error) *error = "cannot install audio output " + out + ": " + ec.message();
        return false;
    }
    const std::string backup = uniqueSibling(out, "backup", ".bak");
    if (backup.empty()) {
        if (error) *error = "cannot choose a backup path for " + out;
        return false;
    }
    ec.clear();
    fs::rename(out, backup, ec);
    if (ec) {
        if (error) *error = "cannot preserve existing audio file " + out + ": " + ec.message();
        return false;
    }
    ec.clear();
    fs::rename(temp, out, ec);
    if (ec) {
        const std::string installError = ec.message();
        std::error_code restoreError;
        fs::rename(backup, out, restoreError);
        if (error) {
            *error = "cannot install audio output " + out + ": " + installError;
            if (restoreError) *error += "; could not restore previous file: " + restoreError.message();
        }
        return false;
    }
    removeIfExists(backup);
    return true;
}

void put16(std::vector<uint8_t>& b, uint32_t v) {
    b.push_back((uint8_t)(v & 0xFF));
    b.push_back((uint8_t)((v >> 8) & 0xFF));
}
void put32(std::vector<uint8_t>& b, uint32_t v) {
    put16(b, v & 0xFFFF);
    put16(b, v >> 16);
}
void putTag(std::vector<uint8_t>& b, const char* t) { b.insert(b.end(), t, t + 4); }

}  // namespace

// 16-bit PCM WAV with the migrator's marker chunk "re1p" and, for a looping
// clip, a `smpl` chunk. The game honours the loop only in marked files (the
// PC tree's own BGM_33.WAV has a smpl chunk the original ignored); see
// src/system/AudioFile.h.
std::vector<uint8_t> buildWav(const Clip& c) {
    const uint32_t dataBytes = (uint32_t)(c.pcm.size() * 2);
    std::vector<uint8_t> b;
    b.reserve(56 + dataBytes + 68);
    putTag(b, "RIFF");
    put32(b, 0);  // patched below
    putTag(b, "WAVE");
    putTag(b, "fmt ");
    put32(b, 16);
    put16(b, 1);
    put16(b, (uint32_t)c.channels);
    put32(b, (uint32_t)c.rate);
    put32(b, (uint32_t)(c.rate * c.channels * 2));
    put16(b, (uint32_t)(c.channels * 2));
    put16(b, 16);
    putTag(b, "re1p");
    put32(b, 4);
    put32(b, 1);
    putTag(b, "data");
    put32(b, dataBytes);
    const uint8_t* p = reinterpret_cast<const uint8_t*>(c.pcm.data());
    b.insert(b.end(), p, p + dataBytes);
    if (c.loops()) {
        putTag(b, "smpl");
        put32(b, 36 + 24);
        put32(b, 0);                                  // manufacturer
        put32(b, 0);                                  // product
        put32(b, (uint32_t)(1000000000.0 / c.rate));  // sample period, ns
        put32(b, 60);                                 // MIDI unity note
        put32(b, 0);                                  // pitch fraction
        put32(b, 0);                                  // SMPTE format
        put32(b, 0);                                  // SMPTE offset
        put32(b, 1);                                  // loops
        put32(b, 0);                                  // sampler data
        put32(b, 0);                                  // cue point id
        put32(b, 0);                                  // forward loop
        put32(b, (uint32_t)c.loopStart);
        put32(b, (uint32_t)(c.loopEnd - 1));          // inclusive
        put32(b, 0);                                  // fraction
        put32(b, 0);                                  // play count: forever
    }
    const uint32_t riff = (uint32_t)(b.size() - 8);
    std::memcpy(b.data() + 4, &riff, 4);
    return b;
}

bool writeWavAtomically(const std::string& out, const Clip& c,
                        std::string* error) {
    if (error) error->clear();
    const std::string temp = uniqueSibling(out, "wav", ".wav");
    if (temp.empty()) {
        if (error) *error = "cannot choose a temporary WAV path for " + out;
        return false;
    }
    const std::vector<uint8_t> wav = buildWav(c);
    if (wav.empty() || !writeFile(temp, wav)) {
        removeIfExists(temp);
        if (error) *error = "cannot write temporary WAV for " + out;
        return false;
    }
    if (!replaceFileAtomically(temp, out, error)) {
        removeIfExists(temp);
        return false;
    }
    return true;
}

// Ogg Vorbis through ffmpeg (raw PCM on stdin). The loop and the marker go in
// as Vorbis comments: LOOPSTART / LOOPLENGTH (frames, the usual convention)
// and RE1PS1=1.
std::string encodeOgg(const std::string& ffmpeg, int quality, const Clip& c,
                      const std::string& out) {
    const std::string temp = uniqueSibling(out, "ogg", ".ogg");
    if (temp.empty()) return "cannot choose a temporary Ogg path for " + out;
    std::vector<std::string> args = {
        "-hide_banner", "-loglevel", "error", "-y", "-f", "s16le",
        "-ar", std::to_string(c.rate), "-ac", std::to_string(c.channels), "-i", "-",
        "-c:a", "libvorbis", "-q:a", std::to_string(quality),
        "-metadata", "RE1PS1=1"};
    if (c.loops()) {
        args.insert(args.end(), {"-metadata", "LOOPSTART=" + std::to_string(c.loopStart),
                                 "-metadata",
                                 "LOOPLENGTH=" + std::to_string(c.loopEnd - c.loopStart)});
    }
    args.push_back(temp);
    ChildProcess proc;
    std::string err;
    if (!proc.start(ffmpeg, args, false, &err)) {
        removeIfExists(temp);
        return "cannot start ffmpeg: " + err;
    }
    const bool wroteAll = proc.writeStdin(c.pcm.data(), c.pcm.size() * 2);
    proc.closeStdin();
    int code = -1;
    if (!proc.wait(&code)) {
        removeIfExists(temp);
        return "cannot wait for ffmpeg while writing " + out;
    }
    if (!wroteAll) {
        const std::string msg = proc.stderrText();
        removeIfExists(temp);
        return "cannot stream PCM to ffmpeg for " + out +
               (msg.empty() ? std::string() : ": " + msg);
    }
    if (code != 0) {
        std::string msg = proc.stderrText();
        if (msg.find("libvorbis") != std::string::npos || msg.find("Unknown encoder") != std::string::npos)
            msg += " (the ffmpeg build needs the libvorbis encoder)";
        removeIfExists(temp);
        return "ffmpeg failed for " + out + ": " + msg;
    }
    if (!replaceFileAtomically(temp, out, &err)) {
        removeIfExists(temp);
        return err;
    }
    return std::string();
}

bool writeAudioClipAtomically(const Clip& c, AudioFormat format,
                              const std::string& ffmpeg, int quality,
                              const std::string& out,
                              const std::string& alternate,
                              std::string* error) {
    if (error) error->clear();
    if (format == AudioFormat::Wav) {
        if (!writeWavAtomically(out, c, error)) return false;
    } else {
        const std::string err = encodeOgg(ffmpeg, quality, c, out);
        if (!err.empty()) {
            if (error) *error = err;
            return false;
        }
    }

    if (alternate.empty() || alternate == out) return true;
    std::error_code ec;
    fs::remove(alternate, ec);
    if (ec) {
        if (error) *error = "new audio was written, but cannot remove old format " +
                            alternate + ": " + ec.message();
        return false;
    }
    return true;
}

}  // namespace re1
