// The Saturn wardrobe as an RDT patch - see SaturnWardrobe.h. The byte-level
// reference is tools/saturn_wardrobe_rdt.py; keep the two in step.
#include "core/SaturnWardrobe.h"

#include "core/Util.h"

#include <algorithm>
#include <cstring>

namespace re1 {

namespace {

typedef std::vector<uint8_t> Bytes;

// The Saturn's three questions (GAME5 0x0609E8BC / 0x0609E8F9 / 0x0609E93A)
// without their `08 02 0A 04 01 00` tail (yes/no whose yes is post-action 4).
const uint8_t kSatC0[] = {  // "This costume looks cool. / Will you change your outfit?"
    0x02, 0x30, 0x44, 0x45, 0x4f, 0x00, 0x3f, 0x4b, 0x4f, 0x50, 0x51, 0x49, 0x41,
    0x00, 0x48, 0x4b, 0x4b, 0x47, 0x4f, 0x00, 0x3f, 0x4b, 0x4b, 0x48, 0x79, 0x03,
    0x00, 0x33, 0x45, 0x48, 0x48, 0x00, 0x55, 0x4b, 0x51, 0x00, 0x3f, 0x44, 0x3d,
    0x4a, 0x43, 0x41, 0x00, 0x55, 0x4b, 0x51, 0x4e, 0x02, 0x4b, 0x51, 0x50, 0x42,
    0x45, 0x50, 0x1b};
const uint8_t kSatC1[] = {  // "This costume is my favorite. / Will you change your outfit?"
    0x02, 0x30, 0x44, 0x45, 0x4f, 0x00, 0x3f, 0x4b, 0x4f, 0x50, 0x51, 0x49, 0x41,
    0x00, 0x45, 0x4f, 0x00, 0x49, 0x55, 0x00, 0x42, 0x3d, 0x52, 0x4b, 0x4e, 0x45,
    0x50, 0x41, 0x79, 0x03, 0x00, 0x33, 0x45, 0x48, 0x48, 0x00, 0x55, 0x4b, 0x51,
    0x00, 0x3f, 0x44, 0x3d, 0x4a, 0x43, 0x41, 0x00, 0x55, 0x4b, 0x51, 0x4e, 0x02,
    0x4b, 0x51, 0x50, 0x42, 0x45, 0x50, 0x1b};
const uint8_t kSatC2[] = {  // "This fits perfectly.   / Will you change your outfit?"
    0x02, 0x30, 0x44, 0x45, 0x4f, 0x00, 0x42, 0x45, 0x50, 0x4f, 0x00, 0x4c, 0x41,
    0x4e, 0x42, 0x41, 0x3f, 0x50, 0x48, 0x55, 0x79, 0x00, 0x00, 0x00, 0x03, 0x00,
    0x33, 0x45, 0x48, 0x48, 0x00, 0x55, 0x4b, 0x51, 0x00, 0x3f, 0x44, 0x3d, 0x4a,
    0x43, 0x41, 0x00, 0x55, 0x4b, 0x51, 0x4e, 0x02, 0x4b, 0x51, 0x50, 0x42, 0x45,
    0x50, 0x1b};
// The Japanese disc's three (Bio Hazard GAME5 0x0609EAB7 / 0x0609EAE2 /
// 0x0609EB0D, pointers at 0x0609EC80), same order and meaning, in the PC
// Biohazard.exe encoding. Their GAME5 tail is `02 08 02 0A 04 01 00`: one more
// line break before the yes/no than the USA's. It is dropped with the rest of
// the tail, so the question ends at its '?' like the room's own yes/no
// (message 1: `...か?` then `08 00 01 01`) - the line the room message box
// lays its YES / NO under.
const uint8_t kSatJpnC0[] = {  // "\nとてもカッコイイコスチュームがある / コスチュームチェンジしますか?"
    0x02, 0x6a, 0x69, 0x79, 0xac, 0xf6, 0xb0, 0xa8, 0xa8, 0xb0, 0xb3, 0xb7, 0xf4,
    0x3b, 0xc7, 0x85, 0x57, 0x7f, 0x00, 0x03, 0x00, 0xb0, 0xb3, 0xb7, 0xf4, 0x3b,
    0xc7, 0xb7, 0xf1, 0xd4, 0xdb, 0x62, 0x75, 0x63, 0x5c, 0x1b};
const uint8_t kSatJpnC1[] = {  // "\n みたことのあるコスチュームがある / コスチュームチェンジしますか?"
    0x02, 0x00, 0x76, 0x66, 0x60, 0x6a, 0x6f, 0x57, 0x7f, 0xb0, 0xb3, 0xb7, 0xf4,
    0x3b, 0xc7, 0x85, 0x57, 0x7f, 0x00, 0x03, 0x00, 0xb0, 0xb3, 0xb7, 0xf4, 0x3b,
    0xc7, 0xb7, 0xf1, 0xd4, 0xdb, 0x62, 0x75, 0x63, 0x5c, 0x1b};
const uint8_t kSatJpnC2[] = {  // "\n  サイズのピッタリなものがある / コスチュームチェンジしますか?"
    0x02, 0x00, 0x00, 0xb1, 0xa8, 0xdc, 0x6f, 0xea, 0xf6, 0xb6, 0xce, 0x6b, 0x79,
    0x6f, 0x85, 0x57, 0x7f, 0x00, 0x03, 0x00, 0xb0, 0xb3, 0xb7, 0xf4, 0x3b, 0xc7,
    0xb7, 0xf1, 0xd4, 0xdb, 0x62, 0x75, 0x63, 0x5c, 0x1b};
// What follows each question in GAME5 (yes/no whose yes is post-action 4).
const uint8_t kGame5UsaTail[] = {0x08, 0x02, 0x0A, 0x04, 0x01, 0x00};
const uint8_t kGame5JpnTail[] = {0x02, 0x08, 0x02, 0x0A, 0x04, 0x01, 0x00};
// GAME5.PRG loads at 0x0602B000; where each release keeps its first question.
const uint32_t kGame5Base = 0x0602B000;
const uint32_t kUsaQuestions = 0x0609E8BC;
const uint32_t kJpnQuestions = 0x0609EAB7;

// A room message's yes/no with no post-action (room message 1's own tail): the
// script reads the answer itself with `state_byte_test 5,0,0`.
const uint8_t kRoomYesNoTail[] = {0x08, 0x00, 0x01, 0x01};

const uint8_t kFlagOutfit1 = 0x70;  // 112, g_ScenarioFlags (SCD bank 0)
const uint8_t kFlagOutfit2 = 0x71;  // 113

// Area 5's room_action_set as the disc has it: handler 9, event 0 (byte 14).
const uint8_t kArea5Action[] = {0x0d, 0x05, 0xf0, 0x23, 0xa0, 0x0f, 0x10, 0x0e, 0x40,
                                0x06, 0x09, 0x81, 0x09, 0x00, 0x00, 0x00, 0x00, 0x00};
const size_t kArea5EventByte = 14;
// Event 0's yes-branch head: if; state_byte_test 5,0,0; state_byte_set 9,<model>.
const uint8_t kEvent0YesHead[] = {0x01, 0x12, 0x06, 0x05, 0x00, 0x00, 0x08, 0x09};

// Header pointer slots, RDT +0x48 + 4*n.
const int kHInit = 6, kHScd = 7, kHEvents = 8, kHAnimHeader = 9, kHMessages = 11,
          kHItemIcons = 12, kHEffIndex = 13, kHEffData = 14, kHEffSprite = 15,
          kHVabBody = 18;

void put(Bytes* b, std::initializer_list<uint8_t> v) { b->insert(b->end(), v); }
void put(Bytes* b, const Bytes& v) { b->insert(b->end(), v.begin(), v.end()); }
void put(Bytes* b, const uint8_t* p, size_t n) { b->insert(b->end(), p, p + n); }

void setU32(Bytes* d, size_t off, uint32_t v) {
    (*d)[off] = (uint8_t)v;
    (*d)[off + 1] = (uint8_t)(v >> 8);
    (*d)[off + 2] = (uint8_t)(v >> 16);
    (*d)[off + 3] = (uint8_t)(v >> 24);
}

void pad4(Bytes* b) {
    while (b->size() % 4) b->push_back(0);
}

// Event opcode 06: [06][len], one u16-sized SCD block (commands + block_end,
// padded to 4) and a u16 0 that ends the block list.
Bytes runScd(const Bytes& cmds) {
    Bytes body = cmds;
    body.push_back(0x00);
    const size_t size = (2 + body.size() + 3) & ~(size_t)3;
    Bytes out;
    put(&out, {0x06, (uint8_t)(2 + size + 2), (uint8_t)size, (uint8_t)(size >> 8)});
    put(&out, body);
    out.resize(2 + size, 0);
    put(&out, {0x00, 0x00});
    return out;
}

// cmd_if pushes (after its header) + L as the false target; cmd_else jumps
// (its own position) + L. Several conditions in one if are an AND.
Bytes ifElse(const Bytes& conds, const Bytes& then, const Bytes& other) {
    Bytes out;
    put(&out, {0x01, (uint8_t)(conds.size() + then.size() + 2)});
    put(&out, conds);
    put(&out, then);
    put(&out, {0x02, (uint8_t)(2 + other.size())});
    put(&out, other);
    return out;
}

Bytes bitTest(uint8_t flag, bool wantSet) { return {0x04, 0x00, flag, (uint8_t)(wantSet ? 0 : 1)}; }
Bytes bitOp(uint8_t flag, bool on) { return {0x05, 0x00, flag, (uint8_t)(on ? 0 : 1)}; }
Bytes cat(const Bytes& a, const Bytes& b) {
    Bytes o = a;
    put(&o, b);
    return o;
}

Bytes wardrobeEvent(uint8_t modelByte, const Bytes& wearing, const Bytes& wearOps,
                    uint8_t offerMsg, uint8_t backMsg) {
    const Bytes msgBitsOff = {0x05, 0x06, 0x1F, 0x01, 0x05, 0x06, 0x1D, 0x01, 0x05, 0x06, 0x19, 0x01};
    const Bytes msgBitsOn = {0x05, 0x06, 0x1F, 0x00, 0x05, 0x06, 0x1D, 0x00, 0x05, 0x06, 0x19, 0x00};
    const Bytes ask = cat(msgBitsOff, ifElse(wearing, {0x0B, backMsg, 0x45, 0x00},
                                             {0x0B, offerMsg, 0x45, 0x00}));
    Bytes yes = {0x08, 0x09, modelByte, 0x00};
    put(&yes, ifElse(wearing, cat(bitOp(kFlagOutfit1, false), bitOp(kFlagOutfit2, false)), wearOps));
    put(&yes, {0x14, 0x00, 0x09, 0x01});
    const Bytes apply = ifElse({0x06, 0x05, 0x00, 0x00}, yes, msgBitsOn);
    Bytes out = runScd(ask);
    // The disc's wait for the answer (event 0 +0x18): call `state_byte_test
    // 5,2,0x80` until it passes.
    put(&out, {0xFC, 0x06, 0x06, 0x05, 0x02, 0x80, 0xFE, 0xFD});
    put(&out, runScd(apply));
    put(&out, {0x00, 0xFF});
    return out;
}

struct QuestionSet {
    const uint8_t* text[3];
    size_t size[3];
};

QuestionSet questions(SaturnRegion region) {
    if (region == SaturnRegion::Jpn)
        return {{kSatJpnC0, kSatJpnC1, kSatJpnC2},
                {sizeof(kSatJpnC0), sizeof(kSatJpnC1), sizeof(kSatJpnC2)}};
    return {{kSatC0, kSatC1, kSatC2}, {sizeof(kSatC0), sizeof(kSatC1), sizeof(kSatC2)}};
}

// The three questions, each followed by its GAME5 tail, back to back from `at`.
bool game5HasQuestions(const Bytes& game5, uint32_t at, const QuestionSet& q,
                       const uint8_t* tail, size_t tailSize) {
    size_t off = at - kGame5Base;
    for (int i = 0; i < 3; ++i) {
        if (off + q.size[i] + tailSize > game5.size()) return false;
        if (std::memcmp(game5.data() + off, q.text[i], q.size[i]) != 0 ||
            std::memcmp(game5.data() + off + q.size[i], tail, tailSize) != 0)
            return false;
        off += q.size[i] + tailSize;
    }
    return true;
}

}  // namespace

bool saturnRegionFromGame5(const std::vector<uint8_t>& game5, SaturnRegion* out) {
    if (game5HasQuestions(game5, kUsaQuestions, questions(SaturnRegion::Usa),
                          kGame5UsaTail, sizeof(kGame5UsaTail))) {
        *out = SaturnRegion::Usa;
        return true;
    }
    if (game5HasQuestions(game5, kJpnQuestions, questions(SaturnRegion::Jpn),
                          kGame5JpnTail, sizeof(kGame5JpnTail))) {
        *out = SaturnRegion::Jpn;
        return true;
    }
    return false;
}

bool patchSaturnWardrobeRdt(std::vector<uint8_t>* rdtIn, SaturnRegion region,
                            std::string* why) {
    Bytes d = *rdtIn;
    const size_t n = d.size();
    if (n < 0x94) {
        *why = "too small for an RDT";
        return false;
    }
    uint32_t hdr[19];
    for (int i = 0; i < 19; ++i) hdr[i] = readU32(d.data(), n, 0x48 + 4 * i);
    for (int s : {kHInit, kHScd, kHEvents, kHAnimHeader, kHMessages, kHItemIcons, kHVabBody}) {
        if (hdr[s] == 0 || hdr[s] > n) {
            *why = "header slot " + std::to_string(s) + " out of range";
            return false;
        }
    }

    // init: area 5 -> event 3 (the DC's own edit to its arrange wardrobe)
    {
        const uint8_t* b = d.data() + hdr[kHInit];
        const uint8_t* e = d.data() + hdr[kHScd];
        const uint8_t* hit = std::search(b, e, kArea5Action, kArea5Action + sizeof(kArea5Action));
        if (hit == e || std::search(hit + 1, e, kArea5Action, kArea5Action + sizeof(kArea5Action)) != e) {
            *why = "init script: not exactly one handler-9 area-5 action";
            return false;
        }
        d[(size_t)(hit - d.data()) + kArea5EventByte] = 3;
    }

    // events: the disc's three scripts
    const uint32_t ev = hdr[kHEvents];
    std::vector<uint32_t> offs;
    for (;;) {
        if (ev + 4 * offs.size() + 4 > n) {
            *why = "event table runs off the file";
            return false;
        }
        const uint32_t o = readU32(d.data(), n, ev + 4 * offs.size());
        if (o == 0) break;
        offs.push_back(o);
    }
    if (offs.size() != 3) {
        *why = "expected the Saturn's 3 event scripts, found " + std::to_string(offs.size());
        return false;
    }
    std::vector<uint32_t> sorted = offs;
    sorted.push_back(hdr[kHAnimHeader] - ev);
    std::sort(sorted.begin(), sorted.end());
    std::vector<Bytes> scripts;
    for (uint32_t o : offs) {
        const uint32_t end = *std::upper_bound(sorted.begin(), sorted.end(), o);
        scripts.emplace_back(d.begin() + ev + o, d.begin() + ev + end);
    }
    const Bytes& s0 = scripts[0];
    const auto head = std::search(s0.begin(), s0.end(), kEvent0YesHead,
                                  kEvent0YesHead + sizeof(kEvent0YesHead));
    if (head == s0.end() || head + sizeof(kEvent0YesHead) >= s0.end()) {
        *why = "event 0: yes-branch not found";
        return false;
    }
    // 8 in Chris's room (variant 0), 9 in Jill's: the outfit bit | the character.
    const uint8_t modelByte = *(head + sizeof(kEvent0YesHead));
    if (modelByte != 8 && modelByte != 9) {
        *why = "event 0: model byte " + std::to_string(modelByte);
        return false;
    }
    const std::vector<Bytes> newScripts = {
        wardrobeEvent(modelByte, cat(bitTest(kFlagOutfit1, true), bitTest(kFlagOutfit2, true)),
                      cat(bitOp(kFlagOutfit1, true), bitOp(kFlagOutfit2, true)), 0x83, 0x84),
        scripts[1],
        scripts[2],
        wardrobeEvent(modelByte, cat(bitTest(kFlagOutfit1, true), bitTest(kFlagOutfit2, false)),
                      cat(bitOp(kFlagOutfit1, true), bitOp(kFlagOutfit2, false)), 0x85, 0x84),
    };

    // messages: the disc's three, then the Saturn's questions as 3-5
    const uint32_t mb = hdr[kHMessages];
    const int count = readU16(d.data(), n, mb) / 2;
    if (count != 3) {
        *why = "expected 3 room messages, found " + std::to_string(count);
        return false;
    }
    std::vector<uint32_t> bounds;
    for (int i = 0; i < count; ++i) bounds.push_back(readU16(d.data(), n, mb + 2 * i));
    bounds.push_back(hdr[kHItemIcons] - mb);
    std::vector<Bytes> msgs;
    for (int i = 0; i < count; ++i)
        msgs.emplace_back(d.begin() + mb + bounds[i], d.begin() + mb + bounds[i + 1]);
    const QuestionSet q = questions(region);
    for (int i = 0; i < 3; ++i) {
        Bytes m(q.text[i], q.text[i] + q.size[i]);
        put(&m, kRoomYesNoTail, sizeof(kRoomYesNoTail));
        msgs.push_back(m);
    }

    Bytes mblob;
    uint32_t pos = (uint32_t)(2 * msgs.size());
    for (const Bytes& m : msgs) {
        put(&mblob, {(uint8_t)pos, (uint8_t)(pos >> 8)});
        pos += (uint32_t)m.size();
    }
    for (const Bytes& m : msgs) put(&mblob, m);
    pad4(&mblob);

    Bytes eblob(4 * (newScripts.size() + 1), 0);
    pos = (uint32_t)eblob.size();
    for (size_t i = 0; i < newScripts.size(); ++i) {
        setU32(&eblob, 4 * i, pos);
        pos += (uint32_t)newScripts[i].size();
    }
    for (const Bytes& s : newScripts) put(&eblob, s);
    pad4(&eblob);

    // The blob goes in at the VAB body (+0x90), the last block LoadRoomRdt
    // keeps: past it the buffer is reused for the omodel/item slot records, so
    // nothing can be appended at the end. Every offset at or past it moves.
    const uint32_t x = hdr[kHVabBody];
    if (x % 4) {
        *why = "VAB body offset is not 4-aligned";
        return false;
    }
    const uint32_t grow = (uint32_t)(mblob.size() + eblob.size());
    auto mv = [&](uint32_t v) { return v >= x ? v + grow : v; };
    // Table positions come from the header as loaded; the moved values are
    // written back at the end.
    const uint32_t* at = hdr;
    for (int c = 0; c < d[1]; ++c)
        for (int k = 0; k < 2; ++k) {
            const size_t p = 0x94 + c * 0x2C + 4 * k;
            setU32(&d, p, mv(readU32(d.data(), n, p)));
        }
    // object_models / item_models: {TMD, TIM} pairs, 0 = empty.
    for (const auto& t : {std::make_pair(at[2], (int)d[2]), std::make_pair(at[3], (int)d[3])})
        for (int i = 0; i < t.second; ++i)
            for (int k = 0; k < 2; ++k) {
                const size_t p = t.first + 8 * i + 4 * k;
                const uint32_t v = readU32(d.data(), n, p);
                if (v) setU32(&d, p, mv(v));
            }
    // Effect sprites: RDT-relative offsets read backwards from +0x80 (one per
    // index byte until 0xFF) and from +0x84 (eight).
    if (at[kHEffData] >= 4 * 8) {
        for (int i = 0; i < 8 && d[at[kHEffIndex] + i] != 0xFF; ++i) {
            const size_t p = at[kHEffData] - 4 * i;
            setU32(&d, p, mv(readU32(d.data(), n, p)));
        }
    }
    if (at[kHEffSprite] >= 4 * 8) {
        for (int i = 0; i < 8; ++i) {
            const size_t p = at[kHEffSprite] - 4 * i;
            setU32(&d, p, mv(readU32(d.data(), n, p)));
        }
    }
    uint32_t moved[19];
    for (int i = 0; i < 19; ++i) moved[i] = hdr[i] ? mv(hdr[i]) : 0;
    moved[kHMessages] = x;
    moved[kHEvents] = x + (uint32_t)mblob.size();
    for (int i = 0; i < 19; ++i) setU32(&d, 0x48 + 4 * i, moved[i]);

    Bytes out(d.begin(), d.begin() + x);
    put(&out, mblob);
    put(&out, eblob);
    out.insert(out.end(), d.begin() + x, d.end());
    *rdtIn = std::move(out);
    return true;
}

}  // namespace re1
