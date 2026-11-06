// vmu_lcd.cpp - the VMU screen shows the player's condition (Dreamcast).
//
// The Dreamcast releases of Resident Evil 2 / 3 put the player's condition and
// ammo on the VMU screen. This does the same for RE1: the status screen's
// condition word and EKG, and the equipped weapon with its ammo count; the
// picture is drawn by vmu_lcd_draw.h.
//
// A low-priority KOS thread samples the game state ~20 times a second and
// sends the bitmap to every VMU with a screen when it changed (and once a
// second anyway, for a VMU plugged in later). It reads a few bytes of game
// state without a lock: a torn read shows one wrong frame at worst. While the
// save code writes a VMU (dc_vmu_busy) the screen is left alone.
#include "../../Globals.h"
#include "../../game/BioCard.h"
#include "../../game/Types.h"
#include "../../game/dc/Items.h"
#include "dc_platform.h"
#include "vmu_lcd_draw.h"

#include <kos.h>
#include <dc/maple.h>
#include <dc/maple/vmu.h>

extern const char* PTR_DAT_004b9278[];   // MenuData.cpp: the status screen's EKG beats

volatile int g_dcVmuBusy = 0;            // dc_platform.h

namespace {

const unsigned kTickMs    = 50;
const unsigned kRefreshMs = 1000;
const int      kMaxScreens = 8;

kthread_t*     s_thread = nullptr;
volatile bool  s_run = false;

const char* WeaponName(unsigned char id, unsigned char qty)
{
    switch (id) {
    case ITEM_KNIFE:             return "KNIFE";
    case ITEM_BERETTA:           return "BERETTA";
    case ITEM_SHOTGUN:           return "SHOTGUN";
    case ITEM_COLT_PYTHON_DUM:   return g_bDcMode ? "CUSTOM" : "PYTHON";
    case ITEM_COLT_PYTHON_MAG:   return "PYTHON";
    case ITEM_FLAMETHROWER:      return "FLAMER";
    case ITEM_BAZOOKA_EXPLOSIVE: return "BAZ EXP";
    case ITEM_BAZOOKA_ACID:      return "BAZ ACD";
    case ITEM_BAZOOKA_FLAME:     return "BAZ FLM";
    case ITEM_ROCKET_LAUNCHER:   return "ROCKET";
    case ITEM_INGRAM:            return "INGRAM";
    case ITEM_MINIMI:            return "MINIMI";
    default:                     (void)qty; return nullptr;
    }
}

// The status screen's rules (menu_draw_health_bar, display_item_qty).
vmulcd::Status Sample()
{
    vmulcd::Status st = { false, 3, false, nullptr, -1 };
    if (g_playingGameFlag == 0 || (g_main_state_flags2 & MSF2_ATTRACT_DEMO) != 0) return st;
    const int maxHp = g_playerEntity.maxHealth;
    if (maxHp == 0) return st;
    st.inGame = true;

    const int hp = g_playerEntity.health;
    if (hp <= 0) {
        st.cond = 0;
        st.dead = true;
    } else {
        const int quarter = maxHp >> 2;
        int c = quarter > 0 ? (hp - 1) / quarter : 3;
        st.cond = c > 3 ? 3 : c;
        if ((g_playerEntity.healthStatusFlags & 0x22) != 0) st.cond = 4;
    }

    const unsigned eq = g_EquippedItemId;
    const unsigned char* slots = (const unsigned char*)g_ItemSlotsPointer;
    if (eq != 0 && eq <= 12 && slots != nullptr) {
        const unsigned char id = slots[(eq - 1) * 2], qty = slots[(eq - 1) * 2 + 1];
        st.weapon = WeaponName(id, qty);
        if (st.weapon != nullptr && id != ITEM_KNIFE) {
            const bool inf = id > ITEM_NON_INFINITE_MAX ||
                (id == ITEM_ROCKET_LAUNCHER && Flg_ck((int)g_ScenarioFlags, SCENARIO_FLAG_INF_R_LAUNCHER)) ||
                (id == ITEM_COLT_PYTHON_MAG && dc_is_infinite_colt_python(id));
            st.ammo = inf ? -2 : (id != ITEM_FLAMETHROWER ? (qty & 0x7F) : qty);
        }
    }
    return st;
}

void* Thread(void*)
{
    static vmulcd::Screen screen(PTR_DAT_004b9278);
    static uint32_t sent[48] __attribute__((aligned(4)));
    maple_device_t* seen[kMaxScreens] = {};
    uint64_t lastSend = 0;
    bool any = false;

    while (s_run) {
        const uint64_t now = timer_ms_gettime64();
        const uint8_t* bits = screen.Draw(Sample(), (uint32_t)now);

        // Who has a screen now (a VMU can come and go with its controller).
        maple_device_t* devs[kMaxScreens] = {};
        int n = 0;
        for (int i = 0; n < kMaxScreens; ++i) {
            maple_device_t* d = maple_enum_type(i, MAPLE_FUNC_LCD);
            if (d == nullptr) break;
            devs[n++] = d;
        }
        const bool newDevice = memcmp(devs, seen, sizeof(devs)) != 0;
        const bool changed = !any || memcmp(bits, sent, sizeof(sent)) != 0;

        if (n > 0 && g_dcVmuBusy == 0 && (changed || newDevice || now - lastSend >= kRefreshMs)) {
            memcpy(sent, bits, sizeof(sent));
            bool ok = true;
            for (int i = 0; i < n; ++i)
                if (vmu_draw_lcd_rotated(devs[i], sent) != MAPLE_EOK) ok = false;
            if (ok) {
                memcpy(seen, devs, sizeof(seen));
                lastSend = now;
                any = true;
            } else {
                any = false;   // a frame was busy: send again next tick
            }
        }
        thd_sleep(kTickMs);
    }
    return nullptr;
}

}  // namespace

void dc_vmu_lcd_start(void)
{
    if (s_thread != nullptr) return;
    s_run = true;
    s_thread = thd_create(0, Thread, nullptr);
    // Below the game thread (10): the screen waits for the game, never the
    // other way round.
    if (s_thread != nullptr) thd_set_prio(s_thread, 12);
}
