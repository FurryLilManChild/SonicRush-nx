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
#include <cstring>

#include "../native/nr_runtime.h"
#include "../native/srnx_wide.h"

uint32_t nds_gpu2d_read(uint32_t addr, uint32_t width);
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

int g_srnx_zoom = 0;
int g_srnx_cam_lag = 2;   // host-checked: the DS screen just drawn matches the camera two VBlanks back


namespace {
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
void copy_vram(uint8_t* dst, const uint8_t* const* pages, int npages, bool obj) {
    for (int i = 0; i < npages; ++i) {
        uint8_t* d = dst + i * 0x4000;
        if (pages[i]) { std::memcpy(d, pages[i], 0x4000); continue; }
        for (uint32_t a = 0; a < 0x4000; a += 4) {
            const uint32_t w = obj ? nds_vram_read_obj(0, i * 0x4000u + a, 4) : nds_vram_read_bg(0, i * 0x4000u + a, 4);
            std::memcpy(d + a, &w, 4);
        }
    }
}

void copy_ram(uint8_t* dst, uint32_t src, uint32_t bytes) {
    for (uint32_t a = 0; a < bytes; a += 4) {
        const uint32_t w = NR_RD32(src + a);
        std::memcpy(dst + a, &w, 4);
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
struct Latch { int cam0x, cam0y, cam1y; float tx, ty; bool ok; };
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

bool srnx_wide_snapshot(SrnxWideParams& p, uint8_t* data, const uint32_t* top, const uint32_t* bottom) {
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
    p.bg1_sx = bgx[1] + (wx - cam0x);
    p.bg1_sy = bgy[1] + (wy - cam0y);
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
    p.off_spr = take(kSrnxSprMax * 16);
    p.off_main = take(256 * 192 * 4);
    if (off > kSrnxDataBytes) return false;

    const NdsVramRendererView* vv = nds_vram_renderer_view(0);
    const uint8_t* pal = nds_vram_renderer_palette(0);
    if (!vv || !pal) return false;
    copy_vram(data + p.off_bg, vv->bg, 32, false);
    copy_vram(data + p.off_obj, vv->obj, 16, true);
    std::memcpy(data + p.off_bgpal, pal, 512);
    std::memcpy(data + p.off_objpal, pal + 512, 512);
    if ((p.dispcnt >> 30) & 1)
        for (uint32_t s = 0; s < 2; ++s)
            for (uint32_t a = 0; a < 8192; a += 4) {
                const uint32_t w = nds_vram_read_bg_extpal(0, (2u + s) * 0x2000u + a, 4);
                std::memcpy(data + p.off_xpal + s * 8192 + a, &w, 4);
            }
    p.backdrop = rgb((uint16_t)(pal[0] | (pal[1] << 8)));

    // layouts and block data
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
                if (x >= 128) x += VW - 256;
                if (y >= 172) y += VH - 192;
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
    const uint32_t* f3d = nds_gpu2d_srnx_3d_frame(nullptr);
    p.main_valid = (mainscr && f3d && (p.dispcnt & 0x100)) ? 1u : 0u;
    if (p.main_valid) {
        std::memcpy(data + p.off_main, f3d, 256 * 192 * 4);
        srnx_wide_align_main(p, data, mainscr);
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
bool bg_pixel(const Src& s, int bg, int x, int y, uint16_t& col) {
    const SrnxWideParams& p = s.p;
    const uint32_t c = p.bgcnt[bg];
    const uint32_t char_off = ((p.dispcnt >> 24) & 7) * 0x10000u, map_off = ((p.dispcnt >> 27) & 7) * 0x10000u;
    const uint32_t chrbase = char_off + ((c >> 2) & 15) * 0x4000u;
    if (bg == 1) {
        const int size = (c >> 14) & 3;
        const int lw = (size & 1) ? 512 : 256, lh = (size & 2) ? 512 : 256;
        const int xx = (((x + p.bg1_sx) % lw) + lw) % lw, yy = (((y + p.bg1_sy) % lh) + lh) % lh;
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
        const bool rs = (a0 >> 8) & 1;
        if (!rs && ((a0 >> 9) & 1)) continue;
        if (((a0 >> 10) & 3) == 3) continue;
        const int shape = (a0 >> 14) & 3;
        if (shape == 3) continue;
        const int w = kSz[shape][(a1 >> 14) & 3][0], h = kSz[shape][(a1 >> 14) & 3][1];
        int xx = x - sx, yy = y - sy;
        if (xx < 0 || yy < 0 || xx >= w || yy >= h) continue;
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
        col = s.u16(p.off_objpal + 2 * (bpp8 ? v : ((a2 >> 12) * 16 + v)));
        prio = pr;
        hit = true;
    }
    return hit;
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
    int best = -1, bdx = 0, bdy = 0;
    for (int dy = -3; dy <= 3; ++dy)
        for (int dx = -3; dx <= 3; ++dx) {
            int score = 0;
            for (int gy = 4; gy < 192; gy += 8)
                for (int gx = 4; gx < 256; gx += 8) {
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
            if (score > best || (score == best && dx == 0 && dy == 0)) { best = score; bdx = dx; bdy = dy; }
        }
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
        below = rgb(sc);
        below_key = sp * 8 + 1;
    }
    uint32_t c = below;
    // the 3D layer (BG0)
    const int mx = x - p.main_x, my = y - p.main_y;
    if (p.main_valid && mx >= 0 && my >= 0 && mx < 256 && my < 192) {
        const uint32_t t = s.u32(p.off_main + 4u * (uint32_t)(my * 256 + mx));
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

void srnx_wide_draw_cpu(const SrnxWideParams& p, const uint8_t* data, uint32_t* out, int pitch) {
    const Src s{p, data};
    for (int y = 0; y < p.view_h; ++y)
        for (int x = 0; x < p.view_w; ++x)
            out[y * pitch + x] = srnx_wide_pixel(s, x, y);
}
