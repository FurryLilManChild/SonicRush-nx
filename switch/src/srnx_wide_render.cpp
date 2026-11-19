// SonicRush-nx: our own stage view.
//
// The DS game frames each screen with its own camera and moves Sonic between
// the two screens. This view ignores that split: our camera follows Sonic
// directly (classic 2D Sonic framing, zoomable) and the picture is built from
// world data, so it does not matter which DS screen the game currently has
// him on.
//
//   - BG1, far background: the main engine's latched scroll, moved by our
//     view's offset from the main engine's camera (the game's parallax comes
//     with the main camera's own movement).
//   - BG2/BG3, level layers: straight from the level layouts in RAM (camera
//     block +0x130 block data, +0x134 / +0x138 layouts: u16 width, u16 height
//     in 64 px blocks, then u16 block indices; a block is 8x8 tile entries).
//   - Sprites: the native sprite records (switch/native/srnx_wide.cpp),
//     placed by the main engine's camera, so they line up whichever screen
//     the game draws them for. HUD sprites are pinned to the view's corners.
//   - The main engine's screen as the DS drew it (Sonic's 3D layer, effects),
//     with its HUD left out, pasted at its own world position: its BG and
//     sprites are the same data as ours, so it lines up.
//
// Facts this relies on (host checks, docs/widescreen-notes.md):
//   camera record 0 (0x0236AAD8) is the main engine's, the one with the 3D
//   layer, and it follows Sonic; record 1 is the other screen, 272 px above or
//   below. The main engine is on the bottom LCD while record 0 is the lower.
//   [0x0236AC60] points at the camera's target, Sonic: +0x10 x, +0x14 y (24.8).
#include "srnx_wide_render.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "../native/nr_runtime.h"
#include "../native/srnx_wide.h"

uint32_t nds_gpu2d_read(uint32_t addr, uint32_t width);
const uint8_t* nds_vram_oam_live();
extern "C" uint32_t bus_read_u32_slow(uint32_t addr);
void nds_gpu2d_debug_regs(int e, uint16_t bgx[4], uint16_t bgy[4], int32_t refx[2],
                          int32_t refy[2], int16_t pabcd[8]);
uint32_t nds_vram_read_bg_extpal(int engine, uint32_t addr, uint32_t width);
const uint8_t* nds_vram_renderer_palette(int engine);
struct NdsVramRendererView {
    const uint8_t* bg[32];
    const uint8_t* obj[16];
};
const NdsVramRendererView* nds_vram_renderer_view(int engine);
uint32_t nds_vram_read_bg(int engine, uint32_t addr, uint32_t width);
uint32_t nds_vram_read_obj(int engine, uint32_t addr, uint32_t width);
const uint32_t* nds_gpu2d_srnx_3d_frame(uint32_t* frames);
// switch-test13: the 3D layer drawn for the whole view by our own projection
// (deko renderer, Switch only; weak so the host build links without it).
bool nx_deko_stage_image(const uint32_t** px, int* w, int* h, int* rx0, int* ry0, int* rw, int* rh, int* wx, int* wy) __attribute__((weak));
void nx_deko_srnx_force_scale(int scale) __attribute__((weak));
#include "srnx_remap.h"
int g_srnx_own3d = 1;
int g_srnx_act_start = 0;     // switch-test19: set on entering a stage (presenter opens the title card window)
int g_srnx_card_bottom = 0;   // the main engine is on the bottom DS screen this frame
int g_srnx_autocap = 0;          // switch-test15: 1 = Sonic without 3D, 2 = sudden mismatch (frontend saves a capture)
unsigned g_srnx_cam_swaps = 0;   // camera-record swaps seen (log)
static int g_rect_x, g_rect_y, g_rect_w = 0, g_rect_h = 0;

int g_srnx_zoom = 0;
int g_srnx_act_extra = 0, g_srnx_act_off = 0;   // host experiments
int g_srnx_cam_lag = 2;   // host-checked: the DS screen just drawn matches the camera two VBlanks back


namespace {
constexpr uint32_t kOamShadow = 0x0208A2ECu;  // engine A OAM shadow (DMA source), host-verified
constexpr uint32_t kCam = 0x0236AAD8u;      // camera records: main engine +0, other +0x64
constexpr uint32_t kTargetPtr = 0x0236AC60u;
constexpr int kGap = 272;

inline bool in_ram(uint32_t a) { return a >= 0x02000000u && a < 0x023FF000u; }

// ---- our camera -----------------------------------------------------------
struct Camera {
    bool valid = false;
    uint32_t level = 0;          // layout pointer: a new level snaps the camera
    float cx = 0, cy = 0;        // view centre, world pixels
    float look_x = 0, look_y = 0;
    float px = 0, py = 0;        // target last frame
} g_cam;

void camera_update(float tx, float ty, int vw, int vh, int lw_px, int lh_px, uint32_t level) {
    const float vx = tx - g_cam.px, vy = ty - g_cam.py;
    if (!g_cam.valid || level != g_cam.level || std::fabs(vx) > 160 || std::fabs(vy) > 160) {
        g_cam = Camera{};
        g_cam.valid = true;
        g_cam.level = level;
        g_cam.cx = tx;
        g_cam.cy = ty - vh * 0.08f;
        g_cam.px = tx;
        g_cam.py = ty;
    } else {
        g_cam.px = tx;
        g_cam.py = ty;
        // look ahead in the direction of travel, eased so it never jerks
        const float want_lx = std::clamp(vx * 9.0f, -vw * 0.20f, vw * 0.20f);
        const float want_ly = std::clamp(vy * 7.0f, -vh * 0.22f, vh * 0.22f);
        g_cam.look_x += (want_lx - g_cam.look_x) * 0.06f;
        g_cam.look_y += (want_ly - g_cam.look_y) * 0.08f;
        // horizontal: a small window around the centre, then follow, capped
        const float dx = tx + g_cam.look_x - g_cam.cx;
        constexpr float kWinX = 8.0f, kMaxStep = 32.0f;
        if (dx > kWinX) g_cam.cx += std::min(dx - kWinX, kMaxStep);
        else if (dx < -kWinX) g_cam.cx += std::max(dx + kWinX, -kMaxStep);
        // vertical: a dead zone (no bobbing on slopes and small hops), then
        // follow, capped; settle back to centre while Sonic is steady
        const float want_y = ty - vh * 0.08f + g_cam.look_y;
        const float dy = want_y - g_cam.cy;
        const float zone = vh * 0.12f;
        if (dy > zone) g_cam.cy += std::min(dy - zone, kMaxStep);
        else if (dy < -zone) g_cam.cy += std::max(dy + zone, -kMaxStep);
        else if (std::fabs(vy) < 1.0f) g_cam.cy += dy * 0.08f;
    }
    // stay inside the level
    const float hx = vw * 0.5f, hy = vh * 0.5f;
    if (lw_px > vw) g_cam.cx = std::clamp(g_cam.cx, hx, lw_px - hx);
    if (lh_px > vh) g_cam.cy = std::clamp(g_cam.cy, hy, lh_px - hy);
}

// ---- snapshot helpers ------------------------------------------------------
void copy_vram(uint8_t* dst, const uint8_t* const* pages, int npages, bool /*obj*/) {
    // a page with no direct mapping has no VRAM bank behind it in a stage
    // (host: every such page read back as zeros, 20 of 20 checks), so it is
    // zero here instead of 4096 slow reads per page per frame
    for (int i = 0; i < npages; ++i) {
        uint8_t* d = dst + i * 0x4000;
        if (pages[i]) std::memcpy(d, pages[i], 0x4000);
        else std::memset(d, 0, 0x4000);
    }
}

void copy_ram(uint8_t* dst, uint32_t src, uint32_t bytes) {
    // a 4 KiB page at a time straight from the guest's page table; word reads
    // only for a page without a direct mapping
    uint32_t a = 0;
    while (a < bytes) {
        const uint32_t addr = src + a;
        const uint32_t n = std::min<uint32_t>(bytes - a, 0x1000u - (addr & 0xFFFu));
        const uintptr_t b = nr_rd_tab[addr >> 12];
        if (b) std::memcpy(dst + a, (const void*)(b + (addr & 0xFFFu)), n);
        else
            for (uint32_t k = 0; k < n; k += 4) {
                const uint32_t w = NR_RD32(addr + k);
                std::memcpy(dst + a + k, &w, std::min<uint32_t>(4, n - k));
            }
        a += n;
    }
}

uint32_t g_rgb_lut[32768];
bool g_rgb_ready = false;
uint32_t rgb(uint16_t c) {
    if (!g_rgb_ready) {
        for (uint32_t i = 0; i < 32768; ++i) {
            // as the DS 2D engine outputs it: 5-bit to 6-bit (x2), then 6-bit to 8-bit
            auto ex = [](uint32_t c5) { const uint32_t c6 = c5 * 2; return (c6 << 2) | (c6 >> 4); };
            const uint32_t r = ex(i & 31), g = ex((i >> 5) & 31), b = ex((i >> 10) & 31);
            g_rgb_lut[i] = 0xFF000000u | (r << 16) | (g << 8) | b;
        }
        g_rgb_ready = true;
    }
    return g_rgb_lut[c & 0x7FFF];
}

// The game's camera as latched at each VBlank start, newest first. The frame
// the DS has just drawn was framed by an earlier frame's camera
// (g_srnx_cam_lag picks which; host-checked).
struct Latch { int cam0x, cam0y, cam1y; float tx, ty; bool ok; uint16_t bg1x, bg1y; };
Latch g_latch[4];

// cache of the level's block count (largest index in either layout + 1)
uint32_t g_blk_level = 0, g_blk_count = 0;
}  // namespace

void srnx_wide_vblank() {
    for (int i = 3; i > 0; --i) g_latch[i] = g_latch[i - 1];
    Latch& l = g_latch[0];
    l.cam0x = (int32_t)NR_RD32(kCam + 4) >> 8;
    l.cam0y = (int32_t)NR_RD32(kCam + 8) >> 8;
    l.cam1y = (int32_t)NR_RD32(kCam + 0x64 + 8) >> 8;
    const uint32_t tgt = NR_RD32(kTargetPtr);
    l.ok = in_ram(tgt);
    l.tx = l.ok ? (float)((int32_t)NR_RD32(tgt + 0x10) >> 8) : 0.f;
    l.ty = l.ok ? (float)((int32_t)NR_RD32(tgt + 0x14) >> 8) : 0.f;
    {   // switch-test17: the far background's scroll, latched with the camera
        uint16_t bx[4], by[4]; int32_t rx[2], ry[2]; int16_t pm[8];
        nds_gpu2d_debug_regs(0, bx, by, rx, ry, pm);
        l.bg1x = bx[1]; l.bg1y = by[1];
    }
    // switch-test14: the 3D geometry finished this frame was built for the
    // camera now in record 0; tag it so it is placed by level position (a
    // screen swap moves the DS screen, not the level).
    g_srnx_remap_cur.wx = l.cam0x;
    g_srnx_remap_cur.wy = l.cam0y;
}

// overlay 0 hook after the camera records swap (0x0233B600): geometry built
// from here on is for the other DS screen
extern "C" void srnx_wide_cam_swapped() {
    if (!g_rect_w) return;
    const int c0x = (int32_t)NR_RD32(kCam + 4) >> 8;
    const int c0y = (int32_t)NR_RD32(kCam + 8) >> 8;
    if (g_srnx_remap_cur.on) {
        srnx_remap_set(g_srnx_remap_cur, g_rect_x - c0x, g_rect_y - c0y, g_rect_w, g_rect_h);
        g_srnx_remap_cur.wx = c0x; g_srnx_remap_cur.wy = c0y;
    }
    if (g_srnx_remap_next.on) srnx_remap_set(g_srnx_remap_next, g_rect_x - c0x, g_rect_y - c0y, g_rect_w, g_rect_h);
    ++g_srnx_cam_swaps;
}

// switch-test18: the game uploads this frame's sprite pictures after the
// snapshot (at VBlank); copy sprite memory and palettes again one frame later
// so each sprite is drawn with the pictures of its own frame.
int g_srnx_obj_late = 0;   // switch-test19: off; rpl-005 showed the late copy one frame ahead of the records
void srnx_wide_refresh_obj(const SrnxWideParams& p, uint8_t* data) {
    const NdsVramRendererView* vv = nds_vram_renderer_view(0);
    const uint8_t* pal = nds_vram_renderer_palette(0);
    if (!vv || !pal) return;
    copy_vram(data + p.off_obj, vv->obj, 16, true);
    std::memcpy(data + p.off_objpal, pal + 512, 512);
}

void srnx_wide_inactive() {
    g_srnx_remap_next.on = 0;
    if (nx_deko_srnx_force_scale) nx_deko_srnx_force_scale(-1);
}

bool srnx_wide_stage_active() {
    const uint32_t dc = nds_gpu2d_read(0x04000000u, 4);
    if ((dc & 7) != 5 || (dc & 0x0C00) != 0x0C00 || !(dc & 8)) return false;
    const uint32_t blk = NR_RD32(kCam + 0x130), l0 = NR_RD32(kCam + 0x134), l1 = NR_RD32(kCam + 0x138);
    if (!in_ram(blk) || !in_ram(l0) || !in_ram(l1)) return false;
    for (uint32_t L : {l0, l1}) {
        const uint32_t w = NR_RD16(L), h = NR_RD16(L + 2);
        if (!w || !h || w * h * 2u + 4u > kSrnxLayMax) return false;
    }
    if ((int16_t)NR_RD16(kCam + 0x1E) != 256) return false;
    if (!in_ram(NR_RD32(kTargetPtr))) return false;
    return true;
}

void srnx_wide_align_main(SrnxWideParams& p, const uint8_t* data, const uint32_t* ds);

#ifdef SRNX_HOST_PROFILE
#include <chrono>
double g_snap_part[8];
static std::chrono::steady_clock::time_point g_snap_t;
#define SNAP_T(i) do { auto _n = std::chrono::steady_clock::now(); if (i) g_snap_part[i] += std::chrono::duration<double, std::milli>(_n - g_snap_t).count(); g_snap_t = _n; } while (0)
#else
#define SNAP_T(i) do {} while (0)
#endif
bool srnx_wide_snapshot(SrnxWideParams& p, uint8_t* data, const uint32_t* top, const uint32_t* bottom) {
    SNAP_T(0);
    std::memset(&p, 0, sizeof(p));
    const SrnxZoom z = kSrnxZooms[std::clamp(g_srnx_zoom, 0, 3)];
    const int VW = z.w, VH = z.h;

    const uint32_t blk = NR_RD32(kCam + 0x130);
    const uint32_t lay[2] = {NR_RD32(kCam + 0x134), NR_RD32(kCam + 0x138)};
    const Latch& L = g_latch[std::clamp(g_srnx_cam_lag, 0, 3)];
    if (!L.ok) return false;
    const int cam0x = L.cam0x, cam0y = L.cam0y, cam1y = L.cam1y;
    const float tx = L.tx, ty = L.ty;
    const int lw = NR_RD16(lay[0]), lh = NR_RD16(lay[0] + 2);
    camera_update(tx, ty, VW, VH, lw * 64, lh * 64, lay[0]);
    const int wx = (int)std::lround(g_cam.cx - VW * 0.5f), wy = (int)std::lround(g_cam.cy - VH * 0.5f);

    // sprite clip for the next frame: our view around the main camera's screen
    constexpr int kMargin = 64;
    g_srnx_wide_l = std::max(0, cam0x - wx) + kMargin;
    g_srnx_wide_r = std::max(0, wx + VW - (cam0x + 256)) + kMargin;
    g_srnx_wide_t = std::max(0, cam0y - wy) + kMargin;
    g_srnx_wide_b = std::max(0, wy + VH - (cam0y + 192)) + kMargin;
    // object activation (hook in overlay 0): the same area, with room to spare
    // so objects come alive before they scroll into view
    g_srnx_act_l = g_srnx_act_off ? 0 : g_srnx_wide_l + 32 + g_srnx_act_extra;
    g_srnx_act_r = g_srnx_act_off ? 0 : g_srnx_wide_r + 32 + g_srnx_act_extra;
    g_srnx_act_t = g_srnx_act_off ? 0 : g_srnx_wide_t + 32 + g_srnx_act_extra;
    g_srnx_act_b = g_srnx_act_off ? 0 : g_srnx_wide_b + 32 + g_srnx_act_extra;
    g_srnx_keep_margin = std::max(std::max(g_srnx_act_l, g_srnx_act_r), std::max(g_srnx_act_t, g_srnx_act_b));

    p.view_w = VW;
    p.view_h = VH;
    p.world_x = wx;
    p.world_y = wy;
    p.main_x = cam0x - wx;
    p.main_y = cam0y - wy;
    p.dispcnt = nds_gpu2d_read(0x04000000u, 4);
    for (int i = 0; i < 4; ++i) p.bgcnt[i] = nds_gpu2d_read(0x04000008u + 2u * i, 2) & 0xFFFF;
    // BG1..BG3 back to front: higher priority number first; ties, higher BG first
    int ord[3] = {1, 2, 3};
    std::sort(ord, ord + 3, [&](int a, int b) {
        const int pa = p.bgcnt[a] & 3, pb = p.bgcnt[b] & 3;
        return pa != pb ? pa > pb : a > b;
    });
    p.order[0] = ord[0]; p.order[1] = ord[1]; p.order[2] = ord[2]; p.order[3] = 4;
    uint16_t bgx[4], bgy[4]; int32_t rx[2], ry[2]; int16_t pm[8];
    nds_gpu2d_debug_regs(0, bgx, bgy, rx, ry, pm);
    // switch-test17: scroll and camera from the same latch, and across a
    // screen swap hold the last offset if it jumps (the game moves the far
    // background and the camera on different frames there)
    (void)bgx; (void)bgy;
    {
        static int prev_ox = 0, prev_oy = 0, hold = 0;
        static unsigned last_swaps = 0;
        int ox = (int)L.bg1x - cam0x, oy = (int)L.bg1y - cam0y;
        if (g_srnx_cam_swaps != last_swaps) { last_swaps = g_srnx_cam_swaps; hold = 4; }
        if (hold > 0) {
            --hold;
            auto wrapd = [](int d) { d %= 512; if (d > 256) d -= 512; if (d < -256) d += 512; return d; };
            if (std::abs(wrapd(ox - prev_ox)) > 24 || std::abs(wrapd(oy - prev_oy)) > 24) { ox = prev_ox; oy = prev_oy; }
        }
        prev_ox = ox; prev_oy = oy;
        p.bg1_sx = ox + wx;
        p.bg1_sy = oy + wy;
        bgy[1] = (uint16_t)(oy + cam0y);
    }
    {   // The DS screen shows rows bgy..bgy+191 of the far background. When
        // that fits inside the picture without wrapping, the rows our taller
        // view adds above or below are not part of the picture: repeat the
        // edge tile row (sky above) instead of wrapping to the other side.
        const int lh = (p.bgcnt[1] & 0x8000) ? 512 : 256;
        const int dsy = bgy[1] & 511;
        p.bg1_clamp = (dsy + 192 <= lh) ? 1 : 0;
    }
    p.bright = nds_gpu2d_read(0x0400006Cu, 4) & 0xFFFF;

    // data block
    uint32_t off = 0;
    auto take = [&](uint32_t bytes) { const uint32_t o = off; off += (bytes + 255u) & ~255u; return o; };
    p.off_bg = take(kSrnxBgVram);
    p.off_obj = take(kSrnxObjVram);
    p.off_bgpal = take(512);
    p.off_objpal = take(512);
    p.off_xpal = take(2 * 8192);
    p.off_lay0 = take(kSrnxLayMax);
    p.off_lay1 = take(kSrnxLayMax);
    p.off_blk = take(kSrnxBlkMax);
    p.off_spr = take(kSrnxSprMax * 16 + 512);
    p.off_main = take(256 * 192 * 4);
    p.off_v3d = take(1024 * 512 * 4);
    if (off > kSrnxDataBytes) return false;

    {   // rotation/scale groups from engine A's OAM, and the blend registers
        uint8_t* af = data + p.off_spr + kSrnxAffOff;
        // switch-test20: engine A's OAM as the 2D engine holds it. The bus
        // read used since test16 returned 0 for every group (rpl-002 from
        // test19: 120 frames, all zero), so every rotated sprite drew with a
        // zero matrix: the boost blocks.
        const uint8_t* oam = nds_vram_oam_live();
        for (uint32_t g = 0; g < 32; ++g)
            for (uint32_t k = 0; k < 4; ++k)
                std::memcpy(af + (g * 4 + k) * 2, oam + g * 32u + k * 8u + 6u, 2);
        const uint32_t bc = nds_gpu2d_read(0x04000050u, 2) & 0xFFFF, ba = nds_gpu2d_read(0x04000052u, 2) & 0xFFFF;
        std::memcpy(af + 256, &bc, 4);
        std::memcpy(af + 260, &ba, 4);
        // switch-test18 diagnostic: two candidate rotation tables in the game's
        // own memory (16-byte records, PA PB PC PD first), 8 groups each, for
        // the replay to compare against the DS picture
        for (uint32_t t = 0; t < 2; ++t)
            for (uint32_t g = 0; g < 8; ++g)
                for (uint32_t k = 0; k < 4; ++k) {
                    const uint16_t v = NR_RD16((t ? 0x02089254u : 0x02087D2Cu) + g * 16u + k * 2u);
                    std::memcpy(af + 272 + (t * 8 + g) * 8 + k * 2, &v, 2);
                }
    }
    const NdsVramRendererView* vv = nds_vram_renderer_view(0);
    const uint8_t* pal = nds_vram_renderer_palette(0);
    if (!vv || !pal) return false;
    SNAP_T(6);
    copy_vram(data + p.off_bg, vv->bg, 32, false);
    copy_vram(data + p.off_obj, vv->obj, 16, true);
    std::memcpy(data + p.off_bgpal, pal, 512);
    std::memcpy(data + p.off_objpal, pal + 512, 512);
    SNAP_T(1);
    if ((p.dispcnt >> 30) & 1)
        for (uint32_t s = 0; s < 2; ++s)
            for (uint32_t a = 0; a < 8192; a += 4) {
                const uint32_t w = nds_vram_read_bg_extpal(0, (2u + s) * 0x2000u + a, 4);
                std::memcpy(data + p.off_xpal + s * 8192 + a, &w, 4);
            }
    p.backdrop = rgb((uint16_t)(pal[0] | (pal[1] << 8)));

    // layouts and block data
    SNAP_T(2);
    if (lay[0] != g_blk_level) {
        uint32_t mx = 0;
        for (uint32_t L : lay) {
            const uint32_t n = NR_RD16(L) * NR_RD16(L + 2);
            for (uint32_t i = 0; i < n; ++i) mx = std::max<uint32_t>(mx, NR_RD16(L + 4 + 2 * i));
        }
        g_blk_level = lay[0];
        g_blk_count = std::min<uint32_t>(mx + 1, kSrnxBlkMax / 128);
    }
    for (int k = 0; k < 2; ++k) {
        p.layout_w[k] = NR_RD16(lay[k]);
        p.layout_h[k] = NR_RD16(lay[k] + 2);
        copy_ram(data + (k ? p.off_lay1 : p.off_lay0), lay[k] + 4, (p.layout_w[k] * p.layout_h[k] * 2u + 3u) & ~3u);
    }
    copy_ram(data + p.off_blk, blk, g_blk_count * 128u);

    SNAP_T(3);
    // sprites: world ones by the main camera, then HUD pinned to the corners
    const SrnxSprite* sp = nullptr;
    const uint32_t n = srnx_spr_last(0, &sp);
    int32_t scx = 0, scy = 0;
    srnx_spr_last_cam(0, &scx, &scy);
    const int spr_ox = (scx >> 8) - wx, spr_oy = (scy >> 8) - wy;
    SrnxWideSpr* out = reinterpret_cast<SrnxWideSpr*>(data + p.off_spr);
    uint32_t nw = 0, nh = 0;
    for (int pass = 0; pass < 2; ++pass)
        for (uint32_t i = 0; i < n && nw + nh < kSrnxSprMax; ++i) {
            const bool hud = sp[i].slot & 0x8000;
            if (hud != (pass == 1)) continue;
            SrnxWideSpr& o = out[nw + nh];
            int x = sp[i].x >> 12, y = sp[i].y >> 12;
            if (hud) {
                // Sonic Rush's HUD is all on the left: keep every piece where
                // the game put it, and only the lives counter (bottom of the
                // screen) moves to the view's bottom edge. (switch-test8 moved
                // right-half pieces right, which split the score and time.)
                // The tension gauge's lowest piece (x 1, y 170) stays with the
                // gauge; only the lives counter (x 20/32, y 176) moves.
                if (y >= 172) y += VH - 192;   // lives icon and count (icon x varies)
            } else {
                x += spr_ox;
                y += spr_oy;
            }
            o.x = x;
            o.y = y;
            o.a01 = sp[i].attr0 | ((uint32_t)sp[i].attr1 << 16);
            o.a2 = sp[i].attr2;
            (hud ? nh : nw)++;
        }
    p.nspr = nw;
    p.nhud = nh;
    p.hud_dx = VW - 256;
    p.hud_dy = VH - 192;

    // the main engine's screen: bottom LCD while camera 0 is the lower one
    // Only the 3D layer comes from the DS side (the 2D engine keeps each
    // frame's 3D lines); level and sprites are all ours, so nothing else is
    // stitched. The 3D frame is placed by the DS screen it went into, which
    // the latched camera puts within a pixel or so; the DS screen's level
    // pixels settle the exact spot.
    const uint32_t* mainscr = (cam0y > cam1y) ? bottom : top;
    g_srnx_card_bottom = cam0y > cam1y ? 1 : 0;
    const uint32_t* f3d = nds_gpu2d_srnx_3d_frame(nullptr);
    p.main_valid = (mainscr && f3d && (p.dispcnt & 0x100)) ? 1u : 0u;
    p.v3d_valid = 0;
    // Our own 3D: ask the 3D engine to draw the next frames for a rectangle
    // around the view (in the DS screen's pixel space, margin for camera
    // movement before they are shown), at the render size that covers it.
    const bool own3d = g_srnx_own3d && nx_deko_stage_image && nx_deko_srnx_force_scale;
    if (own3d) {
        static const int kW[5] = {256, 384, 512, 768, 1024}, kH[5] = {192, 288, 384, 576, 768};
        int sc = 4, m = 48;
        for (int i = 1; i <= 4; ++i) {
            const int mm = std::min({48, (kW[i] - VW) / 2, (kH[i] - VH) / 2});
            if (mm >= 24) { sc = i; m = mm; break; }
        }
        nx_deko_srnx_force_scale(sc);
        // the rectangle in level coordinates; turned into DS-screen pixels
        // against the camera the geometry is built with (now, and again at
        // a screen swap: srnx_wide_cam_swapped)
        g_rect_x = wx - m; g_rect_y = wy - m; g_rect_w = VW + 2 * m; g_rect_h = VH + 2 * m;
        const int c0x = (int32_t)NR_RD32(kCam + 4) >> 8;
    const int c0y = (int32_t)NR_RD32(kCam + 8) >> 8;
        srnx_remap_set(g_srnx_remap_next, g_rect_x - c0x, g_rect_y - c0y, g_rect_w, g_rect_h);
        const uint32_t* px; int w, h, rx0, ry0, rw, rh, fwx, fwy;
        if (p.main_valid && nx_deko_stage_image(&px, &w, &h, &rx0, &ry0, &rw, &rh, &fwx, &fwy)) {
            // view pixel (x, y) is level (world_x + x, world_y + y), which is
            // pixel (world - fw) of the DS screen this 3D frame was built for
            uint32_t* o = reinterpret_cast<uint32_t*>(data + p.off_v3d);
            const int64_t sx = ((int64_t)w << 16) / rw, sy = ((int64_t)h << 16) / rh;
            for (int y = 0; y < VH; ++y) {
                const int64_t hy = ((int64_t)(y + p.world_y - fwy - ry0) * sy + sy / 2) >> 16;
                uint32_t* row = o + (size_t)y * VW;
                if (hy < 0 || hy >= h) { std::memset(row, 0, (size_t)VW * 4); continue; }
                const uint32_t* src = px + hy * w;
                int64_t fx = (int64_t)(p.world_x - fwx - rx0) * sx + sx / 2;
                for (int x = 0; x < VW; ++x, fx += sx) {
                    const int64_t hx = fx >> 16;
                    row[x] = (hx >= 0 && hx < w) ? src[hx] : 0u;
                }
            }
            p.v3d_valid = 1;
        }
        SNAP_T(4);
    } else {
        g_srnx_remap_next.on = 0;
        if (nx_deko_srnx_force_scale) nx_deko_srnx_force_scale(-1);
    }
    if (p.main_valid && !own3d) {
        std::memcpy(data + p.off_main, f3d, 256 * 192 * 4);
        SNAP_T(4);
        srnx_wide_align_main(p, data, mainscr);
        SNAP_T(5);
    }
    SNAP_T(7);
    if (false) {
    }
    (void)kGap;
    return true;
}

// ---- drawing (the shader WideStage.glsl mirrors this) ----------------------
namespace {
struct Src {
    const SrnxWideParams& p;
    const uint8_t* d;
    uint8_t u8(uint32_t o) const { return d[o]; }
    uint16_t u16(uint32_t o) const { uint16_t v; std::memcpy(&v, d + o, 2); return v; }
    uint32_t u32(uint32_t o) const { uint32_t v; std::memcpy(&v, d + o, 4); return v; }
};

const int kSz[3][4][2] = {{{8, 8}, {16, 16}, {32, 32}, {64, 64}},
                          {{16, 8}, {32, 8}, {32, 16}, {64, 32}},
                          {{8, 16}, {8, 32}, {16, 32}, {32, 64}}};

// one BG's colour at view pixel (x, y); false = transparent
static inline int bg1_row(const SrnxWideParams& p, int y, int lh) {
    int r = y + p.bg1_sy;
    if (p.bg1_clamp) {
        if (r < 0) return 0;          // the top row is flat sky; a tile row is not
        if (r >= lh) return lh - 1;
        return r;
    }
    return ((r % lh) + lh) % lh;
}

bool bg_pixel(const Src& s, int bg, int x, int y, uint16_t& col) {
    const SrnxWideParams& p = s.p;
    const uint32_t c = p.bgcnt[bg];
    const uint32_t char_off = ((p.dispcnt >> 24) & 7) * 0x10000u, map_off = ((p.dispcnt >> 27) & 7) * 0x10000u;
    const uint32_t chrbase = char_off + ((c >> 2) & 15) * 0x4000u;
    if (bg == 1) {
        const int size = (c >> 14) & 3;
        const int lw = (size & 1) ? 512 : 256, lh = (size & 2) ? 512 : 256;
        const int xx = (((x + p.bg1_sx) % lw) + lw) % lw, yy = bg1_row(p, y, lh);
        const int tx = xx >> 3, ty = yy >> 3;
        uint32_t sb = 0;
        if (lw == 512 && tx >= 32) sb += 1;
        if (lh == 512 && ty >= 32) sb += (lw == 512) ? 2 : 1;
        const uint32_t mapbase = map_off + ((c >> 8) & 31) * 0x800u;
        const uint16_t ent = s.u16(p.off_bg + ((mapbase + sb * 0x800u + (uint32_t)(((ty & 31) * 32 + (tx & 31)) * 2)) & (kSrnxBgVram - 1)));
        int px = xx & 7, py = yy & 7;
        if (ent & 0x400) px = 7 - px;
        if (ent & 0x800) py = 7 - py;
        const uint32_t t = ent & 0x3FF;
        uint32_t v;
        if (c & 0x80) v = s.u8(p.off_bg + ((chrbase + t * 64 + py * 8 + px) & (kSrnxBgVram - 1)));
        else {
            const uint8_t b = s.u8(p.off_bg + ((chrbase + t * 32 + py * 4 + px / 2) & (kSrnxBgVram - 1)));
            v = (px & 1) ? (b >> 4) : (b & 15);
        }
        if (!v) return false;
        col = s.u16(p.off_bgpal + 2 * ((c & 0x80) ? v : ((ent >> 12) * 16 + v)));
        return true;
    }
    const int k = bg - 2;
    const int wxp = p.world_x + x, wyp = p.world_y + y;
    if (wxp < 0 || wyp < 0) return false;
    const int bx = wxp >> 6, by = wyp >> 6;
    if (bx >= (int)p.layout_w[k] || by >= (int)p.layout_h[k]) return false;
    const uint32_t b = s.u16((k ? p.off_lay1 : p.off_lay0) + 2u * (uint32_t)(by * (int)p.layout_w[k] + bx));
    if (b * 128u >= kSrnxBlkMax) return false;
    const int tx = (wxp & 63) >> 3, ty = (wyp & 63) >> 3;
    const uint16_t ent = s.u16(p.off_blk + b * 128u + (uint32_t)((ty * 8 + tx) * 2));
    int px = wxp & 7, py = wyp & 7;
    if (ent & 0x400) px = 7 - px;
    if (ent & 0x800) py = 7 - py;
    const uint32_t v = s.u8(p.off_bg + ((chrbase + (ent & 0x3FF) * 64u + py * 8 + px) & (kSrnxBgVram - 1)));
    if (!v) return false;
    col = ((p.dispcnt >> 30) & 1) ? s.u16(p.off_xpal + k * 8192u + 2u * ((ent >> 12) * 256u + v))
                                  : s.u16(p.off_bgpal + 2 * v);
    return true;
}

// sprite colour at view pixel; prio out; false = none
// A rotation/scale matrix that shrinks or magnifies by more than 4x is taken
// as stale or wrong data: one texel stretched over the whole box drew solid
// blocks in the boost effect (rpl-004).
bool aff_ok(int pa, int pb, int pc, int pd) {
    const int n1 = pa * pa + pc * pc, n2 = pb * pb + pd * pd;
    return n1 >= 64 * 64 && n1 <= 1024 * 1024 && n2 >= 64 * 64 && n2 <= 1024 * 1024;
}

bool spr_pixel(const Src& s, uint32_t first, uint32_t count, int x, int y, uint16_t& col, int& prio) {
    const SrnxWideParams& p = s.p;
    const bool one_d = p.dispcnt & 0x10;
    const uint32_t bound = 32u << ((p.dispcnt >> 20) & 3);
    bool hit = false;
    for (uint32_t i = first; i < first + count; ++i) {
        const uint32_t o = p.off_spr + i * 16;
        const int sx = (int32_t)s.u32(o), sy = (int32_t)s.u32(o + 4);
        const uint32_t a01 = s.u32(o + 8), a2 = s.u32(o + 12);
        const uint32_t a0 = a01 & 0xFFFF, a1 = a01 >> 16;
        const int pr = (a2 >> 10) & 3;
        if (hit && pr >= prio) continue;
        const bool rs = i < p.nspr && ((a0 >> 8) & 1);   // HUD pieces: no rotation/scale
        if (!((a0 >> 8) & 1) && ((a0 >> 9) & 1)) continue;
        if (((a0 >> 10) & 3) == 3) continue;
        const int shape = (a0 >> 14) & 3;
        if (shape == 3) continue;
        const int w = kSz[shape][(a1 >> 14) & 3][0], h = kSz[shape][(a1 >> 14) & 3][1];
        const bool dbl = rs && ((a0 >> 9) & 1);
        const int bw = dbl ? 2 * w : w, bh = dbl ? 2 * h : h;
        int xx = x - sx, yy = y - sy;
        if (xx < 0 || yy < 0 || xx >= bw || yy >= bh) continue;
        if (rs) {
            const int16_t* af = reinterpret_cast<const int16_t*>(s.d + p.off_spr + kSrnxAffOff) + ((a1 >> 9) & 31) * 4;
            const int cx = xx - bw / 2, cy = yy - bh / 2;
            int pa = af[0], pb = af[1], pc = af[2], pd = af[3];
            if (!aff_ok(pa, pb, pc, pd)) { pa = pd = 256; pb = pc = 0; }
            xx = ((pa * cx + pb * cy) >> 8) + w / 2;
            yy = ((pc * cx + pd * cy) >> 8) + h / 2;
            if (xx < 0 || yy < 0 || xx >= w || yy >= h) continue;
        }
        if (!rs && (a1 & 0x1000)) xx = w - 1 - xx;
        if (!rs && (a1 & 0x2000)) yy = h - 1 - yy;
        const bool bpp8 = a0 & 0x2000;
        const uint32_t tile = a2 & 0x3FF;
        const uint32_t tx = (uint32_t)xx >> 3, ty = (uint32_t)yy >> 3;
        uint32_t to;
        if (one_d) to = tile * bound + (ty * (uint32_t)(w / 8) + tx) * (bpp8 ? 64u : 32u);
        else to = (tile + ty * 32u + tx * (bpp8 ? 2u : 1u)) * 32u;
        uint32_t v;
        if (bpp8) v = s.u8(p.off_obj + ((to + (yy & 7) * 8 + (xx & 7)) & (kSrnxObjVram - 1)));
        else {
            const uint8_t b = s.u8(p.off_obj + ((to + (yy & 7) * 4 + (xx & 7) / 2) & (kSrnxObjVram - 1)));
            v = (xx & 1) ? (b >> 4) : (b & 15);
        }
        if (!v) continue;
        col = (uint16_t)(s.u16(p.off_objpal + 2 * (bpp8 ? v : ((a2 >> 12) * 16 + v))) & 0x7FFF) | ((((a0 >> 10) & 3) == 1) ? 0x8000 : 0);
        prio = pr;
        hit = true;
    }
    return hit;
}

uint32_t blend_obj(uint32_t a, uint32_t b, uint32_t eva, uint32_t evb) {
    auto ch = [&](int sh) { return std::min<uint32_t>(255, ((((a >> sh) & 255) * eva) + (((b >> sh) & 255) * evb)) / 16) << sh; };
    return 0xFF000000u | ch(16) | ch(8) | ch(0);
}

uint32_t brighten(uint32_t c, uint32_t mb) {
    const uint32_t mode = (mb >> 14) & 3, f = std::min<uint32_t>(mb & 31, 16);
    if (mode != 1 && mode != 2) return c;
    uint32_t r = (c >> 16) & 255, g = (c >> 8) & 255, b = c & 255;
    if (mode == 1) { r += (255 - r) * f / 16; g += (255 - g) * f / 16; b += (255 - b) * f / 16; }
    else { r -= r * f / 16; g -= g * f / 16; b -= b * f / 16; }
    return 0xFF000000u | (r << 16) | (g << 8) | b;
}
}  // namespace

// The latched camera frames the DS screen to within a pixel or so; settle the
// exact spot by matching our level layers against the DS screen on a sparse
// grid of points (sprites and the 3D layer only add noise to every offset).
void srnx_wide_align_main(SrnxWideParams& p, const uint8_t* data, const uint32_t* ds_screen) {
    const Src s{p, data};
    // around last frame's correction first (it rarely changes); the full
    // +-3 search only when the best of those is poor
    static int last_dx = 0, last_dy = 0;
    int best = -1, bdx = 0, bdy = 0;
    auto search = [&](int dx0, int dx1, int dy0, int dy1, int step) {
    for (int dy = dy0; dy <= dy1; ++dy)
        for (int dx = dx0; dx <= dx1; ++dx) {
            int score = 0;
            for (int gy = 4; gy < 192; gy += step)
                for (int gx = 4; gx < 256; gx += step) {
                    const int x = p.main_x + dx + gx, y = p.main_y + dy + gy;
                    uint16_t col = 0;
                    bool have = false;
                    for (int i = 2; i >= 0 && !have; --i) {
                        const int bg = (int)p.order[i];
                        if (bg > 3 || !((p.dispcnt >> (8 + bg)) & 1)) continue;
                        have = bg_pixel(s, bg, x, y, col);
                    }
                    const uint32_t ours = have ? rgb(col) : p.backdrop;
                    const uint32_t ds = ds_screen[gy * 256 + gx];
                    score += ((ours ^ ds) & 0xFFFFFF) == 0;
                }
            if (score > best || (score == best && dx == last_dx && dy == last_dy)) { best = score; bdx = dx; bdy = dy; }
        }
    };
    search(last_dx - 1, last_dx + 1, last_dy - 1, last_dy + 1, 16);      // 9 offsets x 192 points
    if (best < 192 / 2) { best = -1; search(-3, 3, -3, 3, 16); }           // poor match: look wider
    last_dx = bdx;
    last_dy = bdy;
    p.main_x += bdx;
    p.main_y += bdy;
}

// One view pixel: the frontmost of our BG layers, our sprites and the DS 3D
// layer by DS rules (sprite above a BG of the same priority, BG0 = 3D above
// higher-numbered BGs); a partly transparent 3D edge blends with what is
// below it; master brightness over all; HUD on top.
uint32_t srnx_wide_pixel(const Src& s, int x, int y) {
    const SrnxWideParams& p = s.p;
    // BGs, front to back
    uint16_t bcol = 0;
    int bprio = 4, bkey = 99;
    for (int i = 2; i >= 0; --i) {
        const int bg = (int)p.order[i];
        if (bg > 3 || !((p.dispcnt >> (8 + bg)) & 1)) continue;
        uint16_t cc;
        if (bg_pixel(s, bg, x, y, cc)) { bcol = cc; bprio = p.bgcnt[bg] & 3; bkey = bprio * 8 + 2 + bg; break; }
    }
    uint32_t below = bkey < 99 ? rgb(bcol) : p.backdrop;
    int below_key = bkey;
    // sprites
    uint16_t sc; int sp = 4;
    if (((p.dispcnt >> 12) & 1) && spr_pixel(s, 0, p.nspr, x, y, sc, sp) && sp * 8 + 1 < below_key) {
        if (sc & 0x8000) {
            const uint32_t ba = s.u32(p.off_spr + kSrnxAffOff + 260);
            below = blend_obj(rgb(sc & 0x7FFF), below, std::min<uint32_t>(ba & 31, 16), std::min<uint32_t>((ba >> 8) & 31, 16));
        } else below = rgb(sc);
        below_key = sp * 8 + 1;
    }
    uint32_t c = below;
    // the 3D layer (BG0)
    const int mx = x - p.main_x, my = y - p.main_y;
    if (p.main_valid && (p.v3d_valid || (mx >= 0 && my >= 0 && mx < 256 && my < 192))) {
        const uint32_t t = p.v3d_valid ? s.u32(p.off_v3d + 4u * (uint32_t)(y * p.view_w + x))
                                       : s.u32(p.off_main + 4u * (uint32_t)(my * 256 + mx));
        const uint32_t a = (t >> 24) & 31;
        const int key3 = (int)(p.bgcnt[0] & 3) * 8 + 2;
        if (a && key3 < below_key) {
            auto e6 = [](uint32_t v) { return (v << 2) | (v >> 4); };
            const uint32_t r = e6(t & 63), g = e6((t >> 8) & 63), b = e6((t >> 16) & 63);
            if (a >= 31) c = 0xFF000000u | (r << 16) | (g << 8) | b;
            else {
                const uint32_t ia = 31 - a;
                const uint32_t br = (below >> 16) & 255, bgc = (below >> 8) & 255, bb = below & 255;
                c = 0xFF000000u | (((r * a + br * ia) / 31) << 16) | (((g * a + bgc * ia) / 31) << 8) | ((b * a + bb * ia) / 31);
            }
        }
    }
    c = brighten(c, p.bright);
    uint16_t hc; int hp;
    if (p.nhud && spr_pixel(s, p.nspr, p.nhud, x, y, hc, hp)) c = brighten(rgb(hc), p.bright);
    return c;
}

// Reference: one pixel at a time (slow; host checks only).
void srnx_wide_draw_ref(const SrnxWideParams& p, const uint8_t* data, uint32_t* out, int pitch) {
    const Src s{p, data};
    for (int y = 0; y < p.view_h; ++y)
        for (int x = 0; x < p.view_w; ++x)
            out[y * pitch + x] = srnx_wide_pixel(s, x, y);
}

// The same picture, drawn the fast way: BG layers back to front in tile
// spans (one layout and map lookup per 8 pixels), sprites one by one, the 3D
// layer only inside its window. Matches srnx_wide_draw_ref pixel for pixel
// (host-checked): same winner rules (frontmost BG; lowest-priority sprite,
// earliest on ties; sprite over a BG of equal priority; 3D by its key).
namespace {
uint8_t g_key[kSrnxWideMaxW * kSrnxWideMaxH];       // winner key of the BG/sprite pass
uint8_t g_sp_pr[kSrnxWideMaxW * kSrnxWideMaxH];     // sprite pass: priority (4 = none)
uint16_t g_sp_col[kSrnxWideMaxW * kSrnxWideMaxH];

// sprites [first, first+count) into g_sp_pr / g_sp_col (lowest priority wins,
// earliest on ties: drawn last to first, a pixel taken when priority <= held)
void draw_sprites(const Src& s, uint32_t first, uint32_t count, int W, int H) {
    const SrnxWideParams& p = s.p;
    std::memset(g_sp_pr, 4, (size_t)W * H);
    const bool one_d = p.dispcnt & 0x10;
    const uint32_t bound = 32u << ((p.dispcnt >> 20) & 3);
    for (int i = (int)(first + count) - 1; i >= (int)first; --i) {
        const uint32_t o = p.off_spr + (uint32_t)i * 16u;
        const int sx = (int32_t)s.u32(o), sy = (int32_t)s.u32(o + 4);
        const uint32_t a01 = s.u32(o + 8), a2 = s.u32(o + 12);
        const uint32_t a0 = a01 & 0xFFFF, a1 = a01 >> 16;
        const bool hud = (uint32_t)i >= p.nspr;   // HUD pieces: drawn without rotation/scale
        const bool rs = !hud && ((a0 >> 8) & 1);
        if (!((a0 >> 8) & 1) && ((a0 >> 9) & 1)) continue;
        if (((a0 >> 10) & 3) == 3) continue;
        const int shape = (a0 >> 14) & 3;
        if (shape == 3) continue;
        const int w = kSz[shape][(a1 >> 14) & 3][0], h = kSz[shape][(a1 >> 14) & 3][1];
        // rotation/scale sprites: box doubles with the double-size bit
        const bool dbl = rs && ((a0 >> 9) & 1);
        const int bw = dbl ? 2 * w : w, bh = dbl ? 2 * h : h;
        if (sx >= W || sy >= H || sx + bw <= 0 || sy + bh <= 0) continue;
        const uint8_t pr = (a2 >> 10) & 3;
        const uint16_t semi = (((a0 >> 10) & 3) == 1) ? 0x8000 : 0;
        if (rs) {
            const int16_t* af = reinterpret_cast<const int16_t*>(s.d + p.off_spr + kSrnxAffOff) + ((a1 >> 9) & 31) * 4;
            int pa = af[0], pb = af[1], pc = af[2], pd = af[3];
            if (!aff_ok(pa, pb, pc, pd)) { pa = pd = 256; pb = pc = 0; }   // switch-test19: no blown-up blocks
            const bool bpp8r = a0 & 0x2000;
            const uint32_t tile = a2 & 0x3FF, palbase = bpp8r ? 0 : (a2 >> 12) * 16;
            for (int yy = std::max(0, -sy); yy < bh && sy + yy < H; ++yy) {
                const int cy = yy - bh / 2;
                uint8_t* prow = g_sp_pr + (size_t)(sy + yy) * W + sx;
                uint16_t* crow = g_sp_col + (size_t)(sy + yy) * W + sx;
                for (int xx = std::max(0, -sx); xx < bw && sx + xx < W; ++xx) {
                    if (pr > prow[xx]) continue;
                    const int cx = xx - bw / 2;
                    const int px = ((pa * cx + pb * cy) >> 8) + w / 2, py = ((pc * cx + pd * cy) >> 8) + h / 2;
                    if (px < 0 || py < 0 || px >= w || py >= h) continue;
                    const uint32_t tx = (uint32_t)px >> 3, ty = (uint32_t)py >> 3;
                    const uint32_t to = one_d ? tile * bound + (ty * (uint32_t)(w / 8) + tx) * (bpp8r ? 64u : 32u)
                                              : (tile + ty * 32u + tx * (bpp8r ? 2u : 1u)) * 32u;
                    uint32_t v;
                    if (bpp8r) v = s.u8(p.off_obj + ((to + (py & 7) * 8 + (px & 7)) & (kSrnxObjVram - 1)));
                    else {
                        const uint8_t b = s.u8(p.off_obj + ((to + (py & 7) * 4 + (px & 7) / 2) & (kSrnxObjVram - 1)));
                        v = (px & 1) ? (b >> 4) : (b & 15);
                    }
                    if (!v) continue;
                    prow[xx] = pr;
                    crow[xx] = (uint16_t)(s.u16(p.off_objpal + 2 * (palbase + v)) & 0x7FFF) | semi;
                }
            }
            continue;
        }
        const bool bpp8 = a0 & 0x2000;
        const uint32_t tile = a2 & 0x3FF, palbase = bpp8 ? 0 : (a2 >> 12) * 16;
        const bool hf = !rs && (a1 & 0x1000), vf = !rs && (a1 & 0x2000);
        const int x0 = std::max(0, -sx), x1 = std::min(w, W - sx);
        for (int yy = std::max(0, -sy); yy < h && sy + yy < H; ++yy) {
            const int py = vf ? h - 1 - yy : yy;
            const uint32_t ty = (uint32_t)py >> 3;
            uint8_t* prow = g_sp_pr + (size_t)(sy + yy) * W + sx;
            uint16_t* crow = g_sp_col + (size_t)(sy + yy) * W + sx;
            for (int xx = x0; xx < x1; ++xx) {
                if (pr > prow[xx]) continue;
                const int px = hf ? w - 1 - xx : xx;
                const uint32_t tx = (uint32_t)px >> 3;
                const uint32_t to = one_d ? tile * bound + (ty * (uint32_t)(w / 8) + tx) * (bpp8 ? 64u : 32u)
                                          : (tile + ty * 32u + tx * (bpp8 ? 2u : 1u)) * 32u;
                uint32_t v;
                if (bpp8) v = s.u8(p.off_obj + ((to + (py & 7) * 8 + (px & 7)) & (kSrnxObjVram - 1)));
                else {
                    const uint8_t b = s.u8(p.off_obj + ((to + (py & 7) * 4 + (px & 7) / 2) & (kSrnxObjVram - 1)));
                    v = (px & 1) ? (b >> 4) : (b & 15);
                }
                if (!v) continue;
                prow[xx] = pr;
                crow[xx] = (uint16_t)(s.u16(p.off_objpal + 2 * (palbase + v)) & 0x7FFF) | semi;
            }
        }
    }
}
}  // namespace

void srnx_wide_draw_cpu(const SrnxWideParams& p, const uint8_t* data, uint32_t* out, int pitch) {
    const Src s{p, data};
    const int W = p.view_w, H = p.view_h;
    if (W <= 0 || H <= 0 || W > kSrnxWideMaxW || H > kSrnxWideMaxH) return;
    rgb(0);                                                  // LUT ready
    const uint32_t* lut = g_rgb_lut;
    for (int y = 0; y < H; ++y) {
        uint32_t* o = out + (size_t)y * pitch;
        for (int x = 0; x < W; ++x) o[x] = p.backdrop;
    }
    std::memset(g_key, 99, (size_t)W * H);

    const uint32_t char_off = ((p.dispcnt >> 24) & 7) * 0x10000u, map_off = ((p.dispcnt >> 27) & 7) * 0x10000u;
    const bool ext = (p.dispcnt >> 30) & 1;
    const uint8_t* vram = data + p.off_bg;
    constexpr uint32_t M = kSrnxBgVram - 1;
    // BGs back to front: order[0] first, the frontmost (order[2]) last
    for (int oi = 0; oi < 3; ++oi) {
        const int bg = (int)p.order[oi];
        if (bg > 3 || !((p.dispcnt >> (8 + bg)) & 1)) continue;
        const uint32_t c = p.bgcnt[bg];
        const uint8_t key = (uint8_t)((c & 3) * 8 + 2 + bg);
        const uint32_t chrbase = char_off + ((c >> 2) & 15) * 0x4000u;
        if (bg == 1) {
            const int size = (c >> 14) & 3;
            const int lw = (size & 1) ? 512 : 256, lh = (size & 2) ? 512 : 256;
            const uint32_t mapbase = map_off + ((c >> 8) & 31) * 0x800u;
            const bool b8 = c & 0x80;
            for (int y = 0; y < H; ++y) {
                const int yy = bg1_row(p, y, lh);
                const int ty = yy >> 3, py0 = yy & 7;
                uint32_t* o = out + (size_t)y * pitch;
                uint8_t* k = g_key + (size_t)y * W;
                for (int x = 0; x < W;) {
                    const int xx = (x + p.bg1_sx) & (lw - 1);
                    const int tx = xx >> 3;
                    const int span = std::min(8 - (xx & 7), W - x);
                    uint32_t sb = 0;
                    if (lw == 512 && tx >= 32) sb += 1;
                    if (lh == 512 && ty >= 32) sb += (lw == 512) ? 2 : 1;
                    const uint16_t ent = s.u16(p.off_bg + ((mapbase + sb * 0x800u + (uint32_t)(((ty & 31) * 32 + (tx & 31)) * 2)) & M));
                    const uint32_t t = ent & 0x3FF;
                    const int py = (ent & 0x800) ? 7 - py0 : py0;
                    const uint32_t pal = (ent >> 12) * 16;
                    for (int j = 0; j < span; ++j, ++x) {
                        int px = (xx + j) & 7;
                        if (ent & 0x400) px = 7 - px;
                        uint32_t v;
                        if (b8) v = vram[(chrbase + t * 64 + py * 8 + px) & M];
                        else { const uint8_t b = vram[(chrbase + t * 32 + py * 4 + px / 2) & M]; v = (px & 1) ? (b >> 4) : (b & 15); }
                        if (!v) continue;
                        o[x] = lut[s.u16(p.off_bgpal + 2 * (b8 ? v : pal + v)) & 0x7FFF];
                        k[x] = key;
                    }
                }
            }
            continue;
        }
        const int kk = bg - 2;
        const uint32_t lay = kk ? p.off_lay1 : p.off_lay0;
        const int lw = (int)p.layout_w[kk], lh = (int)p.layout_h[kk];
        const uint32_t xp = p.off_xpal + (uint32_t)kk * 8192u;
        for (int y = 0; y < H; ++y) {
            const int wy = p.world_y + y;
            if (wy < 0 || (wy >> 6) >= lh) continue;
            const int by = wy >> 6, ty = (wy & 63) >> 3, py0 = wy & 7;
            uint32_t* o = out + (size_t)y * pitch;
            uint8_t* k = g_key + (size_t)y * W;
            for (int x = 0; x < W;) {
                const int wx = p.world_x + x;
                const int span = std::min(8 - (wx & 7), W - x);
                if (wx < 0 || (wx >> 6) >= lw) { x += span; continue; }
                const uint32_t b = s.u16(lay + 2u * (uint32_t)(by * lw + (wx >> 6)));
                if (b * 128u >= kSrnxBlkMax) { x += span; continue; }
                const uint16_t ent = s.u16(p.off_blk + b * 128u + (uint32_t)((ty * 8 + ((wx & 63) >> 3)) * 2));
                const int py = (ent & 0x800) ? 7 - py0 : py0;
                const uint32_t tb = chrbase + (ent & 0x3FF) * 64u + (uint32_t)py * 8;
                const uint32_t pb = (ent >> 12) * 256u;
                for (int j = 0; j < span; ++j, ++x) {
                    int px = (wx + j) & 7;
                    if (ent & 0x400) px = 7 - px;
                    const uint32_t v = vram[(tb + px) & M];
                    if (!v) continue;
                    o[x] = lut[(ext ? s.u16(xp + 2u * (pb + v)) : s.u16(p.off_bgpal + 2 * v)) & 0x7FFF];
                    k[x] = key;
                }
            }
        }
    }
    // world sprites over BGs of the same or a lower priority; semi-transparent
    // ones blend with what is below by BLDALPHA
    uint32_t bldalpha;
    std::memcpy(&bldalpha, data + p.off_spr + kSrnxAffOff + 260, 4);
    const uint32_t eva = std::min<uint32_t>(bldalpha & 31, 16), evb = std::min<uint32_t>((bldalpha >> 8) & 31, 16);
    if (((p.dispcnt >> 12) & 1) && p.nspr) {
        draw_sprites(s, 0, p.nspr, W, H);
        for (int y = 0; y < H; ++y) {
            uint32_t* o = out + (size_t)y * pitch;
            uint8_t* k = g_key + (size_t)y * W;
            const uint8_t* pr = g_sp_pr + (size_t)y * W;
            const uint16_t* cc = g_sp_col + (size_t)y * W;
            for (int x = 0; x < W; ++x)
                if (pr[x] < 4 && pr[x] * 8 + 1 < k[x]) {
                    o[x] = (cc[x] & 0x8000) ? blend_obj(lut[cc[x] & 0x7FFF], o[x], eva, evb) : lut[cc[x] & 0x7FFF];
                    k[x] = (uint8_t)(pr[x] * 8 + 1);
                }
        }
    }
    // the 3D layer, inside its window
    if (p.main_valid) {
        const int key3 = (int)(p.bgcnt[0] & 3) * 8 + 2;
        const bool v3 = p.v3d_valid != 0;   // our own view-size 3D layer
        const int y0 = v3 ? 0 : std::max(0, p.main_y), y1 = v3 ? H : std::min(H, p.main_y + 192);
        const int x0 = v3 ? 0 : std::max(0, p.main_x), x1 = v3 ? W : std::min(W, p.main_x + 256);
        for (int y = y0; y < y1; ++y) {
            uint32_t* o = out + (size_t)y * pitch;
            const uint8_t* k = g_key + (size_t)y * W;
            // row start such that row3 + 4 * x is pixel (x - main_x, y - main_y); signed, it can lie before the frame
            const uint8_t* row3 = v3 ? data + p.off_v3d + 4 * (size_t)y * W
                                     : data + p.off_main + 4 * ((std::ptrdiff_t)(y - p.main_y) * 256 - p.main_x);
            for (int x = x0; x < x1; ++x) {
                uint32_t t;
                std::memcpy(&t, row3 + 4 * x, 4);
                const uint32_t a = (t >> 24) & 31;
                if (!a || key3 >= k[x]) continue;
                const uint32_t r = ((t & 63) << 2) | ((t & 63) >> 4), g = (((t >> 8) & 63) << 2) | (((t >> 8) & 63) >> 4),
                               b = (((t >> 16) & 63) << 2) | (((t >> 16) & 63) >> 4);
                if (a >= 31) { o[x] = 0xFF000000u | (r << 16) | (g << 8) | b; continue; }
                const uint32_t below = o[x], ia = 31 - a;
                o[x] = 0xFF000000u | (((r * a + ((below >> 16) & 255) * ia) / 31) << 16) |
                       (((g * a + ((below >> 8) & 255) * ia) / 31) << 8) | ((b * a + (below & 255) * ia) / 31);
            }
        }
    }
    // master brightness, then the HUD
    const uint32_t mode = (p.bright >> 14) & 3;
    if (mode == 1 || mode == 2)
        for (int y = 0; y < H; ++y) {
            uint32_t* o = out + (size_t)y * pitch;
            for (int x = 0; x < W; ++x) o[x] = brighten(o[x], p.bright);
        }
    if (p.nhud) {
        draw_sprites(s, p.nspr, p.nhud, W, H);
        for (int y = 0; y < H; ++y) {
            uint32_t* o = out + (size_t)y * pitch;
            const uint8_t* pr = g_sp_pr + (size_t)y * W;
            const uint16_t* cc = g_sp_col + (size_t)y * W;
            for (int x = 0; x < W; ++x)
                if (pr[x] < 4) o[x] = brighten(lut[cc[x] & 0x7FFF], p.bright);
        }
    }
}
