// AssetPath.cpp - Runtime asset-root selection
//
// The original hardcodes a data root in front of every asset path. That root
// differs by region: the North American/GOG version ships a "usa" tree and the
// Japanese PC (Biohazard) version a "jpn" tree. The path templates themselves
// are identical, so the only thing that changes is the root folder.
//
// GAME_DATA_ROOT (see AssetPath.h) is a compile-time macro that stands in for the
// original's GAME_DATA_ROOT; it is baked into string literals and the static
// path templates. That remains the USA default. This translation unit adds a
// runtime override selected from config.ini [Assets] Version so the same build
// can point its readers at the JPN tree without recompiling.
//
// Note on idempotence: the previous ResolveAssetPath rewrite scanned for a
// "\usa\" component and spliced in "\assets\USA\", which double-applied because
// the result still contained "\USA\". ResolveAssetRoot instead substitutes the
// recognized root FORM for the configured root, and the configured root is never
// itself a recognized form, so a second pass cannot match.

#include "AssetPath.h"
#include "../platform/types.h"   // sprintf_s
#include "../platform/platform.h" // plat_normalize_path
#include "../DebugPrint.h"        // dbg_printf
#include <stdio.h>
#include <string.h>

// The configured data root (folder + trailing separator). Defaults to the
// compile-time USA root so release behaviour is unchanged unless config.ini
// selects JPN.
#if !defined(_WIN32)
#define ROOT_USA "./assets/USA/"
#define ROOT_JPN "./assets/JPN/"
#elif defined(_DEBUG)
#define ROOT_USA ".\\assets\\USA\\"
#define ROOT_JPN ".\\assets\\JPN\\"
#else
#define ROOT_USA ".\\usa\\"
#define ROOT_JPN ".\\jpn\\"
#endif

#if defined(_WIN32)
#define PATH_SEP '\\'
#else
#define PATH_SEP '/'
#endif

// Folder holding the USA/ and JPN/ trees (config.ini [Assets] Path). Empty
// means "not configured": the compile-time roots above stay in force.
static char s_base[240] = "";
// Composed root ("<base>/<region>/") used when a base is configured.
static char s_assetRootBuf[260] = "";
const char* s_assetRoot = ROOT_USA;
// Explicit save folder (config.ini [Save] Path). Empty means "<base>/SAVE/".
static char s_saveRoot[260] = "";

// Version is tracked as an int, not by comparing s_assetRoot's pointer against
// a string literal. Debug builds compile without string pooling (/GF is off),
// so two ".\assets\JPN\" literals have different addresses and a pointer
// comparison would report USA even when JPN is selected. Content comparison is
// the safe alternative, but an explicit flag is unambiguous.
static int s_assetIsJpn = 0;

// Content-mode overlay (config.ini [Game] Mode), searched BEFORE the base tree.
// An empty name / NULL root means OG: no overlay, and ResolveAssetRoot then
// does exactly what it did before this existed - no probe, no extra cost.
static char s_modeName[16] = "";
static char s_overlayRootBuf[260] = "";
static const char* s_overlayRoot = NULL;

// Which top-level folders a non-OG mode is allowed to take from the BASE tree.
//
// The intent is that a mode's own tree is authoritative for its content, so
// that OG stays vanilla and nothing silently mixes the two. These five are the
// exceptions, and they are not a policy choice - the Director's Cut disc simply
// has nothing to put there:
//
//   sound, voice  the PS1 keeps its samples in VAB banks and streams speech as
//                 XA audio. There is no per-effect WAV to copy: 547 of the PC
//                 tree's Sound names and 563 of its Voice names have no DC
//                 counterpart at all. The DC's audio is in any case identical
//                 to the USA release's.
//   movie         PS1 .STR, which the port cannot play, and 8 of the PC's 27
//                 movies have no DC counterpart.
//   effspr        PC-only asset class; the DC disc has no such folder.
//   objspr        PC-only asset class; the DC disc has no such folder.
//
//   item_m2/filem_, item_m2/arror
//                 the readable documents. The DC packs all 46 pages into one
//                 FILEM.PIX at a 51200 stride - and every page of it is
//                 BYTE-IDENTICAL to a PS1 OG page, so the DC adds no document
//                 art whatever. The PC's per-file containers hold the same
//                 pictures in a format this build already reads, so falling
//                 back to them loses nothing. (ARROR.TIM is the PC's name for
//                 the DC/PS1 FILEMARR.PIX.)
//
// Everything else - stage rooms, enemy and player models, item art, data - the
// DC does own, so a miss there means the mode tree is INCOMPLETE and the player
// is about to see OG content in a DC session. That is exactly the failure this
// list exists to make visible, so it is logged rather than passed over. Data,
// Item_m2 and Players are only partly covered; those log too, and the log is
// the record of what a mode tree still owes.
//
// Entries are path PREFIXES, matched case-insensitively with either separator.
// A folder name is written with its trailing separator so that "sound/" cannot
// also match a "soundtrack/" that someone adds later.
static const char* const kBaseFallbackPrefixes[] = {
    "sound/", "voice/", "movie/", "effspr/", "objspr/",
    "item_m2/filem_", "item_m2/arror",
};
static const int kBaseFallbackCount =
    (int)(sizeof(kBaseFallbackPrefixes) / sizeof(kBaseFallbackPrefixes[0]));

// Modes whose overlay is sparse BY DESIGN rather than by accident.
//
// Every non-OG mode here is a re-release of the same campaign, so an overlay
// only has to carry what that release actually changes or adds. For the DC that
// is most of the tree, which is exactly why the miss list above is a useful
// "your DC/ tree is incomplete" signal. The Saturn is the opposite case: its 348
// shared rooms decode byte-identical to the PC release's, and its entire
// exclusive content is four enemy models plus fifteen Battle Game arenas
// (docs/SATURN_VERSION_ANALYSIS.md 1, docs/SATURN_PORT.md 0). Every campaign room,
// data table, player model and sound is shared, so stage1..7, data, players and
// item_m2 all MISSING is correct.
//
// Without this, a perfectly healthy SATURN/ folder printed a dozen "SATURN has no
// stage3/ROOM30A0.RDT - falling back to the base tree" lines a session, which
// reads as a broken overlay and is not one. Only that warning is suppressed: the
// per-folder "-> overlay" / "-> BASE TREE" line below still fires, so what the
// overlay did supply stays visible.
static const char* const kSparseOverlayModes[] = { "SATURN" };
static const int kSparseOverlayCount =
    (int)(sizeof(kSparseOverlayModes) / sizeof(kSparseOverlayModes[0]));

// One line the FIRST time each top-level folder resolves, saying which tree it
// came from. The miss log below only fires for a folder the mode is supposed to
// own, and says nothing at all when the overlay is working - so "is my overlay
// live?" and "where did the rooms come from?" had no answer in the log. This
// gives both, at about a dozen lines a session.
static char s_logged[24][20];
static int  s_loggedCount = 0;

static void LogOverlayDecision(const char* tail, int hit)
{
    char key[20];
    int n = 0;
    while (tail[n] != '\0' && tail[n] != '/' && tail[n] != '\\' &&
           n < (int)sizeof(key) - 1) {
        char c = tail[n];
        key[n] = (c >= 'A' && c <= 'Z') ? (char)(c + 32) : c;
        n++;
    }
    key[n] = '\0';

    for (int i = 0; i < s_loggedCount; i++) {
        if (strcmp(s_logged[i], key) == 0) return;
    }
    if (s_loggedCount < (int)(sizeof(s_logged) / sizeof(s_logged[0]))) {
        strcpy(s_logged[s_loggedCount++], key);
    }
    dbg_printf("[assets] %s/%s -> %s\n", s_modeName, key,
               hit ? "overlay" : "BASE TREE");
}

static int FallbackIsExpected(const char* tail)
{
    for (int i = 0; i < kSparseOverlayCount; i++) {
        if (strcmp(s_modeName, kSparseOverlayModes[i]) == 0) {
            return 1;
        }
    }
    for (int i = 0; i < kBaseFallbackCount; i++) {
        const char* a = tail;
        const char* b = kBaseFallbackPrefixes[i];
        while (*b != '\0') {
            char ca = *a;
            char cb = *b;
            if (ca >= 'A' && ca <= 'Z') ca = (char)(ca + 32);
            if (ca == '\\') ca = '/';          // either separator matches
            if (ca != cb) break;
            a++;
            b++;
        }
        if (*b == '\0') {
            return 1;
        }
    }
    return 0;
}

// Rebuild s_assetRoot from the configured base + region. With no base the
// compile-time root is kept, which also keeps every static path template (they
// are compiled against it and patched by character index) valid.
static void ComposeAssetRoot(void)
{
    if (s_base[0] == '\0') {
        s_assetRoot = s_assetIsJpn ? ROOT_JPN : ROOT_USA;
        return;
    }
    sprintf_s(s_assetRootBuf, sizeof(s_assetRootBuf), "%s%c%s%c",
              s_base, PATH_SEP, s_assetIsJpn ? "JPN" : "USA", PATH_SEP);
    s_assetRoot = s_assetRootBuf;
}

// Rebuild the overlay root from the configured base + mode name. Mirrors the
// shape of the base root, so an install with no [Assets] Path gets ".\dc\"
// beside the executable in retail and "./assets/DC/" in a dev build, exactly as
// the base tree is ".\usa\" / "./assets/USA/".
static void ComposeModeRoot(const char* mode, char* out, size_t outSize)
{
    // Port-only: the Japanese Saturn release ("Bio Hazard") has its own overlay
    // folder. Its rooms carry Japanese text in the Biohazard.exe encoding, which
    // only reads right against the JPN tree's data\FONT.TIM, so with that tree
    // selected the SATURN mode reads SATURN_JPN - the folder the asset migrator
    // writes that disc to - and the USA Saturn overlay is never mixed with it.
    if (s_assetIsJpn && _stricmp(mode, "SATURN") == 0) {
        mode = "SATURN_JPN";
    }
    if (s_base[0] != '\0') {
        sprintf_s(out, outSize, "%s%c%s%c", s_base, PATH_SEP, mode, PATH_SEP);
    } else {
#if !defined(_WIN32)
        sprintf_s(out, outSize, "./assets/%s/", mode);
#elif defined(_DEBUG)
        sprintf_s(out, outSize, ".\\assets\\%s\\", mode);
#else
        sprintf_s(out, outSize, ".\\%s\\", mode);
#endif
    }
}

static void ComposeOverlayRoot(void)
{
    if (s_modeName[0] == '\0') {
        s_overlayRoot = NULL;
        return;
    }
    ComposeModeRoot(s_modeName, s_overlayRootBuf, sizeof(s_overlayRootBuf));
    s_overlayRoot = s_overlayRootBuf;
}

int AssetOverlayHasFile(const char* mode, const char* relPath)
{
    if (mode == NULL || mode[0] == '\0' || relPath == NULL) return 0;
    char root[260];
    ComposeModeRoot(mode, root, sizeof(root));
    char candidate[300];
    if (sprintf_s(candidate, sizeof(candidate), "%s%s", root, relPath) < 0) return 0;
    char probeBuf[300];
    FILE* fp = fopen(plat_normalize_path(candidate, probeBuf, sizeof(probeBuf)), "rb");
    if (fp == NULL) return 0;
    fclose(fp);
    return 1;
}

int AssetBaseHasFile(int jpn, const char* relPath)
{
    if (relPath == NULL) return 0;
    if (jpn < 0) jpn = s_assetIsJpn;
    char candidate[300];
    int n;
    if (s_base[0] == '\0') {
        n = sprintf_s(candidate, sizeof(candidate), "%s%s", jpn ? ROOT_JPN : ROOT_USA, relPath);
    } else {
        n = sprintf_s(candidate, sizeof(candidate), "%s%c%s%c%s",
                      s_base, PATH_SEP, jpn ? "JPN" : "USA", PATH_SEP, relPath);
    }
    if (n < 0) return 0;
    char probeBuf[300];
    FILE* fp = fopen(plat_normalize_path(candidate, probeBuf, sizeof(probeBuf)), "rb");
    if (fp == NULL) return 0;
    fclose(fp);
    return 1;
}

void SetAssetMode(const char* mode)
{
    if (mode == NULL || mode[0] == '\0') {
        s_modeName[0] = '\0';
    } else {
        strncpy(s_modeName, mode, sizeof(s_modeName) - 1);
        s_modeName[sizeof(s_modeName) - 1] = '\0';
    }
    ComposeOverlayRoot();
}

const char* GetAssetModeName(void)
{
    return s_modeName;
}

void SetAssetBase(const char* base)
{
    if (base == NULL || base[0] == '\0') {
        s_base[0] = '\0';
        ComposeAssetRoot();
        ComposeOverlayRoot();
        return;
    }

    strncpy(s_base, base, sizeof(s_base) - 1);
    s_base[sizeof(s_base) - 1] = '\0';
    // Drop a trailing separator so composition is uniform.
    size_t n = strlen(s_base);
    while (n > 1 && (s_base[n - 1] == '/' || s_base[n - 1] == '\\')) {
        s_base[--n] = '\0';
    }
    ComposeAssetRoot();
    ComposeOverlayRoot();
}

const char* GetAssetBase(void)
{
    return s_base;
}

void SetSaveRoot(const char* path)
{
    if (path == NULL || path[0] == '\0') {
        s_saveRoot[0] = '\0';
        return;
    }

    strncpy(s_saveRoot, path, sizeof(s_saveRoot) - 1);
    s_saveRoot[sizeof(s_saveRoot) - 1] = '\0';
    size_t n = strlen(s_saveRoot);
    if (n + 1 < sizeof(s_saveRoot) &&
        s_saveRoot[n - 1] != '/' && s_saveRoot[n - 1] != '\\') {
        s_saveRoot[n] = PATH_SEP;
        s_saveRoot[n + 1] = '\0';
    }
}

const char* GetSaveRoot(void)
{
    // The folder is "SAVE" (what the original kept next to the executable), but
    // the path is run through the platform layer so an existing "save" or
    // "Save" directory is found as well - save reads are plain fopen, not
    // LoadFile, so without this a copied Windows SAVE folder was invisible on a
    // case-sensitive filesystem while writes (plat_file_write, which does
    // resolve) went somewhere else. Returned buffer is a shared static, like the
    // default it replaces: callers build a name with it immediately.
    static char s_resolved[260];
    const char* raw;

    if (s_saveRoot[0] != '\0') {
        raw = s_saveRoot;
    } else if (s_base[0] == '\0') {
        raw = GAME_SAVE_ROOT;   // nothing configured
    } else {
        static char s_defaultSave[260];
        sprintf_s(s_defaultSave, sizeof(s_defaultSave), "%s%cSAVE%c",
                  s_base, PATH_SEP, PATH_SEP);
        raw = s_defaultSave;
    }

    plat_normalize_path(raw, s_resolved, sizeof(s_resolved));
    return s_resolved;
}

void SetAssetVersion(const char* version)
{
    if (version != NULL &&
        (version[0] == 'J' || version[0] == 'j') &&
        (version[1] == 'P' || version[1] == 'p')) {
        s_assetIsJpn = 1;
    }
    else {
        s_assetIsJpn = 0;
    }
    ComposeAssetRoot();
    // The SATURN overlay's folder depends on the region (ComposeModeRoot).
    ComposeOverlayRoot();
}

const char* GetAssetRoot(void)
{
    return s_assetRoot;
}

int GetAssetVersion(void)
{
    return s_assetIsJpn;
}

const char* AssetSiblingPath(const char* folder, const char* relPath,
                             char* out, size_t outSize)
{
    if (folder == NULL || out == NULL || outSize == 0) return NULL;
    char root[260];
    ComposeModeRoot(folder, root, sizeof(root));
    if (sprintf_s(out, outSize, "%s%s", root, relPath != NULL ? relPath : "") < 0) {
        return NULL;
    }
    return out;
}

static int ProbeFile(const char* candidate)
{
    char probeBuf[300];
    FILE* fp = fopen(plat_normalize_path(candidate, probeBuf, sizeof(probeBuf)), "rb");
    if (fp == NULL) return 0;
    fclose(fp);
    return 1;
}

// Length of the recognized root form `path` starts with, 0 when it is not an
// asset-rooted path. Both compile-time variants are always matched so a path
// built against either the debug ("assets") or retail root is remapped, e.g.
// the FMV table which hardcodes the retail ".\usa\" form. On non-Windows the
// configured roots are themselves in the list; the substitution is idempotent
// for them (root -> same root).
static size_t MatchRootForm(const char* path)
{
#if !defined(_WIN32)
    static const char* kForms[] = { "./assets/USA/", "./assets/JPN/",
                                    ".\\usa\\", ".\\jpn\\" };
#else
    // The JPN forms belong here too: GAME_DATA_ROOT_JPN paths (the JPN-only
    // document art in MainMenu.cpp) are compiled against them, and without a
    // match ResolveAssetRoot returns early - so those loads skipped the overlay
    // probe entirely on Windows, while the same paths resolved on Linux,
    // whose list always carried all four.
    static const char* kForms[] = { ".\\assets\\USA\\", ".\\usa\\",
                                    ".\\assets\\JPN\\", ".\\jpn\\" };
#endif
    size_t matchLen = 0;
    for (int i = 0; i < 4; i++) {
        size_t len = strlen(kForms[i]);
        if (len > matchLen && strncmp(path, kForms[i], len) == 0) {
            matchLen = len;
        }
    }
    return matchLen;
}

int ResolveAssetAlternate(const char* path, const char* altExt,
                          char* out, size_t outSize)
{
    if (path == NULL || altExt == NULL || out == NULL || outSize == 0) return 0;

    const size_t matchLen = MatchRootForm(path);
    if (matchLen == 0) return 0;
    const char* tail = path + matchLen;

    // The tail with its extension swapped for altExt.
    char altTail[260];
    const char* dot = strrchr(tail, '.');
    const size_t stem = (dot != NULL) ? (size_t)(dot - tail) : strlen(tail);
    if (stem + strlen(altExt) + 1 > sizeof(altTail)) return 0;
    memcpy(altTail, tail, stem);
    strcpy(altTail + stem, altExt);

    char candidate[300];
    if (s_overlayRoot != NULL) {
        if (sprintf_s(candidate, sizeof(candidate), "%s%s", s_overlayRoot, altTail) >= 0 &&
            ProbeFile(candidate)) {
            return sprintf_s(out, outSize, "%s", candidate) >= 0;
        }
        // The overlay's own file in the original format outranks an alternate
        // in the base tree: the overlay is the newer content, whatever its
        // encoding.
        if (sprintf_s(candidate, sizeof(candidate), "%s%s", s_overlayRoot, tail) >= 0 &&
            ProbeFile(candidate)) {
            return 0;
        }
    }
    if (sprintf_s(candidate, sizeof(candidate), "%s%s", s_assetRoot, altTail) >= 0 &&
        ProbeFile(candidate)) {
        return sprintf_s(out, outSize, "%s", candidate) >= 0;
    }
    return 0;
}

const char* AssetSourceKey(const char* path, char* out, size_t outSize)
{
    return AssetSourceKeyAlt(path, NULL, out, outSize);
}

const char* AssetSourceKeyAlt(const char* path, const char* altExt,
                              char* out, size_t outSize)
{
    if (path == NULL || out == NULL || outSize == 0) return NULL;
    const size_t matchLen = MatchRootForm(path);
    if (matchLen == 0) return NULL;
    const char* tail = path + matchLen;

    // The same decision ResolveAssetRoot (and, with altExt,
    // ResolveAssetAlternate) makes: the overlay when it carries the file, the
    // base tree otherwise.
    const char* tree = s_assetIsJpn ? "JPN" : "USA";
    if (s_overlayRoot != NULL) {
        char candidate[300];
        bool there = sprintf_s(candidate, sizeof(candidate), "%s%s", s_overlayRoot, tail) >= 0 &&
                     ProbeFile(candidate);
        if (!there && altExt != NULL) {
            const char* dot = strrchr(tail, '.');
            const int stem = (int)((dot != NULL) ? (size_t)(dot - tail) : strlen(tail));
            there = sprintf_s(candidate, sizeof(candidate), "%s%.*s%s", s_overlayRoot, stem,
                              tail, altExt) >= 0 &&
                    ProbeFile(candidate);
        }
        if (there) {
            tree = (s_assetIsJpn && _stricmp(s_modeName, "SATURN") == 0) ? "SATURN_JPN"
                                                                          : s_modeName;
        }
    }

    size_t n = 0;
    for (const char* s = tree; *s != '\0'; s++) {
        if (n + 1 >= outSize) return NULL;
        out[n++] = (*s >= 'a' && *s <= 'z') ? (char)(*s - 32) : *s;
    }
    if (n + 1 >= outSize) return NULL;
    out[n++] = '/';
    const char* dot = strrchr(tail, '.');
    const char* stop = (dot != NULL) ? dot : tail + strlen(tail);
    for (const char* s = tail; s < stop; s++) {
        if (n + 1 >= outSize) return NULL;
        out[n++] = (*s == '\\') ? '/' : *s;
    }
    out[n] = '\0';
    return out;
}

const char* ResolveAssetRoot(const char* path, char* out, size_t outSize)
{
    if (path == NULL) return NULL;
    if (out == NULL || outSize == 0) return path;

    const size_t matchLen = MatchRootForm(path);
    if (matchLen == 0) return path; // Not an asset-rooted path.

    const char* tail = path + matchLen;

    // Content-mode overlay first (config.ini [Game] Mode). A non-OG mode ships
    // only the files it changes or adds, so a miss here is the normal case and
    // falls through to the base tree below - which is what keeps the base tree
    // authoritative for everything the mode did not touch, and what lets
    // several releases live in one install.
    //
    // The probe has to go through plat_normalize_path: on a case-sensitive
    // filesystem the component case is resolved there, not here, so probing the
    // raw path would miss an overlay stored as "assets/dc/stage1/...". The
    // UNnormalized overlay path is what gets returned, because every caller
    // normalizes the result itself (see LoadFile) and doing it here too would
    // just be redundant work.
    if (s_overlayRoot != NULL) {
        char candidate[260];
        int c = sprintf_s(candidate, sizeof(candidate), "%s%s", s_overlayRoot, tail);
        if (c >= 0) {
            char probeBuf[260];
            const char* probe = plat_normalize_path(candidate, probeBuf, sizeof(probeBuf));
            FILE* fp = fopen(probe, "rb");
            // A sound is asked for as ".wav" and AudioFile_Find takes the ".ogg"
            // beside it (system/AudioFile.h). An overlay that holds only the .ogg
            // (the Saturn's music, migrated as Ogg) must still win here, or the
            // lookup falls through to the base tree, which has neither file.
            if (fp == NULL && c >= 4 && candidate[c - 4] == '.' &&
                (candidate[c - 3] | 0x20) == 'w' && (candidate[c - 2] | 0x20) == 'a' &&
                (candidate[c - 1] | 0x20) == 'v') {
                char ogg[260];
                memcpy(ogg, candidate, (size_t)c - 3);
                memcpy(ogg + c - 3, "ogg", 4);
                char oggBuf[260];
                fp = fopen(plat_normalize_path(ogg, oggBuf, sizeof(oggBuf)), "rb");
            }
            if (fp != NULL) {
                fclose(fp);
                int m = sprintf_s(out, outSize, "%s%s", s_overlayRoot, tail);
                if (m >= 0) {
                    LogOverlayDecision(tail, 1);
                    return out;
                }
            }
            // Falling through to the base tree. For the folders a mode does not
            // own that is normal and silent; anywhere else it means this mode's
            // tree is missing a file it should have supplied, and the game is
            // about to load OG content in a non-OG session.
            else {
                LogOverlayDecision(tail, 0);
                if (!FallbackIsExpected(tail)) {
                    dbg_printf("[assets] %s has no %s - falling back to the base tree\n",
                               s_modeName, tail);
                }
            }
        }
    }

    int n = sprintf_s(out, outSize, "%s%s", s_assetRoot, tail);
    if (n < 0) return path;         // Would not fit: keep the original path.
    return out;
}
