// SonicRush-nx switch-test16: instant replay (see srnx_replay.h).
#include "srnx_replay.h"
extern "C" { extern uint32_t g_srnx_linelog_last[254]; extern int g_srnx_linelog_last_n; }

#include "srnx_wide_render.h"

#include <cstdio>
#include <cstring>
#include <mutex>
#include <vector>
#include <sys/stat.h>

extern "C" uint32_t bus_read_u32_slow(uint32_t addr);
extern uint32_t g_srnx_touch_word;   // io.cpp: x | y << 8 | down << 16
uint32_t nds_gpu2d_read(uint32_t addr, uint32_t width);

#ifndef MPHNX_APP_DIR
#define MPHNX_APP_DIR "."
#endif

namespace {
constexpr int kFrames = 120;          // about 2 seconds at 60 fps (about 175 MB with sprite memory)
constexpr int kMaxSaves = 3;          // per session
constexpr uint32_t kSprBytes = 8192;  // sprite records + affine table + blend registers

struct Head {
    uint32_t serial;
    int32_t vw, vh, world_x, world_y, main_x, main_y;
    uint32_t nspr, nhud, dispcnt, v3d_valid, pad;
};

struct GameFrame {   // filled on the game thread
    Head h{};
    std::vector<uint32_t> ds;    // 256 x 384: top screen, then bottom
    std::vector<uint32_t> v3d;   // vw x vh, our own 3D layer (RGB6, alpha5 at bit 24)
    std::vector<uint8_t> spr;    // kSprBytes
    std::vector<uint8_t> obj;    // sprite memory as drawn (kSrnxObjVram)
};
struct ViewFrame {   // filled on the presenter thread
    uint32_t serial = 0;
    int32_t vw = 0, vh = 0;
    std::vector<uint32_t> px;
};

std::mutex g_mu;
GameFrame g_game[kFrames];
ViewFrame g_view[kFrames];
int g_game_next = 0, g_view_next = 0;
uint32_t g_serial = 0;
int g_saves = 0;
int g_vw = 0, g_vh = 0;   // size the rings were sized for

void size_rings(int vw, int vh) {
    if (vw == g_vw && vh == g_vh) return;
    g_vw = vw; g_vh = vh;
    for (auto& f : g_game) {
        f.h = Head{};
        f.ds.assign(256 * 384, 0);
        f.v3d.assign((size_t)vw * vh, 0);
        f.spr.assign(kSprBytes, 0);
        f.obj.assign(kSrnxObjVram, 0);
    }
    for (auto& f : g_view) { f.serial = 0; f.vw = vw; f.vh = vh; f.px.assign((size_t)vw * vh, 0); }
    g_game_next = g_view_next = 0;
}
}  // namespace

void srnx_replay_reset() {
    std::lock_guard<std::mutex> lk(g_mu);
    for (auto& f : g_game) f.h.serial = 0;
    for (auto& f : g_view) f.serial = 0;
}

void srnx_replay_record(SrnxWideParams& p, uint8_t* data, const uint32_t* top, const uint32_t* bottom) {
    std::lock_guard<std::mutex> lk(g_mu);
    size_rings(p.view_w, p.view_h);
    const uint32_t serial = ++g_serial;
    // tag the snapshot so the presenter can match its drawing to this frame
    std::memcpy(data + p.off_spr + kSrnxAffOff + 264, &serial, 4);
    GameFrame& f = g_game[g_game_next];
    g_game_next = (g_game_next + 1) % kFrames;
    f.h = Head{serial, p.view_w, p.view_h, p.world_x, p.world_y, p.main_x, p.main_y, p.nspr, p.nhud, p.dispcnt, p.v3d_valid, 0};
    if (top) std::memcpy(f.ds.data(), top, 256 * 192 * 4);
    if (bottom) std::memcpy(f.ds.data() + 256 * 192, bottom, 256 * 192 * 4);
    if (p.v3d_valid) std::memcpy(f.v3d.data(), data + p.off_v3d, (size_t)p.view_w * p.view_h * 4);
    else std::memset(f.v3d.data(), 0, f.v3d.size() * 4);
    const uint32_t n = std::min<uint32_t>(p.nspr + p.nhud, kSrnxSprMax);
    std::memset(f.spr.data(), 0, kSprBytes);
    std::memcpy(f.spr.data(), data + p.off_spr, n * 16);
    std::memcpy(f.spr.data() + kSprBytes - 512, data + p.off_spr + kSrnxAffOff, 512);
    {   // switch-test30: overlay functions that ran this frame, in the gap
        // between the sprite records (384 x 16 bytes) and the affine table
        const uint32_t n = (uint32_t)std::min<int>(g_srnx_codelist_n, 127);
        std::memcpy(f.spr.data() + 6144, &n, 4);
        std::memcpy(f.spr.data() + 6148, g_srnx_codelist, n * 4);
        // switch-test31: the last full frame's 2D/DMA register writes with
        // their scanlines (io.cpp), at 6656: u32 count, then pairs
        const uint32_t m = (uint32_t)std::min(g_srnx_linelog_last_n, 127);
        std::memcpy(f.spr.data() + 6656, &m, 4);
        std::memcpy(f.spr.data() + 6660, g_srnx_linelog_last, m * 8);
    }
    std::memcpy(f.obj.data(), data + p.off_obj, kSrnxObjVram);
}

void srnx_replay_record_ds(const uint32_t* top, const uint32_t* bottom) {
    std::lock_guard<std::mutex> lk(g_mu);
    if (!g_vw) size_rings(424, 240);
    const uint32_t serial = ++g_serial;
    GameFrame& f = g_game[g_game_next];
    g_game_next = (g_game_next + 1) % kFrames;
    // world_x/world_y: camera record 0 x/y, main_x/main_y: record 1 x/y (pixels),
    // dispcnt: engine A DISPCNT, v3d_valid: 0, pad: POWCNT1 | 0xD5000000 (not a stage frame)
    auto cam = [](uint32_t a) { return (int32_t)bus_read_u32_slow(a) >> 8; };
    f.h = Head{serial, g_vw, g_vh, cam(0x0236AADCu), cam(0x0236AAE0u), cam(0x0236AB40u), cam(0x0236AB44u), g_srnx_touch_word, 0,
               nds_gpu2d_read(0x04000000u, 4), 0, (bus_read_u32_slow(0x04000304u) & 0xFFFF) | 0xD5000000u};
    std::memcpy(f.ds.data(), top, 256 * 192 * 4);
    std::memcpy(f.ds.data() + 256 * 192, bottom, 256 * 192 * 4);
    std::memset(f.v3d.data(), 0, f.v3d.size() * 4);
    std::memset(f.spr.data(), 0, kSprBytes);
    // switch-test27: the overlay functions that ran this frame (u32 count, then
    // address | 0x80000000 for overlay 1)
    {
        const uint32_t n = (uint32_t)std::min<int>(g_srnx_codelist_n, (int)(kSprBytes / 4) - 1);
        std::memcpy(f.spr.data(), &n, 4);
        std::memcpy(f.spr.data() + 4, g_srnx_codelist, n * 4);
    }
    std::memset(f.obj.data(), 0, kSrnxObjVram);
}

void srnx_replay_view(const SrnxWideParams& p, const uint8_t* data, const uint32_t* view, int pitch) {
    uint32_t serial;
    std::memcpy(&serial, data + p.off_spr + kSrnxAffOff + 264, 4);
    std::lock_guard<std::mutex> lk(g_mu);
    if (p.view_w != g_vw || p.view_h != g_vh || !serial) return;
    ViewFrame& f = g_view[g_view_next];
    g_view_next = (g_view_next + 1) % kFrames;
    f.serial = serial;
    for (int y = 0; y < p.view_h; ++y) std::memcpy(f.px.data() + (size_t)y * p.view_w, view + (size_t)y * pitch, (size_t)p.view_w * 4);
}

int srnx_replay_save(char* msg, int msg_len) {
    std::lock_guard<std::mutex> lk(g_mu);
    if (g_saves >= kMaxSaves) { std::snprintf(msg, msg_len, "Replay limit reached (%d this session)", kMaxSaves); return 0; }
    if (!g_vw) { std::snprintf(msg, msg_len, "Nothing recorded yet"); return 0; }
    mkdir(MPHNX_APP_DIR "/captures", 0777);
    char path[112];
    int n = 1;
    for (; n < 1000; ++n) {
        std::snprintf(path, sizeof path, MPHNX_APP_DIR "/captures/rpl-%03d.bin", n);
        FILE* t = std::fopen(path, "rb");
        if (!t) break;
        std::fclose(t);
    }
    FILE* f = std::fopen(path, "wb");
    if (!f) { std::snprintf(msg, msg_len, "Replay save failed"); return 0; }
    // file: "SRNXRPL1", frame count, view w, h, then per frame oldest first:
    // Head, ds (256x384 u32), v3d (vw x vh u32), view (vw x vh u32, zero if
    // the presenter never drew it), sprites (8192 bytes)
    int count = 0;
    for (const auto& g : g_game) count += g.h.serial != 0;
    std::fwrite("SRNXRPL2", 1, 8, f);
    const int32_t hdr[3] = {count, g_vw, g_vh};
    std::fwrite(hdr, 4, 3, f);
    std::vector<uint32_t> zero((size_t)g_vw * g_vh, 0);
    for (int i = 0; i < kFrames; ++i) {
        const GameFrame& g = g_game[(g_game_next + i) % kFrames];
        if (!g.h.serial) continue;
        const ViewFrame* v = nullptr;
        for (const auto& vf : g_view) if (vf.serial == g.h.serial) { v = &vf; break; }
        std::fwrite(&g.h, sizeof(Head), 1, f);
        std::fwrite(g.ds.data(), 4, g.ds.size(), f);
        std::fwrite(g.v3d.data(), 4, g.v3d.size(), f);
        std::fwrite(v ? v->px.data() : zero.data(), 4, zero.size(), f);
        std::fwrite(g.spr.data(), 1, kSprBytes, f);
        std::fwrite(g.obj.data(), 1, kSrnxObjVram, f);
    }
    std::fclose(f);
    ++g_saves;
    std::snprintf(msg, msg_len, "Saved replay %03d (%d frames)", n, count);
    std::fprintf(stderr, "[sonicrush-nx] replay saved: %s (%d frames, %dx%d)\n", path, count, g_vw, g_vh);
    return n;
}
