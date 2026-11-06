// vmu_lcd_draw.h - the picture on the VMU screen (Dreamcast).
//
// Pure drawing, no KOS: vmu_lcd.cpp feeds it the game state and sends the
// bitmap; a host test can include it on its own. 48 x 32, 1 bpp, rows of 6
// bytes, MSB = leftmost pixel, first byte = top-left (vmu_draw_lcd_rotated).
//
//   rows  0..6   the condition word, as on the status screen: FINE / CAUTION /
//                DANGER / POISON (the last two blink, like the screen's word)
//   rows  8..23  the EKG, swept left to right: one heartbeat per sweep, drawn
//                from the status screen's own beat shapes (PTR_DAT_004b9278,
//                4 variants per condition, 48 pixels wide - the VMU's width);
//                a flat line once the player is dead
//   rows 25..31  the equipped weapon and its ammo (the status screen's count:
//                infinite weapons get the infinity sign)
//
// Outside a game (title, movies, attract demo) the screen reads RESIDENT EVIL.
// The lettering is typewriter style: monospaced, with slab serifs.
#pragma once
#include <stdint.h>
#include <string.h>

namespace vmulcd {

// Typewriter-style bitmap fonts (slab serifs, monospaced). Row bitmaps,
// MSB = leftmost pixel of the glyph's width.
static const uint8_t kFont57[36][7] = {   // condition word, title, 5 x 7
    {0x04,0x0A,0x0A,0x11,0x1F,0x11,0x1B}, // A
    {0x1E,0x09,0x09,0x0E,0x09,0x09,0x1E}, // B
    {0x0F,0x11,0x10,0x10,0x10,0x11,0x0E}, // C
    {0x1E,0x09,0x09,0x09,0x09,0x09,0x1E}, // D
    {0x1F,0x09,0x0A,0x0E,0x0A,0x09,0x1F}, // E
    {0x1F,0x09,0x0A,0x0E,0x0A,0x08,0x1C}, // F
    {0x0F,0x11,0x10,0x13,0x11,0x11,0x0E}, // G
    {0x1B,0x11,0x11,0x1F,0x11,0x11,0x1B}, // H
    {0x1F,0x04,0x04,0x04,0x04,0x04,0x1F}, // I
    {0x07,0x02,0x02,0x02,0x12,0x12,0x0C}, // J
    {0x1B,0x0A,0x0C,0x0C,0x0A,0x0A,0x1B}, // K
    {0x1C,0x08,0x08,0x08,0x08,0x09,0x1F}, // L
    {0x11,0x1B,0x15,0x15,0x11,0x11,0x1B}, // M
    {0x1B,0x19,0x15,0x15,0x15,0x13,0x19}, // N
    {0x0E,0x11,0x11,0x11,0x11,0x11,0x0E}, // O
    {0x1E,0x09,0x09,0x0E,0x08,0x08,0x1C}, // P
    {0x0E,0x11,0x11,0x11,0x11,0x0E,0x06}, // Q
    {0x1E,0x09,0x09,0x0E,0x0A,0x09,0x1B}, // R
    {0x0F,0x11,0x10,0x0E,0x01,0x11,0x1E}, // S
    {0x1F,0x15,0x04,0x04,0x04,0x04,0x0E}, // T
    {0x1B,0x11,0x11,0x11,0x11,0x11,0x0E}, // U
    {0x1B,0x11,0x11,0x0A,0x0A,0x04,0x04}, // V
    {0x1B,0x11,0x11,0x15,0x15,0x1B,0x11}, // W
    {0x1B,0x0A,0x0A,0x04,0x0A,0x0A,0x1B}, // X
    {0x1B,0x11,0x0A,0x04,0x04,0x04,0x0E}, // Y
    {0x1F,0x12,0x02,0x04,0x08,0x09,0x1F}, // Z
    {0x0E,0x11,0x11,0x11,0x11,0x11,0x0E}, // 0
    {0x04,0x0C,0x04,0x04,0x04,0x04,0x0E}, // 1
    {0x0E,0x11,0x01,0x02,0x04,0x09,0x1F}, // 2
    {0x0E,0x11,0x01,0x06,0x01,0x11,0x0E}, // 3
    {0x02,0x06,0x0A,0x12,0x1F,0x02,0x07}, // 4
    {0x1F,0x10,0x1E,0x01,0x01,0x11,0x0E}, // 5
    {0x06,0x08,0x10,0x1E,0x11,0x11,0x0E}, // 6
    {0x1F,0x11,0x02,0x02,0x04,0x04,0x04}, // 7
    {0x0E,0x11,0x11,0x0E,0x11,0x11,0x0E}, // 8
    {0x0E,0x11,0x11,0x0F,0x01,0x02,0x0C}, // 9
};
static const uint8_t kFont47[37][7] = {   // weapon and ammo, 4 x 7
    {0x06,0x09,0x09,0x0F,0x09,0x09,0x09}, // A
    {0x0E,0x05,0x05,0x06,0x05,0x05,0x0E}, // B
    {0x07,0x09,0x08,0x08,0x08,0x09,0x06}, // C
    {0x0E,0x05,0x05,0x05,0x05,0x05,0x0E}, // D
    {0x0F,0x05,0x04,0x06,0x04,0x05,0x0F}, // E
    {0x0F,0x05,0x04,0x06,0x04,0x04,0x0E}, // F
    {0x07,0x08,0x08,0x0B,0x09,0x09,0x06}, // G
    {0x09,0x09,0x09,0x0F,0x09,0x09,0x09}, // H
    {0x0E,0x04,0x04,0x04,0x04,0x04,0x0E}, // I
    {0x03,0x01,0x01,0x01,0x09,0x09,0x06}, // J
    {0x0D,0x05,0x06,0x06,0x05,0x05,0x0D}, // K
    {0x0E,0x04,0x04,0x04,0x04,0x05,0x0F}, // L
    {0x09,0x0F,0x0F,0x09,0x09,0x09,0x09}, // M
    {0x09,0x0D,0x0D,0x0B,0x0B,0x09,0x09}, // N
    {0x06,0x09,0x09,0x09,0x09,0x09,0x06}, // O
    {0x0E,0x05,0x05,0x06,0x04,0x04,0x0E}, // P
    {0x06,0x09,0x09,0x09,0x09,0x06,0x03}, // Q
    {0x0E,0x05,0x05,0x06,0x05,0x05,0x0D}, // R
    {0x07,0x08,0x08,0x06,0x01,0x01,0x0E}, // S
    {0x0F,0x0A,0x04,0x04,0x04,0x04,0x04}, // T
    {0x09,0x09,0x09,0x09,0x09,0x09,0x06}, // U
    {0x09,0x09,0x09,0x09,0x06,0x06,0x06}, // V
    {0x09,0x09,0x09,0x09,0x0F,0x0F,0x09}, // W
    {0x09,0x09,0x06,0x06,0x06,0x09,0x09}, // X
    {0x09,0x09,0x09,0x06,0x04,0x04,0x0E}, // Y
    {0x0F,0x09,0x02,0x04,0x08,0x09,0x0F}, // Z
    {0x06,0x09,0x09,0x09,0x09,0x09,0x06}, // 0
    {0x04,0x0C,0x04,0x04,0x04,0x04,0x0E}, // 1
    {0x06,0x09,0x01,0x02,0x04,0x09,0x0F}, // 2
    {0x06,0x09,0x01,0x06,0x01,0x09,0x06}, // 3
    {0x02,0x06,0x0A,0x0A,0x0F,0x02,0x07}, // 4
    {0x0F,0x08,0x0E,0x01,0x01,0x09,0x06}, // 5
    {0x06,0x08,0x08,0x0E,0x09,0x09,0x06}, // 6
    {0x0F,0x09,0x01,0x02,0x02,0x04,0x04}, // 7
    {0x06,0x09,0x09,0x06,0x09,0x09,0x06}, // 8
    {0x06,0x09,0x09,0x07,0x01,0x01,0x06}, // 9
    {0x00,0x00,0x00,0x0F,0x00,0x00,0x00}, // -
};
static const uint8_t kInfinity[5] = {0x00,0x36,0x49,0x49,0x36}; // 7 x 5

enum { kW = 48, kH = 32, kRowBytes = 6, kNone = 127 };

struct Status {
    bool        inGame;   // a game is running (not title / movie / demo)
    int         cond;     // 0 danger, 1 caution, 2-3 fine, 4 poisoned (the status screen's index)
    bool        dead;     // health 0: flat line
    const char* weapon;   // up to 7 characters (A-Z, 0-9, '-', ' '), NULL = none
    int         ammo;     // -1 no count, -2 infinite, else the count
};

class Screen {
public:
    // waves: the status screen's 20 beat shapes, 4 per condition 0..4.
    explicit Screen(const char* const* waves) : m_waves(waves) { Reset(); }

    void Reset()
    {
        for (int x = 0; x < kW; ++x) m_col[x] = kNone;
        m_beat = NULL; m_variant = 0; m_lastCol = -1; m_phase = 0; m_lastMs = 0;
        m_rng = 0x1234567u; m_cond = 3; m_started = false;
    }

    // Advance to `nowMs` and redraw. Returns the bitmap (192 bytes, 4-aligned).
    const uint8_t* Draw(const Status& st, uint32_t nowMs)
    {
        memset(m_bits, 0, sizeof(m_bits));
        if (!st.inGame) {
            Reset();
            Text57("RESIDENT", 7);
            Text57("EVIL", 17);
            return Bytes();
        }
        Sweep(st, nowMs);
        DrawEkg();
        const bool blinkOff = (st.cond == 0 || st.cond == 4) && ((nowMs / 250) % 4) == 3;
        if (!blinkOff) Text57(CondWord(st.cond), 0);
        // Bottom row, 4 x 7 type on a 5-pixel pitch: the weapon on the left,
        // its ammo flush right; the name gives way if both do not fit.
        int ammoX = kW + 1;
        if (st.ammo == -2) {
            ammoX = kW - 7;
            Glyph(kInfinity, 5, 7, ammoX, 26);
        } else if (st.ammo >= 0) {
            char tmp[8];
            int n = st.ammo > 999 ? 999 : st.ammo, len = 0;
            do { tmp[len++] = (char)('0' + n % 10); n /= 10; } while (n != 0);
            ammoX = kW - (len * 5 - 1);
            for (int i = 0; i < len; ++i)
                Glyph(kFont47[26 + (tmp[len - 1 - i] - '0')], 7, 4, ammoX + i * 5, 25);
        }
        if (st.weapon != NULL) Text47(st.weapon, 0, 25, (ammoX - 2 + 1) / 5);
        return Bytes();
    }

private:
    const char* const* m_waves;
    const char* m_beat;
    int         m_variant, m_lastCol, m_cond;
    uint32_t    m_phase;      // sweep position, 1/65536 of the screen width
    uint32_t    m_lastMs, m_rng;
    bool        m_started;
    signed char m_col[kW];    // EKG height per column (status-screen pixels), kNone = gap
    uint32_t    m_bitsAligned[kW];   // 192 bytes
    uint8_t     m_bits[kW * kH / 8];

    const uint8_t* Bytes()
    {
        memcpy(m_bitsAligned, m_bits, sizeof(m_bits));
        return (const uint8_t*)m_bitsAligned;
    }

    static const char* CondWord(int c)
    {
        switch (c) {
        case 0:  return "DANGER";
        case 1:  return "CAUTION";
        case 4:  return "POISON";
        default: return "FINE";
        }
    }

    // One sweep is one heartbeat; a weaker heart beats faster.
    static uint32_t PeriodMs(int c)
    {
        switch (c) {
        case 0:  return 650;
        case 1:  return 800;
        case 2:  return 900;
        case 4:  return 850;
        default: return 1000;
        }
    }

    uint32_t Rand() { m_rng = m_rng * 1103515245u + 12345u; return m_rng >> 16; }

    // A new beat, picked like the status screen does it: a random variant of
    // the condition's four, never the same one twice in a row.
    void NewBeat(const Status& st)
    {
        m_cond = st.cond < 0 ? 0 : st.cond > 4 ? 4 : st.cond;
        int v = (int)(Rand() & 3);
        if (v == m_variant) v = (v + 1) & 3;
        m_variant = v;
        m_beat = st.dead ? NULL : m_waves[m_cond * 4 + v];
    }

    // Height of the beat at column x: the shape is a polyline from (47, 0)
    // through each record's (X, Y) down to X = 0 (records {X, Xprev, Y, slope}).
    int WaveAt(int x) const
    {
        if (m_beat == NULL) return 0;
        int px = 47, py = 0;
        for (const char* r = m_beat;; r += 4) {
            const int cx = (unsigned char)r[0], cy = r[2];
            if (x <= px && x >= cx) {
                if (px == cx) return cy;
                return cy + (py - cy) * (x - cx) / (px - cx);
            }
            if (cx == 0) break;
            px = cx; py = cy;
        }
        return 0;
    }

    void Sweep(const Status& st, uint32_t nowMs)
    {
        if (!m_started) {
            m_started = true;
            m_lastMs = nowMs;
            m_phase = 0;
            m_lastCol = -1;
            NewBeat(st);
        }
        uint32_t dt = nowMs - m_lastMs;
        if (dt > 200) dt = 200;              // a long load: do not jump a whole sweep
        m_lastMs = nowMs;
        m_phase += dt * 65536u / PeriodMs(m_cond);
        while (true) {
            int col = (int)(((m_phase > 65535u ? 65535u : m_phase) * kW) >> 16);
            for (int x = m_lastCol + 1; x <= col && x < kW; ++x) m_col[x] = (signed char)WaveAt(x);
            m_lastCol = col;
            if (m_phase < 65536u) break;
            m_phase -= 65536u;               // the next beat starts at the left edge
            m_lastCol = -1;
            NewBeat(st);
        }
        if (st.dead && m_beat != NULL) m_beat = NULL;
        for (int g = 1; g <= 4; ++g) {       // the gap ahead of the trace
            const int x = m_lastCol + g;
            if (x >= 0 && x < kW) m_col[x] = kNone;
        }
    }

    void Set(int x, int y)
    {
        if (x < 0 || x >= kW || y < 0 || y >= kH) return;
        m_bits[y * kRowBytes + (x >> 3)] |= (uint8_t)(0x80 >> (x & 7));
    }

    // Status-screen pixels to VMU rows: 3/4 scale around row 19.
    static int Row(int v) { return 19 + (v * 3 + (v < 0 ? -1 : 1)) / 4; }

    void DrawEkg()
    {
        int prev = kNone;
        for (int x = 0; x < kW; ++x) {
            if (m_col[x] == kNone) { prev = kNone; continue; }
            const int y = Row(m_col[x]);
            if (prev == kNone) {
                Set(x, y);
            } else {
                const int a = prev < y ? prev : y, b = prev < y ? y : prev;
                for (int yy = a; yy <= b; ++yy) Set(x, yy);
            }
            prev = y;
        }
        if (m_lastCol >= 0 && m_lastCol < kW && m_col[m_lastCol] != kNone) {   // the bright head
            const int y = Row(m_col[m_lastCol]);
            Set(m_lastCol, y - 1); Set(m_lastCol, y + 1);
            Set(m_lastCol - 1, y);
        }
    }

    void Glyph(const uint8_t* rows, int h, int w, int x0, int y0)
    {
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x)
                if (rows[y] & (1 << (w - 1 - x))) Set(x0 + x, y0 + y);
    }

    static int Index(char c)
    {
        if (c >= 'A' && c <= 'Z') return c - 'A';
        if (c >= '0' && c <= '9') return 26 + (c - '0');
        if (c == '-') return 36;
        return -1;
    }

    void Text57(const char* s, int y)
    {
        const int n = (int)strlen(s);
        int x = (kW - (n * 6 - 1)) / 2;
        for (int i = 0; i < n; ++i, x += 6) {
            const int g = Index(s[i]);
            if (g >= 0 && g < 36) Glyph(kFont57[g], 7, 5, x, y);
        }
    }

    void Text47(const char* s, int x, int y, int maxChars)
    {
        for (int i = 0; s[i] != '\0' && i < maxChars; ++i, x += 5) {
            const int g = Index(s[i]);
            if (g >= 0) Glyph(kFont47[g], 7, 4, x, y);
        }
    }
};

}  // namespace vmulcd
