// Sonic Rush widescreen: sprite hooks in the game's central sprite routine.
//
// The game draws every object sprite through one routine (main program,
// around 0x020329F0). It clips each sprite to a hard-coded 256x192 box and
// packs the position into OAM's 9-bit x and 8-bit y, which cannot describe
// anything outside one DS screen. Two hooks (tools/nrecomp/hooks/srnx.hooks):
//
//   0x020329F4  the clip test: with a wide view active, test against the wide
//               view instead (camera-relative, in the main engine's terms).
//   0x02032B40  right after the slot allocator returns: record the sprite's
//               true position (20.12, camera-relative) against its OAM slot.
//
// At the next frame's first allocation the finished frame's records are
// completed with the slot's three OAM attributes and kept as the last whole
// frame, which our wide renderer draws at true positions.
#include "srnx_wide.h"

#include <cstdio>
#include <cstring>

extern "C" {
uint32_t bus_read_u32_slow(uint32_t addr);
}

namespace {
// Per-engine sprite blocks (pointer table read by the allocator 0x02036BFC):
// main 0x02089AD4, sub 0x020885AC. +0x000 is the sorted OAM shadow copied to
// hardware; +0x800 is the staging array the allocator hands out (count at
// +0x89C, at most 128); entries are linked into 32 priority lists and sorted
// into the shadow later, so a staging index is not the final OAM index.
constexpr uint32_t kStage[2] = {0x0208A2D4u, 0x02088DACu};

struct Frame {
    SrnxSprite s[SRNX_MAX_SPR];
    uint32_t n = 0;
    uint32_t base = 0;
};
Frame g_cur[2], g_last[2];
int g_prev_idx[2] = {1 << 30, 1 << 30};
uint32_t g_mgr[2] = {0, 0};
bool g_mgr_known[2] = {false, false};
uint32_t g_frames[2] = {0, 0};

int engine_of_slot(uint32_t slot, int* idx, uint32_t* base) {
    for (int e = 0; e < 2; ++e)
        if (slot >= kStage[e] && slot < kStage[e] + 0x400u) {
            *idx = (int)((slot - kStage[e]) >> 3);
            *base = kStage[e];
            return e;
        }
    return -1;
}

void finish(int e) {
    Frame& f = g_cur[e];
    for (uint32_t i = 0; i < f.n; ++i) {
        const uint32_t slot = f.base + 8u * f.s[i].slot;
        const uint32_t w0 = bus_read_u32_slow(slot), w1 = bus_read_u32_slow(slot + 4u);
        f.s[i].attr0 = (uint16_t)w0;
        f.s[i].attr1 = (uint16_t)(w0 >> 16);
        f.s[i].attr2 = (uint16_t)w1;
    }
    g_last[e] = f;
    f.n = 0;
    ++g_frames[e];
}
}  // namespace

extern "C" {
int g_srnx_wide = 0;
int g_srnx_wide_l = 284, g_srnx_wide_r = 285, g_srnx_wide_t = 0, g_srnx_wide_b = 272;
uint32_t g_srnx_spr_overflow = 0;

// Set by a clip hook for the sprite about to be allocated: whether it lies on
// the DS screen. A sprite kept only for the wide view is pushed off the DS
// screen in OAM (y = 192) so the DS picture stays clean.
static int g_last_clip = -1;   // -1 no clip ran, 0 off the DS screen, 1 on it

int srnx_spr_clip(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t mgr) {
    g_last_clip = -1;
    if (!g_srnx_wide || !g_mgr_known[0] || mgr != g_mgr[0]) return -1;
    const int64_t sx = (int32_t)x, sy = (int32_t)y;
    const bool vis = sx + ((int64_t)w << 12) > -((int64_t)g_srnx_wide_l << 12) &&
                     sx < ((int64_t)(256 + g_srnx_wide_r) << 12) &&
                     sy + ((int64_t)h << 12) > -((int64_t)g_srnx_wide_t << 12) &&
                     sy < ((int64_t)(192 + g_srnx_wide_b) << 12);
    g_last_clip = (sx + ((int64_t)w << 12) > 0 && sx < (256ll << 12) &&
                   sy + ((int64_t)h << 12) > 0 && sy < (192ll << 12)) ? 1 : 0;
    return vis ? 1 : 0;
}

int srnx_spr_box(int32_t x0, int32_t x1, int32_t y0, int32_t y1, uint32_t mgr) {
    if (!g_srnx_wide || !g_mgr_known[0] || mgr != g_mgr[0]) return -1;
    // the per-part clip (srnx_spr_clip) decides the DS-screen question for each part
    return (x1 > -g_srnx_wide_l && x0 < 256 + g_srnx_wide_r &&
            y1 > -g_srnx_wide_t && y0 < 192 + g_srnx_wide_b) ? 1 : 0;
}

uint32_t srnx_spr_record(uint32_t slot, uint32_t x, uint32_t y, uint32_t mgr) {
    int idx = 0;
    uint32_t base = 0;
    const int e = engine_of_slot(slot, &idx, &base);
    const int clip = g_last_clip;
    g_last_clip = -1;
    if (e < 0) return y;
    g_mgr[e] = mgr;
    g_mgr_known[e] = true;
    if ((idx <= g_prev_idx[e] || base != g_cur[e].base) && g_cur[e].n) finish(e);
    g_prev_idx[e] = idx;
    Frame& f = g_cur[e];
    f.base = base;
    if (f.n >= SRNX_MAX_SPR) { ++g_srnx_spr_overflow; return y; }
    SrnxSprite& s = f.s[f.n++];
    s.slot = (uint16_t)idx;
    s.x = (int32_t)x;
    s.y = (int32_t)y;
    return (e == 0 && g_srnx_wide && clip == 0) ? (192u << 12) : y;
}

uint32_t srnx_spr_last(int engine, const SrnxSprite** out) {
    *out = g_last[engine & 1].s;
    return g_last[engine & 1].n;
}

uint32_t srnx_spr_frames(int engine) { return g_frames[engine & 1]; }
uint32_t srnx_spr_mgr(int engine) { return g_mgr[engine & 1]; }

static uint32_t g_ac_lr[32], g_ac_n[32], g_ac_mgr[32];
void srnx_alloc_count(uint32_t lr, uint32_t mgr) {
    for (int i = 0; i < 32; ++i) {
        if (g_ac_lr[i] == lr) { ++g_ac_n[i]; return; }
        if (!g_ac_lr[i]) { g_ac_lr[i] = lr; g_ac_n[i] = 1; g_ac_mgr[i] = mgr; return; }
    }
}
void srnx_alloc_report(void) {
    for (int i = 0; i < 32 && g_ac_lr[i]; ++i)
        std::fprintf(stderr, "[srnx] alloc lr %08X n %u mgr %08X\n", g_ac_lr[i], g_ac_n[i], g_ac_mgr[i]);
}
}
