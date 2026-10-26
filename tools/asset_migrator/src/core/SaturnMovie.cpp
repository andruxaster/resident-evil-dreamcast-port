#include "core/SaturnMovie.h"

#include "core/Process.h"
#include "core/Util.h"
#include "core/Video.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <random>

namespace fs = std::filesystem;

namespace re1 {
namespace {

constexpr uint32_t kSector = 2048;
constexpr int kMovieCount = 28;
// FILM 1.06: "FILM", header length, version, 4 reserved; FDSC; STAB.
constexpr uint32_t kFilmFdsc = 0x10;
constexpr uint32_t kStabEntry = 16;
// STAB info1 of an audio chunk; a video chunk keeps its sample time there.
constexpr uint32_t kStabAudio = 0xFFFFFFFFu;

// MOVIE.CPK index -> the stem the port's FMV table names (USA, JPN). -1 marks
// the two prologue tails that are joined onto movie 2. Identified by picture
// against the PC movies; the lookalike pairs (DM1/DM2: the gloved and the
// fingerless hand; DMB/DMC; ED6/ED7; the three staff rolls) by frame
// difference against the PC files, and the Japanese disc's order against the
// USA disc's the same way.
struct MovieName {
    const char* usa;
    const char* jpn;
};
const MovieName kNames[kMovieCount] = {
    {"capcom", "capcom"},   //  0 Capcom logo
    {"OU", "OJ"},           //  1 opening / title
    {"PU", "PJ"},           //  2 prologue (+ 3 + 4)
    {nullptr, nullptr},     //  3 prologue: the beat the PS1 drops for Jill
    {nullptr, nullptr},     //  4 prologue: the cast
    {"DM1", "DM1"},         //  5
    {"DM2", "DM2"},         //  6
    {"DM3", "DM3"},         //  7
    {"DM4", "DM4"},         //  8
    {"DM6", "DM6"},         //  9
    {"DM7", "DM7"},         // 10
    {"DMB", "DMB"},         // 11
    {"DMC", "DMC"},         // 12
    {"DMD", "DMD"},         // 13
    {"DME", "DME"},         // 14
    {"DM8", "DM8"},         // 15
    {"DMF", "DMF"},         // 16
    {"ED1", "ED1"},         // 17
    {"ED2", "ED2"},         // 18
    {"ED3", "ED3"},         // 19
    {"EU4", "ED4"},         // 20
    {"EU5", "ED5"},         // 21
    {"ED6", "ED6"},         // 22
    {"ED7", "ED7"},         // 23
    {"ED8", "ED8"},         // 24
    {"stfc_r", "stfc_b"},   // 25 staff roll, Chris
    {"stfj_r", "stfj_b"},   // 26 staff roll, Jill
    {"stfz_r", "stfz_b"},   // 27 staff roll, both
};
constexpr int kPrologue = 2;
constexpr int kPrologueCut = 3;
constexpr int kPrologueCast = 4;

// The frame counts the game's prologue cut is built from (VideoPlayback.cpp
// kSaturnPrologueCut): movie 2 and movie 3, USA then Japan.
constexpr uint32_t kExpectedPrologueFrames[2][2] = {{2430, 150}, {2666, 151}};

uint32_t be32(const uint8_t* p) {
    return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3];
}

// `n` bytes of a file starting at byte `offset` (sector aligned).
bool readSpan(const DiscImage& img, const DiscEntry& file, uint32_t offset,
              uint32_t n, std::vector<uint8_t>* out) {
    out->resize(n);
    uint32_t done = 0;
    uint32_t lba = file.lba + offset / kSector;
    while (done < n) {
        const uint32_t take = std::min(kSector, n - done);
        if (!img.readUserData(lba++, out->data() + done, take)) return false;
        done += take;
    }
    return true;
}

std::string ffprobeBeside(const std::string& ffmpeg) {
    const std::string dir = fs::path(ffmpeg).parent_path().string();
    return dir.empty() ? std::string("ffprobe") : joinPath(dir, "ffprobe");
}

}  // namespace

bool parseSaturnCpk(const DiscImage& img, const DiscEntry& cpk,
                    std::vector<SaturnCpkMovie>* out, std::string* error) {
    out->clear();
    uint32_t off = 0;
    while (off < cpk.size) {
        const std::string where = "MOVIE.CPK movie " + std::to_string(out->size());
        std::vector<uint8_t> head;
        if (cpk.size - off < 0x40 || !readSpan(img, cpk, off, 0x40, &head)) {
            if (error) *error = where + ": cannot read its header";
            return false;
        }
        if (std::memcmp(head.data(), "FILM", 4) != 0 ||
            std::memcmp(head.data() + kFilmFdsc, "FDSC", 4) != 0) {
            if (error) *error = where + ": no FILM/FDSC header at byte " + std::to_string(off);
            return false;
        }
        const uint32_t headerLen = be32(&head[4]);
        const uint32_t stab = kFilmFdsc + be32(&head[kFilmFdsc + 4]);
        if (headerLen < stab + 16 || headerLen > cpk.size - off) {
            if (error) *error = where + ": bad header length";
            return false;
        }
        std::vector<uint8_t> h;
        if (!readSpan(img, cpk, off, headerLen, &h) || std::memcmp(&h[stab], "STAB", 4) != 0) {
            if (error) *error = where + ": no STAB sample table";
            return false;
        }
        SaturnCpkMovie m;
        m.offset = off;
        m.height = be32(&h[kFilmFdsc + 12]);
        m.width = be32(&h[kFilmFdsc + 16]);
        m.timeBase = be32(&h[stab + 8]);
        const uint32_t count = be32(&h[stab + 12]);
        if (std::memcmp(&h[kFilmFdsc + 8], "cvid", 4) != 0 ||
            stab + 16 + (uint64_t)count * kStabEntry > headerLen) {
            if (error) *error = where + ": not a Cinepak FILM sample table";
            return false;
        }
        uint32_t dataEnd = 0;
        for (uint32_t i = 0; i < count; ++i) {
            const uint8_t* e = &h[stab + 16 + i * kStabEntry];
            dataEnd = std::max(dataEnd, be32(e) + be32(e + 4));
            if (be32(e + 8) == kStabAudio) ++m.audioChunks;
            else ++m.videoFrames;
        }
        m.size = headerLen + dataEnd;
        if (m.size > cpk.size - off) {
            if (error) *error = where + ": runs past the end of MOVIE.CPK";
            return false;
        }
        out->push_back(m);
        off += (m.size + kSector - 1) / kSector * kSector;
    }
    if (off != cpk.size) {
        if (error) *error = "MOVIE.CPK: the FILM chain does not end at the file's end";
        return false;
    }
    return true;
}

bool migrateSaturnMovies(const DiscImage& img, SaturnRegion region,
                         const std::string& overlayDir,
                         const std::string& ffmpegPath,
                         const Progress& progress,
                         std::vector<std::string>* written,
                         std::string* error) {
    const DiscEntry* cpk = img.find("MOVIE.CPK");
    if (!cpk) {
        if (error) *error = "/MOVIE.CPK is not on the disc";
        return false;
    }
    std::vector<SaturnCpkMovie> movies;
    if (!parseSaturnCpk(img, *cpk, &movies, error)) return false;
    if ((int)movies.size() != kMovieCount) {
        if (error)
            *error = "MOVIE.CPK holds " + std::to_string(movies.size()) +
                     " movies, not the " + std::to_string(kMovieCount) +
                     " this tool knows the names of";
        return false;
    }
    const bool jpn = region == SaturnRegion::Jpn;
    progress.info("movies: MOVIE.CPK (" + std::to_string(movies.size()) +
                  " Sega FILM / Cinepak movies) -> MP4");

    // The game cuts the joined prologue at fixed frame counts; say so if this
    // disc's differ, because the cut would then land in the wrong place.
    const uint32_t* expect = kExpectedPrologueFrames[jpn ? 1 : 0];
    progress.info("  prologue = movies 2+3+4 (" +
                  std::to_string(movies[kPrologue].videoFrames) + " + " +
                  std::to_string(movies[kPrologueCut].videoFrames) + " + " +
                  std::to_string(movies[kPrologueCast].videoFrames) +
                  " frames); Jill's cut skips movie 3");
    if (movies[kPrologue].videoFrames != expect[0] ||
        movies[kPrologueCut].videoFrames != expect[1])
        progress.info("  warning: the game expects " + std::to_string(expect[0]) +
                      " + " + std::to_string(expect[1]) +
                      " prologue frames; Jill's prologue cut will be off");

    const std::string movieDir = joinPath(overlayDir, "Movie");
    std::error_code ec;
    // Unique per run: two runs sharing the folder delete each other's inputs.
    const fs::path tmp = fs::temp_directory_path(ec) /
                         ("re1am_saturn_movie_" + std::to_string(std::random_device{}()));
    if (!makeDirs(movieDir) || !makeDirs(tmp.string())) {
        if (error) *error = "cannot create " + movieDir;
        return false;
    }
    auto cleanup = [&]() { fs::remove_all(tmp, ec); };
    // One FILM file out of the CPK into a file ffmpeg can open.
    auto extract = [&](int index, std::string* path) {
        std::vector<uint8_t> data;
        const SaturnCpkMovie& m = movies[index];
        char name[16];
        std::snprintf(name, sizeof(name), "m%02d.cpk", index);
        *path = (tmp / name).string();
        if (!readSpan(img, *cpk, m.offset, m.size, &data) || !writeFile(*path, data)) {
            if (error) *error = "cannot extract MOVIE.CPK movie " + std::to_string(index);
            return false;
        }
        return true;
    };
    // Every movie on both discs runs at 15 fps (40 ticks of 600, or 2 of 30).
    auto seconds = [&](int index) {
        return (double)movies[index].videoFrames / 15.0;
    };
    const std::string ffprobe = ffprobeBeside(ffmpegPath);

    int converted = 0;
    for (int i = 0; i < kMovieCount; ++i) {
        const char* stem = jpn ? kNames[i].jpn : kNames[i].usa;
        if (!stem) continue;  // a prologue tail, joined onto movie 2
        if (progress.isCancelled()) {
            if (error) *error = "cancelled";
            cleanup();
            return false;
        }
        const std::string rel = std::string("Movie/") + stem + ".mp4";
        const std::string outPath = joinPath(overlayDir, rel);
        progress.phase(converted, kMovieCount - 2, rel);

        std::vector<std::string> args = {"-hide_banner", "-loglevel", "error", "-y"};
        std::vector<std::string> inputs;
        double expected = 0;  // the source's own length; ED8's audio outruns its frames
        const int parts = i == kPrologue ? 3 : 1;
        for (int p = 0; p < parts; ++p) {
            std::string in;
            if (!extract(i + p, &in)) {
                cleanup();
                return false;
            }
            inputs.push_back(in);
            args.push_back("-i");
            args.push_back(in);
            const double len = probeDuration(ffprobe, in);
            if (len > 0) expected += len;
            // The joins have to land on the frame counts the game cuts at, and
            // concat starts a segment where the longer of its streams ended.
            if (parts > 1 && p < 2 && len > 0 && std::fabs(len - seconds(i + p)) > 0.05)
                progress.info("  warning: prologue movie " + std::to_string(i + p) +
                              " runs " + std::to_string(len) + " s against " +
                              std::to_string(seconds(i + p)) +
                              " s of frames; Jill's prologue cut will be off");
        }
        if (parts > 1) {
            // Each segment's audio ends with its last frame (n / 15 s), so the
            // joins fall exactly on the frame counts the game cuts at.
            args.insert(args.end(),
                        {"-filter_complex",
                         "[0:v][0:a][1:v][1:a][2:v][2:a]concat=n=3:v=1:a=1[v][a]",
                         "-map", "[v]", "-map", "[a]"});
        }
        args.insert(args.end(), {"-c:v", "libx264", "-preset", "slow", "-crf", "18",
                                 "-pix_fmt", "yuv420p", "-c:a", "aac", "-b:a", "192k",
                                 outPath});
        progress.info("  movie " + std::to_string(i) +
                      (parts > 1 ? "+" + std::to_string(i + 1) + "+" + std::to_string(i + 2) : "") +
                      " -> " + rel);
        int code = -1;
        std::string err;
        if (!ChildProcess::run(ffmpegPath, args, false, &code, nullptr, &err, error)) {
            cleanup();
            return false;
        }
        for (const auto& in : inputs) fs::remove(in, ec);
        if (code != 0) {
            if (error) *error = "ffmpeg failed for " + rel + ": " + err;
            cleanup();
            return false;
        }
        // The length has to survive the conversion.
        const double got = probeDuration(ffprobe, outPath);
        if (got >= 0 && expected > 0 && std::fabs(got - expected) > 0.25)
            progress.info("  warning: " + rel + " is " + std::to_string(got) +
                          " s, the disc's movie " + std::to_string(expected) + " s");
        written->push_back(rel);
        ++converted;
    }
    cleanup();
    progress.info("  " + std::to_string(converted) + " movie(s) written; " +
                  (jpn ? "staf_b" : "staf_r") +
                  " has no Saturn version and stays the base tree's");
    return true;
}

}  // namespace re1
