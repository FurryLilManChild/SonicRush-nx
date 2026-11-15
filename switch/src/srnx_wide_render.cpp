// SonicRush-nx: wide stage view (default 825 x 464: both DS screens and the
// 80 px strip between them, at 16:9), built each frame on the CPU.
//
// What goes into it, back to front by DS priority:
//   - BG1, the far background: a 512x512 text layer that already runs
//     continuously across both screens, sampled past the DS edges with the
//     top engine's latched scroll.
//   - BG2/BG3, the level layers: drawn straight from the level layouts the
//     game keeps in RAM (camera block +0x130 block data, +0x134 / +0x138
//     layouts of 64 px blocks of 8x8 tile entries), so any width works. The
//     game's own 512 px tile maps cannot hold a wider view.
//   - sprites: the native sprite records (srnx_wide.cpp), at their true
//     positions.
// The two DS screens are then pasted into their windows (top at y 0, bottom
// at y 272) so everything the DS draws (Sonic's 3D layer, HUD, effects)
// stays exactly as the game drew it; the wide layers fill the sides and the
// strip.
#include "srnx_wide_render.h"

#include <cstdint>
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

namespace {
constexpr uint32_t kCam = 0x0236AAD8u;      // camera records: top/main +0, other +0x64
constexpr int kGapY = 272;                  // bottom screen's offset from the top screen

uint16_t g_xpal[2][256 * 16];               // BG2/BG3 extended palettes (16 x 256)
uint32_t g_rgb[32768];                      // RGB555 -> 0xFFRRGGBB
bool g_rgb_ready = false;
uint8_t g_prio[kSrnxWideMaxW * kSrnxWideMaxH];

inline uint32_t to_rgb(uint16_t c) { return g_rgb[c & 0x7FFF]; }

void init_rgb() {
    for (uint32_t c = 0; c < 32768; ++c) {
        const uint32_t r = (c & 31) * 255 / 31, g = ((c >> 5) & 31) * 255 / 31, b = ((c >> 10) & 31) * 255 / 31;
        g_rgb[c] = 0xFF000000u | (r << 16) | (g << 8) | b;
    }
    g_rgb_ready = true;
}

struct BgView {
    const NdsVramRendererView* v;
    inline uint8_t byte(uint32_t a) const {
        const uint8_t* p = v->bg[(a >> 14) & 31];
        return p ? p[a & 0x3FFF] : (uint8_t)nds_vram_read_bg(0, a, 1);
    }
    inline uint16_t half(uint32_t a) const {
        const uint8_t* p = v->bg[(a >> 14) & 31];
        if (p) { uint16_t h; std::memcpy(&h, p + (a & 0x3FFF), 2); return h; }
        return (uint16_t)nds_vram_read_bg(0, a, 2);
    }
    inline uint8_t obj(uint32_t a) const {
        const uint8_t* p = v->obj[(a >> 14) & 15];
        return p ? p[a & 0x3FFF] : (uint8_t)nds_vram_read_obj(0, a, 1);
    }
};

const int kSizes[3][4][2] = {{{8, 8}, {16, 16}, {32, 32}, {64, 64}},
                             {{16, 8}, {32, 8}, {32, 16}, {64, 32}},
                             {{8, 16}, {8, 32}, {16, 32}, {32, 64}}};
}  // namespace

int g_srnx_wide_w = 825, g_srnx_wide_h = 464;

bool srnx_wide_stage_active() {
    // A stage: main engine in mode 5 with BG2/BG3 on, the 3D layer on BG0,
    // and a camera block whose level layouts look sane.
    const uint32_t dc = nds_gpu2d_read(0x04000000u, 4);
    if ((dc & 7) != 5 || !(dc & 0x0C00) || !(dc & 8)) return false;
    const uint32_t blk = NR_RD32(kCam + 0x130), l0 = NR_RD32(kCam + 0x134), l1 = NR_RD32(kCam + 0x138);
    auto ram = [](uint32_t a) { return a >= 0x02000000u && a < 0x02400000u; };
    if (!ram(blk) || !ram(l0) || !ram(l1)) return false;
    const uint32_t w0 = NR_RD16(l0), h0 = NR_RD16(l0 + 2);
    if (!w0 || !h0 || w0 > 1024 || h0 > 1024) return false;
    if ((int16_t)NR_RD16(kCam + 0x1E) != 256) return false;
    return true;
}

bool srnx_wide_render(uint32_t* out, const uint32_t* top, const uint32_t* bottom) {
    const int W = g_srnx_wide_w, H = g_srnx_wide_h;
    if (W > kSrnxWideMaxW || H > kSrnxWideMaxH || W < 256 || H < 464) return false;
    if (!g_rgb_ready) init_rgb();
    const NdsVramRendererView* vv = nds_vram_renderer_view(0);
    const uint8_t* pal8 = nds_vram_renderer_palette(0);
    if (!vv || !pal8) return false;
    const BgView vr{vv};
    const uint16_t* bgpal = reinterpret_cast<const uint16_t*>(pal8);
    const uint16_t* objpal = reinterpret_cast<const uint16_t*>(pal8 + 0x200);

    const uint32_t dc = nds_gpu2d_read(0x04000000u, 4);
    uint16_t cnt[4];
    for (int i = 0; i < 4; ++i) cnt[i] = (uint16_t)nds_gpu2d_read(0x04000008u + 2u * i, 2);
    uint16_t bgx[4], bgy[4]; int32_t rx[2], ry[2]; int16_t pm[8];
    nds_gpu2d_debug_regs(0, bgx, bgy, rx, ry, pm);
    const uint32_t char_off = ((dc >> 24) & 7) * 0x10000u, map_off = ((dc >> 27) & 7) * 0x10000u;
    const bool ext_pal = (dc >> 30) & 1;

    // The level's world position of the wide view's top-left: the DS view
    // sits centred horizontally, the top screen at the top.
    const int left = (W - 256) / 2;
    const int camx = (int32_t)NR_RD32(kCam + 4) >> 8, camy = (int32_t)NR_RD32(kCam + 8) >> 8;
    const int wx0 = camx - left, wy0 = camy;

    // extended palettes of BG2 / BG3 (slots 2, 3)
    if (ext_pal)
        for (int s = 0; s < 2; ++s)
            for (uint32_t i = 0; i < 256 * 16; i += 2) {
                const uint32_t w = nds_vram_read_bg_extpal(0, (2u + s) * 0x2000u + i * 2u, 4);
                g_xpal[s][i] = (uint16_t)w;
                g_xpal[s][i + 1] = (uint16_t)(w >> 16);
            }

    const uint32_t backdrop = to_rgb(bgpal[0]);
    for (int i = 0; i < W * H; ++i) out[i] = backdrop;
    std::memset(g_prio, 9, (size_t)W * H);

    // layers back to front: lower DS priority number draws later; ties: lower BG wins
    int order[3] = {1, 2, 3};
    for (int a = 0; a < 3; ++a)
        for (int b = a + 1; b < 3; ++b) {
            const int pa = cnt[order[a]] & 3, pb = cnt[order[b]] & 3;
            if (pa < pb || (pa == pb && order[a] < order[b])) { const int t = order[a]; order[a] = order[b]; order[b] = t; }
        }

    const uint32_t blk = NR_RD32(kCam + 0x130);
    const uint32_t layouts[2] = {NR_RD32(kCam + 0x134), NR_RD32(kCam + 0x138)};

    for (int oi = 0; oi < 3; ++oi) {
        const int bg = order[oi];
        if (!((dc >> (8 + bg)) & 1)) continue;
        const uint16_t c = cnt[bg];
        const uint8_t prio = c & 3;
        const uint32_t chrbase = char_off + ((c >> 2) & 15) * 0x4000u;
        if (bg == 1) {
            // far background: text layer, 4 or 8 bpp, wraps at its size
            const uint32_t mapbase = map_off + ((c >> 8) & 31) * 0x800u;
            const int size = (c >> 14) & 3;
            const int lw = (size == 1 || size == 3) ? 512 : 256, lh = (size >= 2) ? 512 : 256;
            const bool bpp8 = c & 0x80;
            for (int y = 0; y < H; ++y) {
                const int yy = ((y + bgy[1]) % lh + lh) % lh;
                const int ty = yy >> 3, py0 = yy & 7;
                for (int x = 0; x < W; ++x) {
                    const int xx = (((x - left) + bgx[1]) % lw + lw) % lw;
                    const int tx = xx >> 3;
                    uint32_t sb = 0;
                    if (lw == 512 && tx >= 32) sb += 1;
                    if (lh == 512 && ty >= 32) sb += (lw == 512) ? 2 : 1;
                    const uint16_t ent = vr.half(mapbase + sb * 0x800u + (uint32_t)(((ty & 31) * 32 + (tx & 31)) * 2));
                    int px = xx & 7, py = py0;
                    if (ent & 0x400) px = 7 - px;
                    if (ent & 0x800) py = 7 - py;
                    const uint32_t t = ent & 0x3FF;
                    uint32_t v;
                    if (bpp8) v = vr.byte(chrbase + t * 64 + py * 8 + px);
                    else { const uint8_t b = vr.byte(chrbase + t * 32 + py * 4 + px / 2); v = (px & 1) ? (b >> 4) : (b & 15); }
                    if (!v) continue;
                    const uint16_t col = bpp8 ? bgpal[v] : bgpal[(ent >> 12) * 16 + v];
                    out[y * W + x] = to_rgb(col);
                    g_prio[y * W + x] = prio;
                }
            }
            continue;
        }
        // level layer from the layout: BG2 <- layout 0, BG3 <- layout 1
        const uint32_t L = layouts[bg - 2];
        const int lw = NR_RD16(L), lh = NR_RD16(L + 2);
        const uint16_t* xp = g_xpal[bg - 2];
        for (int y = 0; y < H; ++y) {
            const int wy = wy0 + y;
            if (wy < 0 || (wy >> 6) >= lh) continue;
            const int by = wy >> 6, ty = (wy & 63) >> 3, py0 = wy & 7;
            for (int x = 0; x < W;) {
                const int wx = wx0 + x;
                const int span = 8 - (wx & 7);                     // pixels left in this tile
                if (wx < 0 || (wx >> 6) >= lw) { x += span; continue; }
                const int bx = wx >> 6, tx = (wx & 63) >> 3;
                const uint16_t b = (uint16_t)NR_RD16(L + 4u + 2u * (uint32_t)(by * lw + bx));
                const uint16_t ent = (uint16_t)NR_RD16(blk + b * 128u + (uint32_t)((ty * 8 + tx) * 2));
                const uint32_t t = ent & 0x3FF, p = ent >> 12;
                const int py = (ent & 0x800) ? 7 - py0 : py0;
                for (int k = 0; k < span && x < W; ++k, ++x) {
                    int px = (wx + k) & 7;
                    if (ent & 0x400) px = 7 - px;
                    const uint32_t v = vr.byte(chrbase + t * 64 + py * 8 + px);
                    if (!v) continue;
                    out[y * W + x] = to_rgb(ext_pal ? xp[p * 256 + v] : bgpal[v]);
                    g_prio[y * W + x] = prio;
                }
            }
        }
    }

    // sprites at their true positions (main engine records)
    if ((dc >> 12) & 1) {
        const SrnxSprite* sp = nullptr;
        const uint32_t n = srnx_spr_last(0, &sp);
        const bool one_d = dc & 0x10;
        const uint32_t bound = 32u << ((dc >> 20) & 3);
        // draw lowest priority first; within a priority, later records first so
        // earlier ones (lower OAM order) end on top
        for (int pass = 3; pass >= 0; --pass)
            for (int i = (int)n - 1; i >= 0; --i) {
                const SrnxSprite& s = sp[i];
                const uint16_t a0 = s.attr0, a1 = s.attr1, a2 = s.attr2;
                if (((a2 >> 10) & 3) != pass) continue;
                const bool rs = (a0 >> 8) & 1;
                if (!rs && ((a0 >> 9) & 1)) continue;
                if (((a0 >> 10) & 3) == 3) continue;                 // bitmap OBJ
                const int shape = (a0 >> 14) & 3;
                if (shape == 3) continue;
                const int w = kSizes[shape][(a1 >> 14) & 3][0], h = kSizes[shape][(a1 >> 14) & 3][1];
                const int ox = left + (s.x >> 12), oy = s.y >> 12;
                const bool bpp8 = a0 & 0x2000;
                const uint32_t tile = a2 & 0x3FF, palno = a2 >> 12;
                const bool hf = !rs && (a1 & 0x1000), vf = !rs && (a1 & 0x2000);
                for (int yy = 0; yy < h; ++yy) {
                    const int Y = oy + yy;
                    if (Y < 0 || Y >= H) continue;
                    const int py = vf ? h - 1 - yy : yy;
                    for (int xx = 0; xx < w; ++xx) {
                        const int X = ox + xx;
                        if (X < 0 || X >= W || pass > g_prio[Y * W + X]) continue;
                        const int px = hf ? w - 1 - xx : xx;
                        const uint32_t tx = (uint32_t)px >> 3, ty = (uint32_t)py >> 3;
                        uint32_t o;
                        if (one_d) o = tile * bound + ty * (uint32_t)(w / 8) * (bpp8 ? 64u : 32u) + tx * (bpp8 ? 64u : 32u);
                        else o = (tile + ty * 32u + tx * (bpp8 ? 2u : 1u)) * 32u;
                        uint32_t v;
                        if (bpp8) v = vr.obj(o + (py & 7) * 8 + (px & 7));
                        else { const uint8_t b = vr.obj(o + (py & 7) * 4 + (px & 7) / 2); v = (px & 1) ? (b >> 4) : (b & 15); }
                        if (!v) continue;
                        out[Y * W + X] = to_rgb(bpp8 ? objpal[v] : objpal[palno * 16 + v]);
                    }
                }
            }
    }

    // the DS screens as the game drew them, in their windows
    if (top)
        for (int y = 0; y < 192; ++y) std::memcpy(out + y * W + left, top + y * 256, 256 * 4);
    if (bottom)
        for (int y = 0; y < 192 && kGapY + y < H; ++y) std::memcpy(out + (kGapY + y) * W + left, bottom + y * 256, 256 * 4);
    return true;
}
