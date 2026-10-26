#include "core/BackgroundWebp.h"

#include "core/Pak.h"
#include "core/Process.h"
#include "core/Util.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <system_error>

// The decoder the game itself uses (src/system/WebpImage.cpp), so "this tool
// could read the file back" means "the game can".
#define SIMPLEWEBP_IMPLEMENTATION
#define SIMPLEWEBP_DISABLE_STDIO
#include "simplewebp.h"

namespace fs = std::filesystem;

namespace re1 {

namespace {

constexpr size_t kPixels = (size_t)kBackgroundWidth * kBackgroundHeight;
// Frames per ffmpeg run. Small enough that a cancel is honoured quickly and
// that an interrupted run has installed most of what it encoded; large enough
// that starting the process is noise.
constexpr size_t kBatch = 64;

struct Item {
    std::string dir;   // the Stage folder
    std::string pak;   // file name inside it, as spelled on disk
};

std::string stemOf(const std::string& name) {
    const size_t dot = name.rfind('.');
    return dot == std::string::npos ? name : name.substr(0, dot);
}

// The entry of `dir` whose name is `name`, compared case-insensitively.
std::string findCI(const std::string& dir, const std::string& name) {
    const std::string low = toLower(name);
    for (const auto& e : listDirectory(dir)) {
        if (toLower(e) == low && isRegularFile(joinPath(dir, e))) return e;
    }
    return std::string();
}

std::vector<Item> collectPaks(const std::string& tree) {
    std::vector<Item> items;
    for (const auto& d : listDirectory(tree)) {
        const std::string dir = joinPath(tree, d);
        if (toLower(d).rfind("stage", 0) != 0 || !isDirectory(dir)) continue;
        for (const auto& f : listDirectory(dir)) {
            const std::string low = toLower(f);
            if (low.rfind("rc", 0) == 0 && extensionOf(low) == ".pak" &&
                isRegularFile(joinPath(dir, f)))
                items.push_back({dir, f});
        }
    }
    std::sort(items.begin(), items.end(), [](const Item& a, const Item& b) {
        return a.dir != b.dir ? a.dir < b.dir : a.pak < b.pak;
    });
    return items;
}

std::vector<std::string> encoderArgs(const BackgroundFormatOptions& o,
                                     const std::string& pattern) {
    char size[32];
    std::snprintf(size, sizeof(size), "%dx%d", kBackgroundWidth, kBackgroundHeight);
    std::vector<std::string> args = {"-hide_banner", "-loglevel", "error", "-y",
                                     "-f",           "rawvideo",  "-pix_fmt",
                                     "rgb24",        "-s",        size,
                                     "-i",           "-",         "-c:v",
                                     "libwebp"};
    if (o.format == BackgroundFormat::WebpLossless) {
        // Method 4: level 6 is 1 % smaller for 70 % more time, and "-quality
        // 100" (the lossless effort) another 2 % for 25 times the time.
        args.insert(args.end(), {"-lossless", "1", "-compression_level", "4"});
    } else {
        const int q = std::min(100, std::max(0, o.quality));
        args.insert(args.end(), {"-lossless", "0", "-quality", std::to_string(q),
                                 "-compression_level", "6"});
    }
    // image2: one still per frame. Without it the .webp extension selects the
    // animated-WebP muxer and everything lands in a single file.
    args.insert(args.end(), {"-f", "image2", "-start_number", "0", pattern});
    return args;
}

}  // namespace

std::string findBackgroundFile(const std::string& dir, const std::string& pakName) {
    const std::string pak = findCI(dir, pakName);
    if (!pak.empty()) return pak;
    return findCI(dir, stemOf(pakName) + ".webp");
}

bool backgroundPakToRgb(const std::vector<uint8_t>& pak,
                        std::vector<uint8_t>* rgb, std::string* why) {
    std::vector<uint8_t> tim;
    std::string err;
    if (!lzwUnpack(pak.data(), pak.size(), &tim, &err)) {
        if (why) *why = "LZW: " + err;
        return false;
    }
    // makeBackgroundTim's layout, which is Capcom's: magic, 16 bpp without a
    // CLUT, then one image block of 320x240 words. Only the VRAM position
    // differs between files and it is not used.
    const size_t need = 20 + kPixels * 2;
    if (tim.size() < need || readU32(tim.data(), tim.size(), 0) != 0x10 ||
        (readU32(tim.data(), tim.size(), 4) & 0xF) != 2 ||
        readU16(tim.data(), tim.size(), 16) != kBackgroundWidth ||
        readU16(tim.data(), tim.size(), 18) != kBackgroundHeight) {
        if (why) *why = "not a 320x240 16 bpp TIM";
        return false;
    }
    rgb->resize(kPixels * 3);
    for (size_t i = 0; i < kPixels; ++i) {
        const uint16_t px = readU16(tim.data(), tim.size(), 20 + i * 2);
        (*rgb)[i * 3 + 0] = (uint8_t)((px & 0x1F) * 255 / 31);
        (*rgb)[i * 3 + 1] = (uint8_t)(((px >> 5) & 0x1F) * 255 / 31);
        (*rgb)[i * 3 + 2] = (uint8_t)(((px >> 10) & 0x1F) * 255 / 31);
    }
    return true;
}

bool decodeWebp(const std::vector<uint8_t>& file, std::vector<uint8_t>* rgba,
                int* width, int* height) {
    if (file.size() < 12 || std::memcmp(file.data(), "RIFF", 4) != 0 ||
        std::memcmp(file.data() + 8, "WEBP", 4) != 0)
        return false;
    simplewebp* webp = nullptr;
    // The memory reader does not write to the buffer.
    if (simplewebp_load_from_memory(const_cast<uint8_t*>(file.data()), file.size(),
                                    nullptr, &webp) != SIMPLEWEBP_NO_ERROR)
        return false;
    size_t w = 0, h = 0;
    simplewebp_get_dimensions(webp, &w, &h);
    bool ok = w > 0 && h > 0 && w <= 16384 && h <= 16384;
    if (ok) {
        rgba->resize(w * h * 4);
        ok = simplewebp_decode(webp, rgba->data(), nullptr) == SIMPLEWEBP_NO_ERROR;
    }
    simplewebp_unload(webp);
    if (!ok) return false;
    if (width) *width = (int)w;
    if (height) *height = (int)h;
    return true;
}

bool applyBackgroundFormat(const std::string& tree,
                           const BackgroundFormatOptions& o,
                           const Progress& progress, std::string* error) {
    const std::vector<Item> items = collectPaks(tree);
    std::error_code ec;

    if (o.format == BackgroundFormat::Pak) {
        size_t removed = 0;
        for (const Item& it : items) {
            const std::string webp = findCI(it.dir, stemOf(it.pak) + ".webp");
            if (!webp.empty() && fs::remove(joinPath(it.dir, webp), ec)) ++removed;
        }
        if (removed)
            progress.info("backgrounds: removed " + std::to_string(removed) +
                          " .webp from an earlier run (the .pak beside each "
                          "would have been shadowed)");
        return true;
    }

    const bool lossless = o.format == BackgroundFormat::WebpLossless;
    progress.info(std::string("backgrounds: ") + tree + "/Stage*/RC*.pak -> .webp (" +
                  (lossless ? std::string("lossless")
                            : "lossy, quality " + std::to_string(o.quality)) +
                  ")");
    if (items.empty()) {
        progress.info("  no .pak backgrounds to convert");
        return true;
    }

    const std::string tmp =
        joinPath(fs::temp_directory_path(ec).string(), "re1am_webp");
    fs::remove_all(tmp, ec);
    if (!makeDirs(tmp)) {
        if (error) *error = "cannot create " + tmp;
        return false;
    }
    auto cleanup = [&]() { fs::remove_all(tmp, ec); };

    size_t converted = 0, skipped = 0;
    uint64_t bytesBefore = 0, bytesAfter = 0;

    for (size_t first = 0; first < items.size(); first += kBatch) {
        if (progress.isCancelled()) {
            cleanup();
            if (error) *error = "cancelled";
            return false;
        }
        const size_t last = std::min(items.size(), first + kBatch);
        progress.phase((int)first, (int)items.size(), "backgrounds -> WebP");

        // Decode the batch. `batch` lists the items that turned out to be
        // backgrounds, in the order their frames go to the encoder.
        std::vector<size_t> batch;
        std::vector<std::vector<uint8_t>> frames;
        std::vector<size_t> pakSizes;
        for (size_t i = first; i < last; ++i) {
            std::vector<uint8_t> pak, rgb;
            std::string why;
            const std::string path = joinPath(items[i].dir, items[i].pak);
            if (!readFile(path, &pak)) {
                cleanup();
                if (error) *error = "cannot read " + path;
                return false;
            }
            if (!backgroundPakToRgb(pak, &rgb, &why)) {
                progress.info("  " + items[i].pak + ": " + why + " - left as .pak");
                ++skipped;
                continue;
            }
            batch.push_back(i);
            pakSizes.push_back(pak.size());
            frames.push_back(std::move(rgb));
        }
        if (batch.empty()) continue;

        ChildProcess proc;
        std::string startErr;
        if (!proc.start(o.ffmpegPath, encoderArgs(o, joinPath(tmp, "%06d.webp")),
                        false, &startErr)) {
            cleanup();
            if (error)
                *error = "WebP backgrounds need ffmpeg (" + startErr + ")";
            return false;
        }
        for (const auto& f : frames) {
            if (!proc.writeStdin(f.data(), f.size())) break;
        }
        proc.closeStdin();
        int code = -1;
        proc.wait(&code);
        if (code != 0) {
            cleanup();
            if (error)
                *error = "ffmpeg could not encode WebP (it needs libwebp): " +
                         proc.stderrText();
            return false;
        }

        for (size_t k = 0; k < batch.size(); ++k) {
            const Item& it = items[batch[k]];
            char num[32];
            std::snprintf(num, sizeof(num), "%06d.webp", (int)k);
            const std::string encoded = joinPath(tmp, num);
            std::vector<uint8_t> file, rgba;
            int w = 0, h = 0;
            if (!readFile(encoded, &file) || !decodeWebp(file, &rgba, &w, &h) ||
                w != kBackgroundWidth || h != kBackgroundHeight) {
                cleanup();
                if (error)
                    *error = it.pak + ": ffmpeg's output does not decode as a "
                             "320x240 WebP";
                return false;
            }
            if (lossless) {
                const std::vector<uint8_t>& rgb = frames[k];
                for (size_t p = 0; p < kPixels; ++p) {
                    if (rgba[p * 4] != rgb[p * 3] || rgba[p * 4 + 1] != rgb[p * 3 + 1] ||
                        rgba[p * 4 + 2] != rgb[p * 3 + 2]) {
                        cleanup();
                        if (error)
                            *error = it.pak + ": the lossless WebP does not "
                                     "decode to the .pak's pixels";
                        return false;
                    }
                }
            }
            // Reuse an existing .webp's spelling so a case-sensitive tree does
            // not end up with two.
            std::string name = findCI(it.dir, stemOf(it.pak) + ".webp");
            if (name.empty()) name = stemOf(it.pak) + ".webp";
            const std::string out = joinPath(it.dir, name);
            if (!writeFile(out, file)) {
                cleanup();
                if (error) *error = "cannot write " + out;
                return false;
            }
            fs::remove(joinPath(it.dir, it.pak), ec);
            fs::remove(encoded, ec);
            bytesBefore += pakSizes[k];
            bytesAfter += file.size();
            ++converted;
        }
    }
    cleanup();
    progress.phase((int)items.size(), (int)items.size(), "backgrounds -> WebP");

    char summary[160];
    std::snprintf(summary, sizeof(summary),
                  "  %zu background(s): %.1f MB of .pak -> %.1f MB of .webp",
                  converted, bytesBefore / 1048576.0, bytesAfter / 1048576.0);
    progress.info(summary);
    if (skipped)
        progress.info("  " + std::to_string(skipped) + " file(s) left as .pak");
    return true;
}

}  // namespace re1
