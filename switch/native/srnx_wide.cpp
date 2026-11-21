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
#include <cstdlib>
#include <cstdio>

#include <cstdio>
#include <cstring>

extern "C" {
uint32_t bus_read_u32_slow(uint32_t addr);
void bus_write_u32_slow(uint32_t addr, uint32_t val);
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
    uint32_t lr[SRNX_MAX_SPR], lr_multi[SRNX_MAX_SPR];   // who drew each sprite (debug)
    uint32_t n = 0;
    uint32_t base = 0;
    int32_t cam_x = 0, cam_y = 0;   // the camera the game placed this frame's sprites by (24.8)
};
// main engine camera record (0x0236AAD8): the sprite routines subtract this
// camera's position, so a frame's records plus it give world positions
constexpr uint32_t kCamRec[2] = {0x0236AAD8u, 0x0236AB3Cu};
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
        const uint32_t slot = f.base + 8u * (f.s[i].slot & 0x7F);
        const uint32_t w0 = bus_read_u32_slow(slot), w1 = bus_read_u32_slow(slot + 4u);
        f.s[i].attr0 = (uint16_t)w0;
        f.s[i].attr1 = (uint16_t)(w0 >> 16);
        f.s[i].attr2 = (uint16_t)w1;
    }
    if (g_srnx_obj_dbg > 0 && e == 0) {
        --g_srnx_obj_dbg;
        std::fprintf(stderr, "[obj] frame cam %d %d\n", (int32_t)bus_read_u32_slow(kCamRec[0] + 4) >> 8, (int32_t)bus_read_u32_slow(kCamRec[0] + 8) >> 8);
    }
    if (g_srnx_sprlog > 0 && e == 0) {
        --g_srnx_sprlog;
        std::fprintf(stderr, "[sprlog] frame cam %d,%d n %u\n", f.cam_x >> 8, f.cam_y >> 8, f.n);
        for (uint32_t i = 0; i < f.n; ++i)
            std::fprintf(stderr, "[sprlog]  x %5d y %5d hud %d lr %08X multi %08X a2 %04X\n", f.s[i].x >> 12, f.s[i].y >> 12,
                         (f.s[i].slot >> 15) & 1, f.lr[i], f.lr_multi[i], f.s[i].attr2);
    }
    g_last[e] = f;
    f.n = 0;
    ++g_frames[e];
}
}  // namespace

extern "C" {
// HUD sprites: drawn screen-fixed by the HUD code (set from host measurements)
int srnx_is_hud_caller(int routine, uint32_t lr, uint32_t lr_multi) {
    // The stage overlay's HUD block (score, time, rings, tension gauge, lives)
    // draws through the multi-part routine from 0x02335F00..0x02337000; the
    // gauge's middle pieces come straight from routine 0 at lr 0x02032468.
    // Both measured screen-fixed over 500 frames of play.
    if (routine == 1 && lr == 0x02032D14u) return lr_multi >= 0x02335F00u && lr_multi < 0x02337000u;
    if (routine == 0) return lr == 0x02032468u;
    return 0;
}
int g_srnx_wide = 0;
// switch-test35: level select (port menu). armed: 1 = replace the next stage start
int g_srnx_lvl_zone = 0, g_srnx_lvl_act = 0, g_srnx_lvl_armed = 0;
int g_srnx_stage_started = 0;   // a stage start since the world map (srnx_code_frame)
// switch-test35: level select stage 3 = special stage. The game starts a
// special stage through the scene stub main 0x02055108 (ldr ip 0x0203051C,
// r0 = 1 (overlay 1), r1 = 0x0230C9F4, bx ip; reached from the act overlay's
// veneer at ov00 0x0234CFBC after a Special Generator launch). The entry
// 0x0230C9F4 takes the special stage number from the zone byte 0x022C457C,
// which the stage start (0x0205247C) has just written. srnx.hooks @020524BC
// calls that stub instead of the act start when this returns 1.
static int s_sp_pending = 0;
// switch-test37: what follows the special stage. Entered from an act, the
// game sets bit 0x40 of 0x022C4578 (ov00 0x02343EC8) and the special stage's
// end (ov01 0x0230BC70) then calls the scene stub 0x02055194 (overlay 0,
// 0x0204D7E0), which resumes the act at the place saved on the way in. The
// level select skips the act, so nothing was saved (host with the bit set:
// zone 2 act 1 came back with Sonic at 1,0, off the map) and without the bit
// nothing follows the result screen at all (test36: white screen). A special
// stage started from the level select goes back to the world map instead
// (srnx.hooks @ov01:0230BC84 calls the world map stub main 0x02054FD4, the
// one the act and the main menu use).
static int s_sp_from_lvl = 0;
extern "C" int srnx_special_end(void) {
    if (!s_sp_from_lvl) return 0;
    s_sp_from_lvl = 0;
    std::fprintf(stderr, "[srnx] special stage over: back to the world map\n");
    return 1;
}
extern "C" int srnx_special_take(void) {
    if (!s_sp_pending) return 0;
    s_sp_pending = 0;
    s_sp_from_lvl = 1;
    std::fprintf(stderr, "[srnx] level select: special stage %d\n", (int)(bus_read_u32_slow(0x022C457Cu) & 0xFFu) + 1);
    return 1;
}
extern "C" int srnx_lvl_take(uint32_t* zone, uint32_t* act) {
    g_srnx_stage_started = 1;
    std::fprintf(stderr, "[srnx] stage start: zone %u act %u%s\n", (unsigned)*zone + 1, (unsigned)*act + 1,
                 g_srnx_lvl_armed ? " (level select replaces it)" : "");
    s_sp_pending = 0;
    s_sp_from_lvl = 0;   // a special stage quit early leaves it set
    if (!g_srnx_lvl_armed) return 0;
    g_srnx_lvl_armed = 0;
    *zone = (uint32_t)g_srnx_lvl_zone;
    *act = (uint32_t)(g_srnx_lvl_act == 3 ? 0 : g_srnx_lvl_act);
    if (g_srnx_lvl_act == 3) s_sp_pending = 1;
    std::fprintf(stderr, "[srnx] level select: zone %d %s\n", g_srnx_lvl_zone + 1,
                 g_srnx_lvl_act == 3 ? "special stage" : g_srnx_lvl_act == 2 ? "boss" : g_srnx_lvl_act ? "act 2" : "act 1");
    return 1;
}
// switch-test36: robot homing in special stages. The game homes Sonic onto
// a robot when a new tap (touch flags bit 0x40, touch record 0x022B65B4)
// lands on one: ov01 0x02312258 (only while player+0x114 allows it) calls
// 0x02310174, which walks the robots, keeps the ones its own checks allow
// (flags, 0x02310248 distance window), projects each to the bottom screen and
// tests the tap against it (0x0230AE34); a hit sets player state 0x12. A press
// of A or B (no number prompt up) asks for a homing attack: for up to 12
// frames srnx.hooks reports a new tap and treats the first robot that passes
// the game's own checks as hit. srnx_home_done ends the request.
static int s_home_frames = 0;
extern "C" void srnx_home_request(void) {
    s_home_frames = 12;
    std::fprintf(stderr, "[srnx] homing attack requested\n");
}
extern "C" int srnx_home_pending(void) { return s_home_frames > 0; }
extern "C" void srnx_home_tick(void) { if (s_home_frames > 0) --s_home_frames; }
extern "C" void srnx_home_done(uint32_t robot) {
    if (s_home_frames > 0 && robot) {
        std::fprintf(stderr, "[srnx] homing attack on robot %08X\n", robot);
        s_home_frames = 0;
    }
}
int g_srnx_wide_l = 284, g_srnx_wide_r = 285, g_srnx_wide_t = 0, g_srnx_wide_b = 272;
uint32_t g_srnx_spr_overflow = 0;
int g_srnx_act_l = 0, g_srnx_act_r = 0, g_srnx_act_t = 0, g_srnx_act_b = 0;
int g_srnx_act_dbg = 0;
int g_srnx_keep_margin = 0;
unsigned long long g_srnx_send_calls[4], g_srnx_send_words[4];
void srnx_send_debug(uint32_t src, uint32_t dst, uint32_t bytes, uint32_t lr) {
    const int k = dst == 0x04000400u ? 0 : (dst >= 0x04000000u && dst < 0x05000000u ? 1 : 2);
    ++g_srnx_send_calls[k]; g_srnx_send_words[k] += bytes / 4;
    static int shown = 0;
    if (std::getenv("NR_SEND_DBG") && shown < 40) { ++shown; std::fprintf(stderr, "[send] src %08X dst %08X bytes %u lr %08X\n", src, dst, bytes, lr); }
}
int g_srnx_obj_dbg = 0;   // host: frames of object step/draw logging left
void srnx_obj_debug(int kind, uint32_t obj) {
    if (g_srnx_obj_dbg <= 0) return;
    std::fprintf(stderr, "[obj] %c %08X x %d y %d f54 %08X f5c %04X cb70 %08X\n", kind ? 'D' : 'S', obj,
                 (int32_t)bus_read_u32_slow(obj + 0x10) >> 8, (int32_t)bus_read_u32_slow(obj + 0x14) >> 8,
                 bus_read_u32_slow(obj + 0x54), bus_read_u32_slow(obj + 0x5c) & 0xFFFF, bus_read_u32_slow(obj + 0x70));
}
void srnx_act_debug(uint32_t sp, uint32_t r0, uint32_t r1) {
    if (g_srnx_act_dbg <= 0) return;
    --g_srnx_act_dbg;
    std::fprintf(stderr, "[act] rect x0 %d y0 %d x1 %d (r0 %d r1 %d) ext l%d r%d t%d b%d wide %d\n",
                 (int32_t)bus_read_u32_slow(sp + 0x20), (int32_t)bus_read_u32_slow(sp + 0x24), (int32_t)bus_read_u32_slow(sp + 0x28),
                 (int32_t)r0, (int32_t)r1, g_srnx_act_l, g_srnx_act_r, g_srnx_act_t, g_srnx_act_b, g_srnx_wide);
}
int g_srnx_hide_hud = 0;
static int g_ctx_hud = 0;
static uint32_t g_ctx_lr = 0, g_ctx_lr_multi = 0;
int g_srnx_sprlog = 0;          // host: print this many frames of records with their callers
void srnx_spr_ctx(int hud) { g_ctx_hud = hud; }
// routine 0 = 0x02032480, 1 = 0x02032D2C, 2 = 0x02032C8C (multi-part, calls 1)
void srnx_spr_enter(int routine, uint32_t lr) {
    if (routine == 2) { g_ctx_lr_multi = lr; return; }
    g_ctx_lr = lr;
    if (routine == 1 && lr != 0x02032D14u) g_ctx_lr_multi = 0;   // called directly, not via 0x02032C8C
    if (routine == 0) g_ctx_lr_multi = 0;
    g_ctx_hud = srnx_is_hud_caller(routine, lr, g_ctx_lr_multi);
}

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
    if (!f.n) {
        f.cam_x = (int32_t)bus_read_u32_slow(kCamRec[e] + 4u);
        f.cam_y = (int32_t)bus_read_u32_slow(kCamRec[e] + 8u);
    }
    f.base = base;
    if (f.n >= SRNX_MAX_SPR) { ++g_srnx_spr_overflow; return y; }
    f.lr[f.n] = g_ctx_lr;
    f.lr_multi[f.n] = g_ctx_lr_multi;
    SrnxSprite& s = f.s[f.n++];
    s.slot = (uint16_t)(idx | (g_ctx_hud ? 0x8000 : 0));
    s.x = (int32_t)x;
    s.y = (int32_t)y;
    if (e == 0 && g_srnx_wide && g_ctx_hud && g_srnx_hide_hud) return 192u << 12;
    return (e == 0 && g_srnx_wide && clip == 0) ? (192u << 12) : y;
}

uint32_t srnx_spr_last(int engine, const SrnxSprite** out) {
    *out = g_last[engine & 1].s;
    return g_last[engine & 1].n;
}

void srnx_spr_last_cam(int engine, int32_t* x, int32_t* y) {
    *x = g_last[engine & 1].cam_x;
    *y = g_last[engine & 1].cam_y;
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
