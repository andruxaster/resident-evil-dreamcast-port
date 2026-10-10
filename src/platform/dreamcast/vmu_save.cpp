// vmu_save.cpp - save games on the VMU (Dreamcast).
//
// The game reads and writes savedat<N>.dat with plain fopen/fwrite under
// GetSaveRoot(). It cannot point at the VMU directly: VMU files are stored in
// 512-byte blocks, so a 2690-byte save would read back as 3072 bytes - and
// ReadSaveFile reads ftell() bytes into a SAVE_FILE_SIZE + 8 stack buffer.
//
// So the game's save root is the RAM disk (/ram/), and this module mirrors it:
//   - at boot, every SAVEDAT*.DAT on the first VMU is unpacked into /ram/
//     (exact length, kept in a small prefix inside the payload); slots the VMU
//     does not have are imported from the disc's SAVE/ folder (PC saves), if any
//   - after every successful FileWrite into the save root, the file is written
//     to the VMU, wrapped in a VMS header (name, icon) so the Dreamcast BIOS
//     memory manager lists it as "RESIDENT EVIL".
#include "../platform.h"
#include "dc_platform.h"

#include <kos.h>
#include <dc/maple.h>
#include <dc/vmu_pkg.h>
#include <dc/fs_vmu.h>

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

namespace {

const char  kRamRoot[] = "/ram/";
const uint8_t kMagic[4] = { 'R', 'E', '1', 'S' };   // payload prefix: magic + u32 length

char s_vmuRoot[16] = "";        // "/vmu/a1" etc., empty = no VMU
vmu_pkg_t s_pkg;

// 32x32, 4 bpp, high nibble = left pixel. 0 background, 1 letters, 2 border.
uint8_t s_icon[512] = {
    0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x22,
    0x20, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02,
    0x20, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02,
    0x20, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02,
    0x20, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02,
    0x20, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02,
    0x20, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02,
    0x20, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02,
    0x20, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02,
    0x20, 0x00, 0x01, 0x11, 0x11, 0x11, 0x10, 0x00, 0x01, 0x11, 0x11, 0x11, 0x11, 0x10, 0x00, 0x02,
    0x20, 0x00, 0x01, 0x11, 0x11, 0x11, 0x10, 0x00, 0x01, 0x11, 0x11, 0x11, 0x11, 0x10, 0x00, 0x02,
    0x20, 0x00, 0x01, 0x10, 0x00, 0x00, 0x01, 0x10, 0x01, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02,
    0x20, 0x00, 0x01, 0x10, 0x00, 0x00, 0x01, 0x10, 0x01, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02,
    0x20, 0x00, 0x01, 0x10, 0x00, 0x00, 0x01, 0x10, 0x01, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02,
    0x20, 0x00, 0x01, 0x10, 0x00, 0x00, 0x01, 0x10, 0x01, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02,
    0x20, 0x00, 0x01, 0x11, 0x11, 0x11, 0x10, 0x00, 0x01, 0x11, 0x11, 0x11, 0x10, 0x00, 0x00, 0x02,
    0x20, 0x00, 0x01, 0x11, 0x11, 0x11, 0x10, 0x00, 0x01, 0x11, 0x11, 0x11, 0x10, 0x00, 0x00, 0x02,
    0x20, 0x00, 0x01, 0x10, 0x01, 0x10, 0x00, 0x00, 0x01, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02,
    0x20, 0x00, 0x01, 0x10, 0x01, 0x10, 0x00, 0x00, 0x01, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02,
    0x20, 0x00, 0x01, 0x10, 0x00, 0x01, 0x10, 0x00, 0x01, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02,
    0x20, 0x00, 0x01, 0x10, 0x00, 0x01, 0x10, 0x00, 0x01, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02,
    0x20, 0x00, 0x01, 0x10, 0x00, 0x00, 0x01, 0x10, 0x01, 0x11, 0x11, 0x11, 0x11, 0x10, 0x00, 0x02,
    0x20, 0x00, 0x01, 0x10, 0x00, 0x00, 0x01, 0x10, 0x01, 0x11, 0x11, 0x11, 0x11, 0x10, 0x00, 0x02,
    0x20, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02,
    0x20, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02,
    0x20, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02,
    0x20, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02,
    0x20, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02,
    0x20, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02,
    0x20, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02,
    0x20, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02,
    0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x22,
};

bool FindVmu()
{
    maple_device_t* dev = maple_enum_type(0, MAPLE_FUNC_MEMCARD);
    if (dev == nullptr) return false;
    snprintf(s_vmuRoot, sizeof(s_vmuRoot), "/vmu/%c%d", 'a' + dev->port, dev->unit);
    return true;
}

void SetupHeader()
{
    memset(&s_pkg, 0, sizeof(s_pkg));
    strcpy(s_pkg.desc_short, "RESIDENT EVIL");
    strcpy(s_pkg.desc_long, "Resident Evil save data");
    strcpy(s_pkg.app_id, "RE1 DC PORT");
    s_pkg.icon_cnt = 1;
    s_pkg.icon_anim_speed = 0;
    s_pkg.eyecatch_type = VMUPKG_EC_NONE;
    s_pkg.icon_pal[0] = 0xF111;      // near black
    s_pkg.icon_pal[1] = 0xFB00;      // blood red
    s_pkg.icon_pal[2] = 0xF600;      // dark red border
    for (int i = 3; i < 16; ++i) s_pkg.icon_pal[i] = 0xF000;
    s_pkg.icon_data = s_icon;
    fs_vmu_set_default_header(&s_pkg);
}

// VMU file name for a save path: upper-case basename, at most 12 characters.
bool VmuName(const char* path, char* out, size_t outSize)
{
    const char* base = strrchr(path, '/');
    const char* bs = strrchr(path, '\\');
    if (bs != nullptr && (base == nullptr || bs > base)) base = bs;
    base = base ? base + 1 : path;
    const size_t n = strlen(base);
    if (n == 0 || n > 12 || n + 1 > outSize) return false;
    for (size_t i = 0; i <= n; ++i) {
        char c = base[i];
        out[i] = (c >= 'a' && c <= 'z') ? (char)(c - 'a' + 'A') : c;
    }
    return true;
}

bool WriteRam(const char* name, const void* data, size_t len)
{
    char path[64];
    snprintf(path, sizeof(path), "%s%s", kRamRoot, name);
    for (char* p = path + sizeof(kRamRoot) - 1; *p; ++p) if (*p >= 'A' && *p <= 'Z') *p = (char)(*p - 'A' + 'a');
    FILE* f = fopen(path, "wb");
    if (f == nullptr) return false;
    const bool ok = fwrite(data, 1, len, f) == len;
    fclose(f);
    return ok;
}

// Read a VMU save (header already stripped by fs_vmu) and unpack the prefix.
bool LoadFromVmu(const char* vmuName)
{
    char path[48];
    snprintf(path, sizeof(path), "%s/%s", s_vmuRoot, vmuName);
    FILE* f = fopen(path, "rb");
    if (f == nullptr) return false;
    fseek(f, 0, SEEK_END);
    const long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (size < 8 || size > 64 * 1024) { fclose(f); return false; }
    uint8_t* buf = (uint8_t*)malloc((size_t)size);
    if (buf == nullptr) { fclose(f); return false; }
    const bool got = fread(buf, 1, (size_t)size, f) == (size_t)size;
    fclose(f);
    bool ok = false;
    if (got && memcmp(buf, kMagic, 4) == 0) {
        const uint32_t len = buf[4] | (buf[5] << 8) | (buf[6] << 16) | ((uint32_t)buf[7] << 24);
        if (len <= (uint32_t)size - 8) ok = WriteRam(vmuName, buf + 8, len);
    }
    free(buf);
    return ok;
}

}  // namespace

// Called once at boot, after the config is loaded.
void dc_save_init(void)
{
    SetupHeader();

    int fromVmu = 0;
    if (FindVmu()) {
        DIR* d = opendir(s_vmuRoot);
        if (d != nullptr) {
            struct dirent* e;
            while ((e = readdir(d)) != nullptr) {
                if (strncasecmp(e->d_name, "SAVEDAT", 7) != 0) continue;
                if (LoadFromVmu(e->d_name)) fromVmu++;
                else printf("[SAVE] %s/%s is not an RE1 save, ignored\n", s_vmuRoot, e->d_name);
            }
            closedir(d);
        }
        printf("[SAVE] VMU %s: %d save file(s) loaded\n", s_vmuRoot, fromVmu);
    } else {
        printf("[SAVE] no VMU found - saves last until power-off only\n");
    }

    // Import PC saves shipped on the disc for slots the VMU does not have.
    char exeDir[64];
    if (plat_exe_dir(exeDir, sizeof(exeDir))) {
        for (int i = 1; i <= 9; ++i) {
            char name[16], ram[48], disc[96];
            snprintf(name, sizeof(name), "savedat%d.dat", i);
            snprintf(ram, sizeof(ram), "%s%s", kRamRoot, name);
            FILE* have = fopen(ram, "rb");
            if (have != nullptr) { fclose(have); continue; }
            snprintf(disc, sizeof(disc), "%s/SAVE/%s", exeDir, name);
            char norm[128];
            size_t size = 0;
            void* data = plat_file_read_all(plat_normalize_path(disc, norm, sizeof(norm)), &size);
            if (data != nullptr) {
                if (WriteRam(name, data, size)) printf("[SAVE] imported %s from the disc\n", name);
                free(data);
            }
        }
    }
}

const char* dc_save_root(void) { return kRamRoot; }

// After a successful write into the save root: mirror it to the VMU.
extern "C" void dc_save_commit(const char* path, const void* data, int len)
{
    if (path == nullptr || data == nullptr || len <= 0) return;
    if (strncmp(path, kRamRoot, sizeof(kRamRoot) - 1) != 0) return;
    if (s_vmuRoot[0] == '\0' && !FindVmu()) {
        printf("[SAVE] no VMU - %s kept in RAM only\n", path);
        return;
    }
    char name[16];
    if (!VmuName(path, name, sizeof(name))) return;

    uint8_t* buf = (uint8_t*)malloc((size_t)len + 8);
    if (buf == nullptr) return;
    memcpy(buf, kMagic, 4);
    buf[4] = (uint8_t)len; buf[5] = (uint8_t)(len >> 8);
    buf[6] = (uint8_t)(len >> 16); buf[7] = (uint8_t)(len >> 24);
    memcpy(buf + 8, data, (size_t)len);

    char vpath[48];
    snprintf(vpath, sizeof(vpath), "%s/%s", s_vmuRoot, name);
    const uint64_t t0 = timer_ms_gettime64();
    FILE* f = fopen(vpath, "wb");
    bool ok = false;
    if (f != nullptr) {
        ok = fwrite(buf, 1, (size_t)len + 8, f) == (size_t)len + 8;
        ok = (fclose(f) == 0) && ok;      // fs_vmu writes the blocks on close
    }
    free(buf);
    if (ok) printf("[SAVE] %s written (%d bytes, %u ms)\n", vpath, len, (unsigned)(timer_ms_gettime64() - t0));
    else    printf("[SAVE] writing %s FAILED (VMU full or removed?)\n", vpath);
}
