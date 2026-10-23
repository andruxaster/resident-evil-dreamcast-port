#include "core/SaturnMusic.h"

#include "core/Ps1AudioManifest.h"
#include "core/SaturnBgmTable.h"
#include "core/SaturnSound.h"
#include "core/Util.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>

namespace re1 {
namespace {

// The level the PC mix was balanced for: the median RMS (of the mono mix) of
// the PC release's own BGM files, as measured for the PS1 import.
double pcBgmRms() {
    std::vector<double> v;
    for (const Ps1BgmEntry& e : kPs1Bgm)
        if (e.pcRms > 0) v.push_back(e.pcRms);
    std::sort(v.begin(), v.end());
    return v.empty() ? 3000.0 : v[v.size() / 2];
}

// Levels a render to the PC BGM median, never past full scale.
Clip level(const saturn::RenderedSong& r, double target) {
    double sum = 0.0;
    int32_t peak = 1;
    for (size_t i = 0; i + 1 < r.mix.size(); i += 2) {
        const double m = 0.5 * (r.mix[i] + r.mix[i + 1]);
        sum += m * m;
        peak = std::max(peak, std::max(std::abs(r.mix[i]), std::abs(r.mix[i + 1])));
    }
    const double rms = r.mix.empty() ? 0.0 : std::sqrt(sum / (r.mix.size() / 2));
    double gain = rms > 0 ? target / rms : 1.0;
    gain = std::min(gain, 32767.0 / peak);
    Clip c;
    c.pcm.resize(r.mix.size());
    for (size_t i = 0; i < r.mix.size(); ++i)
        c.pcm[i] = (int16_t)std::max(-32768L, std::min(32767L, std::lround(r.mix[i] * gain)));
    c.channels = 2;
    c.rate = r.rate;
    c.loopStart = r.loopStart;
    c.loopEnd = r.loopEnd;
    return c;
}

// A sound effect: every layer of the SE bank's single note summed to mono (the
// DSP effect returns fold in), the silent tail of the fixed render length cut
// off, and one gain for the whole set so the voices keep the relative levels
// the bank's own TL/velocity gave them.
Clip sfxClip(const saturn::RenderedSong& r, double gain) {
    Clip c;
    const size_t frames = r.mix.size() / 2;
    std::vector<double> mono(frames);
    double peak = 0.0;
    for (size_t i = 0; i < frames; ++i) {
        mono[i] = 0.5 * (r.mix[2 * i] + r.mix[2 * i + 1]);
        peak = std::max(peak, std::fabs(mono[i]));
    }
    size_t end = frames;
    while (end > 0 && std::fabs(mono[end - 1]) < peak * 0.01) --end;
    end = std::min(frames, end + (size_t)(r.rate / 100));  // 10 ms of tail
    c.pcm.resize(end);
    for (size_t i = 0; i < end; ++i)
        c.pcm[i] = (int16_t)std::max(-32768L, std::min(32767L, std::lround(mono[i] * gain)));
    c.channels = 1;
    c.rate = r.rate;
    return c;
}

double monoPeak(const saturn::RenderedSong& r) {
    double peak = 0.0;
    for (size_t i = 0; i + 1 < r.mix.size(); i += 2)
        peak = std::max(peak, std::fabs(0.5 * (r.mix[i] + r.mix[i + 1])));
    return peak;
}

bool readSnd(const DiscImage& img, const char* name, std::vector<uint8_t>* out, std::string* error) {
    const DiscEntry* e = img.findInFolder("SND", name);
    if (!e || !img.readFile(*e, out)) {
        if (error) *error = std::string("cannot read SND/") + name + " from the disc";
        return false;
    }
    return true;
}

}  // namespace

bool migrateSaturnMusic(const DiscImage& img, const std::string& overlay,
                        const SaturnMusicOptions& opts, const Progress& progress,
                        std::vector<std::string>* written, std::string* error) {
    std::vector<uint8_t> drvBytes;
    if (!readSnd(img, "SDDRVS.PCK", &drvBytes, error)) return false;
    saturn::SoundDriver drv;
    if (!drv.load(std::move(drvBytes), error)) return false;

    const std::string dir = joinPath(overlay, "Sound");
    if (!makeDirs(dir)) {
        if (error) *error = "cannot create " + dir;
        return false;
    }
    const double target = pcBgmRms();
    const bool ogg = opts.format == AudioFormat::Ogg;
    std::map<std::string, std::vector<uint8_t>> packs;
    std::vector<std::string> files;
    for (const SaturnMusicEntry& e : kSaturnMusic) {
        if (!opts.music) break;
        if (progress.isCancelled()) {
            if (error) *error = "cancelled";
            return false;
        }
        auto it = packs.find(e.pack);
        if (it == packs.end()) {
            std::vector<uint8_t> d;
            if (!readSnd(img, e.pack, &d, error)) return false;
            it = packs.emplace(e.pack, std::move(d)).first;
        }
        saturn::RenderOptions ro;
        saturn::RenderedSong r;
        std::string err;
        if (!saturn::renderSaturnSong(drv, it->second, e.song, ro, &r, &err)) {
            if (error) *error = std::string(e.pack) + " song " + std::to_string(e.song) + ": " + err;
            return false;
        }
        const Clip c = level(r, target);
        const std::string file = std::string(e.name) + (ogg ? ".ogg" : ".WAV");
        const std::string out = joinPath(dir, file);
        const std::string alternate = joinPath(dir, std::string(e.name) +
                                                     (ogg ? ".WAV" : ".ogg"));
        if (!writeAudioClipAtomically(c, opts.format, opts.ffmpegPath,
                                      opts.oggQuality, out, alternate, &err)) {
            if (error) *error = err;
            return false;
        }
        char line[256];
        std::snprintf(line, sizeof(line), "  %s <- %s song %d: %.1f s, %s, %d notes - %s", file.c_str(),
                      e.pack, e.song, c.frames() / (double)c.rate, c.loops() ? "looping" : "one-shot",
                      r.notes, e.what);
        progress.info(line);
        files.push_back("Sound/" + file);
        if (written) written->push_back("Sound/" + file);
    }

    if (opts.tickSfx) {
        // Render the whole set first: one gain, set by the loudest voice, so no
        // effect clips and the voices keep their relative levels.
        std::vector<saturn::RenderedSong> songs;
        double peak = 1.0;
        for (const SaturnSfxEntry& e : kSaturnTickSfx) {
            if (progress.isCancelled()) {
                if (error) *error = "cancelled";
                return false;
            }
            auto it = packs.find(e.pack);
            if (it == packs.end()) {
                std::vector<uint8_t> d;
                if (!readSnd(img, e.pack, &d, error)) return false;
                it = packs.emplace(e.pack, std::move(d)).first;
            }
            saturn::RenderOptions ro;
            saturn::RenderedSong r;
            std::string err;
            if (!saturn::renderSaturnSong(drv, it->second, e.song, ro, &r, &err)) {
                if (error) *error = std::string(e.pack) + " song " + std::to_string(e.song) + ": " + err;
                return false;
            }
            peak = std::max(peak, monoPeak(r));
            songs.push_back(std::move(r));
        }
        const double gain = 0.89 * 32767.0 / peak;
        size_t n = 0;
        for (const SaturnSfxEntry& e : kSaturnTickSfx) {
            const Clip c = sfxClip(songs[n++], gain);
            std::string err;
            const std::string file = std::string(e.name) + (ogg ? ".ogg" : ".WAV");
            const std::string out = joinPath(dir, file);
            const std::string alternate = joinPath(dir, std::string(e.name) +
                                                         (ogg ? ".WAV" : ".ogg"));
            if (!writeAudioClipAtomically(c, opts.format, opts.ffmpegPath,
                                          opts.oggQuality, out, alternate, &err)) {
                if (error) *error = err;
                return false;
            }
            char line[256];
            std::snprintf(line, sizeof(line), "  %s <- %s song %d: %.2f s, mono - Tick %s",
                          file.c_str(), e.pack, e.song, c.frames() / (double)c.rate, e.what);
            progress.info(line);
            files.push_back("Sound/" + file);
            if (written) written->push_back("Sound/" + file);
        }
    }

    if (opts.allBgm) {
        // The campaign's music, one render per PC name. The file is called what
        // g_BgmNameTable calls it, so the game's own loader picks it up from the
        // overlay with no code change; Windows and the Linux port both resolve the
        // name case-insensitively.
        int done = 0, missing = 0;
        for (const SaturnBgmEntry& e : kSaturnBgm) {
            if (!e.first) continue;
            // Already written by the mandatory set (Bgm_24).
            bool mandatory = false;
            for (const SaturnMusicEntry& m : kSaturnMusic)
                if (opts.music && std::strcmp(m.name, e.name) == 0) mandatory = true;
            if (mandatory) continue;
            if (progress.isCancelled()) {
                if (error) *error = "cancelled";
                return false;
            }
            char pack[16];
            std::snprintf(pack, sizeof(pack), "BGM%02X.CDP", e.group);
            auto it = packs.find(pack);
            if (it == packs.end()) {
                std::vector<uint8_t> d;
                std::string ignore;
                if (!readSnd(img, pack, &d, &ignore)) {
                    progress.info(std::string("  ") + e.name + ": " + pack +
                                  " is not on the disc - left to the base tree");
                    ++missing;
                    continue;
                }
                it = packs.emplace(pack, std::move(d)).first;
            }
            saturn::RenderOptions ro;
            saturn::RenderedSong r;
            std::string err;
            if (!saturn::renderSaturnSong(drv, it->second, e.song, ro, &r, &err)) {
                // A pack whose song the renderer cannot make is left to the base
                // tree rather than failing a migration that is otherwise whole.
                progress.info(std::string("  ") + e.name + ": " + pack + " song " +
                              std::to_string(e.song) + ": " + err + " - left to the base tree");
                ++missing;
                continue;
            }
            const Clip c = level(r, target);
            const std::string file = std::string(e.name) + (ogg ? ".ogg" : ".WAV");
            const std::string out = joinPath(dir, file);
            const std::string alternate = joinPath(dir, std::string(e.name) +
                                                         (ogg ? ".WAV" : ".ogg"));
            if (!writeAudioClipAtomically(c, opts.format, opts.ffmpegPath,
                                          opts.oggQuality, out, alternate, &err)) {
                if (error) *error = err;
                return false;
            }
            char line[256];
            std::snprintf(line, sizeof(line), "  %s <- %s song %d: %.1f s, %s, %d notes", file.c_str(),
                          pack, e.song, c.frames() / (double)c.rate,
                          c.loops() ? "looping" : "one-shot", r.notes);
            progress.info(line);
            files.push_back("Sound/" + file);
            if (written) written->push_back("Sound/" + file);
            ++done;
        }
        progress.info("  campaign BGM: " + std::to_string(done) + " rendered, " +
                      std::to_string(missing) + " left to the base tree");
    }

    std::string text =
        "; Written by the RE1 asset migrator: the Sega Saturn's Battle Game music,\n"
        "; rendered from its sequences (SND/*.CDP). Its presence makes the game mix\n"
        "; at 44.1 kHz. See docs/SATURN_VERSION_ANALYSIS.md 6a.\n";
    for (const auto& f : files) text += f + "\n";
    const std::string list = joinPath(dir, "AUDIO.TXT");
    if (!writeFile(list, reinterpret_cast<const uint8_t*>(text.data()), text.size())) {
        if (error) *error = "cannot write " + list;
        return false;
    }
    return true;
}

}  // namespace re1
