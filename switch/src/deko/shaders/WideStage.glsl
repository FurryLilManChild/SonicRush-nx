#version 460
// SonicRush-nx: our own stage view, one thread per view pixel. Mirrors
// srnx_wide_draw_cpu (switch/src/srnx_wide_render.cpp); see there for what
// each layer is and where it comes from. The snapshot is one buffer of
// 32-bit words (byte offsets in the params); the output is 0xAARRGGBB words,
// OutPitch per row.
layout (local_size_x = 8, local_size_y = 8) in;

layout (std140, binding = 0) uniform Params
{
    int ViewW, ViewH, MainX, MainY;
    uint DispCnt, BgCnt0, BgCnt1, BgCnt2, BgCnt3;
    uint Order0, Order1, Order2, Order3;
    int Bg1Sx, Bg1Sy, WorldX, WorldY;
    uint LayW0, LayW1, LayH0, LayH1;
    uint NSpr, NHud;
    int HudDx, HudDy;
    uint Bright, Backdrop;
    uint OffBg, OffObj, OffBgPal, OffObjPal, OffXPal, OffLay0, OffLay1, OffBlk, OffSpr, OffMain;
    uint MainValid, OutPitch, Pad1, Pad2;
};

layout (std430, binding = 0) readonly buffer DataBuf { uint Data[]; };
layout (std430, binding = 1) writeonly buffer OutBuf { uint Out[]; };

const uint kBgMask = 512u * 1024u - 1u, kObjMask = 256u * 1024u - 1u, kBlkMax = 2048u * 128u;

uint rd8(uint o)  { return (Data[o >> 2] >> ((o & 3u) * 8u)) & 0xFFu; }
uint rd16(uint o) { return (Data[o >> 2] >> ((o & 2u) * 8u)) & 0xFFFFu; }   // o is even
uint rd32(uint o) { return Data[o >> 2]; }

uint ex(uint c5) { uint c6 = c5 * 2u; return (c6 << 2) | (c6 >> 4); }
uint rgb(uint c) { return 0xFF000000u | (ex(c & 31u) << 16) | (ex((c >> 5) & 31u) << 8) | ex((c >> 10) & 31u); }

uint bgcnt(uint bg) { return bg == 0u ? BgCnt0 : bg == 1u ? BgCnt1 : bg == 2u ? BgCnt2 : BgCnt3; }

bool bg_pixel(uint bg, int x, int y, out uint col)
{
    col = 0u;
    uint c = bgcnt(bg);
    uint char_off = ((DispCnt >> 24) & 7u) * 0x10000u, map_off = ((DispCnt >> 27) & 7u) * 0x10000u;
    uint chrbase = char_off + ((c >> 2) & 15u) * 0x4000u;
    if (bg == 1u) {
        uint size = (c >> 14) & 3u;
        int lw = (size & 1u) != 0u ? 512 : 256, lh = (size & 2u) != 0u ? 512 : 256;
        int xx = (x + Bg1Sx) & (lw - 1), yy = (y + Bg1Sy) & (lh - 1);   // power-of-two sizes; & also wraps negatives
        int tx = xx >> 3, ty = yy >> 3;
        uint sb = 0u;
        if (lw == 512 && tx >= 32) sb += 1u;
        if (lh == 512 && ty >= 32) sb += (lw == 512) ? 2u : 1u;
        uint mapbase = map_off + ((c >> 8) & 31u) * 0x800u;
        uint ent = rd16(OffBg + ((mapbase + sb * 0x800u + uint(((ty & 31) * 32 + (tx & 31)) * 2)) & kBgMask));
        int px = xx & 7, py = yy & 7;
        if ((ent & 0x400u) != 0u) px = 7 - px;
        if ((ent & 0x800u) != 0u) py = 7 - py;
        uint t = ent & 0x3FFu, v;
        if ((c & 0x80u) != 0u) v = rd8(OffBg + ((chrbase + t * 64u + uint(py * 8 + px)) & kBgMask));
        else {
            uint b = rd8(OffBg + ((chrbase + t * 32u + uint(py * 4 + (px >> 1))) & kBgMask));
            v = (px & 1) != 0 ? (b >> 4) : (b & 15u);
        }
        if (v == 0u) return false;
        col = rd16(OffBgPal + 2u * ((c & 0x80u) != 0u ? v : ((ent >> 12) * 16u + v)));
        return true;
    }
    uint k = bg - 2u;
    int wx = WorldX + x, wy = WorldY + y;
    if (wx < 0 || wy < 0) return false;
    uint lw = k == 0u ? LayW0 : LayW1, lh = k == 0u ? LayH0 : LayH1;
    uint bx = uint(wx) >> 6, by = uint(wy) >> 6;
    if (bx >= lw || by >= lh) return false;
    uint b = rd16((k == 0u ? OffLay0 : OffLay1) + 2u * (by * lw + bx));
    if (b * 128u >= kBlkMax) return false;
    uint tx = (uint(wx) & 63u) >> 3, ty = (uint(wy) & 63u) >> 3;
    uint ent = rd16(OffBlk + b * 128u + (ty * 8u + tx) * 2u);
    int px = wx & 7, py = wy & 7;
    if ((ent & 0x400u) != 0u) px = 7 - px;
    if ((ent & 0x800u) != 0u) py = 7 - py;
    uint v = rd8(OffBg + ((chrbase + (ent & 0x3FFu) * 64u + uint(py * 8 + px)) & kBgMask));
    if (v == 0u) return false;
    col = ((DispCnt >> 30) & 1u) != 0u ? rd16(OffXPal + k * 8192u + 2u * ((ent >> 12) * 256u + v))
                                       : rd16(OffBgPal + 2u * v);
    return true;
}

// OBJ size by shape and size bits, computed (no arrays: arrays indexed at run
// time go to per-thread local memory)
ivec2 obj_size(uint shape, uint size)
{
    int big = 8 << int(size);                                   // square: 8,16,32,64
    if (shape == 0u) return ivec2(big, big);
    int l = size == 0u ? 16 : size == 1u ? 32 : size == 2u ? 32 : 64;
    int s = size == 0u ? 8 : size == 1u ? 8 : size == 2u ? 16 : 32;
    return shape == 1u ? ivec2(l, s) : ivec2(s, l);
}

bool spr_pixel(uint first, uint count, int x, int y, out uint col, out int prio)
{
    col = 0u;
    prio = 4;
    bool one_d = (DispCnt & 0x10u) != 0u;
    uint bound = 32u << ((DispCnt >> 20) & 3u);
    bool hit = false;
    for (uint i = first; i < first + count; ++i) {
        uint o = OffSpr + i * 16u;
        int sx = int(rd32(o)), sy = int(rd32(o + 4u));
        uint a01 = rd32(o + 8u), a2 = rd32(o + 12u);
        uint a0 = a01 & 0xFFFFu, a1 = a01 >> 16;
        int pr = int((a2 >> 10) & 3u);
        if (hit && pr >= prio) continue;
        bool rs = ((a0 >> 8) & 1u) != 0u;
        if (!rs && ((a0 >> 9) & 1u) != 0u) continue;
        if (((a0 >> 10) & 3u) == 3u) continue;
        uint shape = (a0 >> 14) & 3u;
        if (shape == 3u) continue;
        ivec2 sz = obj_size(shape, (a1 >> 14) & 3u);
        int xx = x - sx, yy = y - sy;
        if (xx < 0 || yy < 0 || xx >= sz.x || yy >= sz.y) continue;
        if (!rs && (a1 & 0x1000u) != 0u) xx = sz.x - 1 - xx;
        if (!rs && (a1 & 0x2000u) != 0u) yy = sz.y - 1 - yy;
        bool bpp8 = (a0 & 0x2000u) != 0u;
        uint tile = a2 & 0x3FFu;
        uint tx = uint(xx) >> 3, ty = uint(yy) >> 3;
        uint to;
        if (one_d) to = tile * bound + (ty * uint(sz.x >> 3) + tx) * (bpp8 ? 64u : 32u);
        else to = (tile + ty * 32u + tx * (bpp8 ? 2u : 1u)) * 32u;
        uint v;
        if (bpp8) v = rd8(OffObj + ((to + uint((yy & 7) * 8 + (xx & 7))) & kObjMask));
        else {
            uint b = rd8(OffObj + ((to + uint((yy & 7) * 4 + ((xx & 7) >> 1))) & kObjMask));
            v = (xx & 1) != 0 ? (b >> 4) : (b & 15u);
        }
        if (v == 0u) continue;
        col = rd16(OffObjPal + 2u * (bpp8 ? v : ((a2 >> 12) * 16u + v)));
        prio = pr;
        hit = true;
    }
    return hit;
}

uint brighten(uint c)
{
    uint mode = (Bright >> 14) & 3u, f = min(Bright & 31u, 16u);
    if (mode != 1u && mode != 2u) return c;
    uint r = (c >> 16) & 255u, g = (c >> 8) & 255u, b = c & 255u;
    if (mode == 1u) { r += (((255u - r) * f) >> 4); g += (((255u - g) * f) >> 4); b += (((255u - b) * f) >> 4); }
    else { r -= (r * f) >> 4; g -= (g * f) >> 4; b -= (b * f) >> 4; }
    return 0xFF000000u | (r << 16) | (g << 8) | b;
}

uint e6(uint v) { return (v << 2) | (v >> 4); }

void main()
{
    int x = int(gl_GlobalInvocationID.x), y = int(gl_GlobalInvocationID.y);
    if (Pad1 == 0xC0DEu) {
        // probe: echo a few parameters so the presenter can check the GPU
        // sees the uniform block and both buffers
        if (x == 0 && y == 0) {
            Out[0] = uint(ViewW); Out[1] = uint(ViewH); Out[2] = OffSpr; Out[3] = Data[0]; Out[4] = 0x600Du;
        }
        return;
    }
    if (x >= ViewW || y >= ViewH) return;
    // BGs, front to back
    uint bcol = 0u;
    int bkey = 99;
    for (int i = 2; i >= 0 && bkey == 99; --i) {
        uint bg = i == 2 ? Order2 : i == 1 ? Order1 : Order0;
        if (bg > 3u || ((DispCnt >> (8u + bg)) & 1u) == 0u) continue;
        uint cc;
        if (bg_pixel(bg, x, y, cc)) { bcol = cc; bkey = int(bgcnt(bg) & 3u) * 8 + 2 + int(bg); }
    }
    uint below = bkey < 99 ? rgb(bcol) : Backdrop;
    int below_key = bkey;
    // sprites
    uint sc; int sp;
    if (((DispCnt >> 12) & 1u) != 0u && spr_pixel(0u, min(NSpr, 384u), x, y, sc, sp) && sp * 8 + 1 < below_key) {
        below = rgb(sc);
        below_key = sp * 8 + 1;
    }
    uint c = below;
    // the DS 3D layer (BG0)
    int mx = x - MainX, my = y - MainY;
    if (MainValid != 0u && mx >= 0 && my >= 0 && mx < 256 && my < 192) {
        uint t = rd32(OffMain + 4u * uint(my * 256 + mx));
        uint a = (t >> 24) & 31u;
        int key3 = int(BgCnt0 & 3u) * 8 + 2;
        if (a != 0u && key3 < below_key) {
            uint r = e6(t & 63u), g = e6((t >> 8) & 63u), b = e6((t >> 16) & 63u);
            if (a >= 31u) c = 0xFF000000u | (r << 16) | (g << 8) | b;
            else {
                uint ia = 31u - a;
                uint br = (below >> 16) & 255u, bg_ = (below >> 8) & 255u, bb = below & 255u;
                // x/31 without a divide: (x * 8457) >> 18 is exact for x <= 7905 (255 * 31)
                c = 0xFF000000u | ((((r * a + br * ia) * 8457u) >> 18) << 16) |
                    ((((g * a + bg_ * ia) * 8457u) >> 18) << 8) | (((b * a + bb * ia) * 8457u) >> 18);
            }
        }
    }
    c = brighten(c);
    uint hc; int hp;
    if (NHud != 0u && spr_pixel(NSpr, min(NHud, 64u), x, y, hc, hp)) c = brighten(rgb(hc));
    Out[uint(y) * OutPitch + uint(x)] = c;
}
