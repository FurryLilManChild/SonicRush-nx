#include <chrono>
int srnx_code_frame();
extern int g_srnx_lvl_zone, g_srnx_lvl_act, g_srnx_lvl_armed;
extern uint32_t g_srnx_codelist[];
extern int g_srnx_codelist_n;   // srnx_wide_render.cpp (file scope: the hooks below sit in an anonymous namespace)
// SonicRush-nx host boot test (from MPH-nx): install the native ARM9 engine before upstream
// main() runs (the Switch build does this from nx_main.cpp).
//   NR_DISABLE=1        leave it out: the ARM9 runs on upstream's interpreter
//                       as a reference
//   NR_DUMP=a:n,a:n     at exit, print n words from each guest address a
//   NR_SHOT=file.ppm    at exit, write both screens (top above bottom)
//   NR_STATE_SAVE=file@cycles  save a state once ARM9 time passes cycles
//   NR_STATE_LOAD=file@cycles  load a state once ARM9 time passes cycles
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include "gpu2d.h"
void nds_gpu2d_cow_stats(uint64_t* pages, uint64_t* fallbacks, uint64_t* page_mask);
extern "C" void nr_install(void);
extern bool g_nx_fast_card;
extern "C" uint32_t bus_read_u32_slow(uint32_t addr);
const uint32_t* nds_gpu2d_framebuffer(int screen);

extern unsigned long long g_nx_dma_blocks, g_nx_dma_block_units;   // io.cpp (native9)
extern bool g_nx_dma_block;
#include "savestate.h"
#include "../native/nr_state.h"
#include "../native/srnx_wide.h"
#include "../src/srnx_wide_render.h"
extern double g_snap_part[8];
extern int g_srnx_act_extra, g_srnx_act_off;   // srnx_wide_render.cpp   // srnx_wide_render.cpp, SRNX_HOST_PROFILE
extern void (*g_nx_host_round_hook)(uint64_t arm9_cycles);

void nds_set_key_mask(uint32_t mask);   // io.cpp
void nds_set_touch(uint16_t x, uint16_t y, bool down);   // io.cpp
const uint8_t* nds_vram_oam_live();     // vram.cpp
uint32_t nds_vram_read_bg(int engine, uint32_t addr, uint32_t width);
uint32_t nds_vram_read_obj(int engine, uint32_t addr, uint32_t width);
uint32_t nds_vram_read_bg_extpal(int engine, uint32_t addr, uint32_t width);
uint32_t nds_vram_read_obj_extpal(int engine, uint32_t addr, uint32_t width);
const uint8_t* nds_vram_palette_live();
uint32_t nds_gpu2d_read(uint32_t addr, uint32_t width);
void nds_gpu2d_debug_regs(int e, uint16_t bgx[4], uint16_t bgy[4], int32_t refx[2], int32_t refy[2], int16_t pabcd[8]);
const uint32_t* nds_gpu2d_srnx_3d_frame(uint32_t* frames);
extern unsigned long long g_srnx_gx_busy_skips, g_srnx_gx_busy_reads, g_srnx_gx_halts;   // gpu3d.cpp
#include <unordered_map>
#include <algorithm>
static std::unordered_map<uint32_t, unsigned long long> g_svc_hist;
#include "srnx_remap.h"
// switch-test13 host check of the 3D remap: the soft renderer's 1x output is
// the remapped rectangle squeezed into 256x192 (NR_OWN3D=1).
static int host_own3d() { static int v = -1; if (v < 0) v = std::getenv("NR_OWN3D") ? 1 : 0; return v; }
static SrnxRemap g_host_prev_render{}, g_host_shown{};
bool nx_deko_stage_image(const uint32_t** px, int* w, int* h, int* rx0, int* ry0, int* rw, int* rh, int* wx, int* wy) {
    if (!host_own3d() || !g_host_shown.on) return false;
    *px = nds_gpu2d_srnx_3d_frame(nullptr); *w = 256; *h = 192;
    *rx0 = g_host_shown.rx0; *ry0 = g_host_shown.ry0; *rw = g_host_shown.rw; *rh = g_host_shown.rh;
    *wx = g_host_shown.wx; *wy = g_host_shown.wy;
    return true;
}
void nx_deko_srnx_force_scale(int) {}
// called at each snapshot: the frame on the DS screen was drawn with the remap
// that was "render" one frame earlier (soft renderer draws during the frame).
extern int g_srnx_own3d;
void host_own3d_tick() { g_srnx_own3d = host_own3d(); if (!host_own3d()) return; g_host_shown = g_host_prev_render; g_host_prev_render = g_srnx_remap_render; }
extern "C" void nr_svc_hist_note(uint32_t pc) { ++g_svc_hist[pc]; }
static void svc_hist_print() {
    if (g_svc_hist.empty()) return;
    std::vector<std::pair<unsigned long long, uint32_t>> v;
    unsigned long long tot = 0;
    for (auto& kv : g_svc_hist) { v.push_back({kv.second, kv.first}); tot += kv.second; }
    std::sort(v.rbegin(), v.rend());
    std::fprintf(stderr, "[svc] total %llu\n", tot);
    for (size_t i = 0; i < v.size() && i < 25; ++i) std::fprintf(stderr, "[svc] %08X %llu\n", v[i].second, v[i].first);
}
extern bool g_srnx_gx_skip;
extern void (*g_srnx_vblank_hook)();

namespace {
const SrnxSprite* g_spr_tmp = nullptr;
SrnxWideParams g_wide_p{};
std::vector<uint8_t> g_wide_data;
bool g_wide_have = false;
double g_snap_ms = 0; unsigned g_snap_n = 0;
uint32_t g_wide_ds[2][256 * 192];
void write_shot(const char* f) {
    std::FILE* o = std::fopen(f, "wb");
    if (!o) return;
    std::fprintf(o, "P6\n256 384\n255\n");
    for (int sc = 0; sc < 2; ++sc) {
        const uint32_t* fb = nds_gpu2d_framebuffer(sc);
        for (int i = 0; i < 256 * 192; ++i) {
            const uint32_t p = fb ? fb[i] : 0;
            const unsigned char rgb[3] = {(unsigned char)(p >> 16), (unsigned char)(p >> 8), (unsigned char)p};
            std::fwrite(rgb, 1, 3, o);
        }
    }
    std::fclose(o);
}
void shot_at_exit() {
    if (const char* f = std::getenv("NR_SHOT")) write_shot(f);
}
void gpu2d_stats_at_exit() {
    NdsGpu2dProfile p{};
    nds_gpu2d_profile(&p);
    static const char* names[] = {"vram", "vramcnt", "palette", "oam", "frame", "present", "slots", "capture"};
    std::fprintf(stderr, "[gpu2d] threaded %llu inline %llu helped %llu wait %.1f ms\n",
                 (unsigned long long)p.threaded_lines, (unsigned long long)p.inline_lines,
                 (unsigned long long)p.fence_helped_lines, p.fence_wait_ns / 1e6);
    for (int i = 0; i < NDS_GPU2D_FENCE_CAUSE_COUNT && i < 8; ++i)
        std::fprintf(stderr, "[gpu2d]   fence %-8s drains %llu lines %llu\n", names[i],
                     (unsigned long long)p.fence_drains[i], (unsigned long long)p.fenced_lines[i]);
    uint64_t cp = 0, cf = 0, cm = 0;
    nds_gpu2d_cow_stats(&cp, &cf, &cm);
    std::fprintf(stderr, "[gpu2d] cow pages %llu fallbacks %llu page mask %llx\n",
                 (unsigned long long)cp, (unsigned long long)cf, (unsigned long long)cm);
    std::fprintf(stderr, "[dma] video blocks %llu units %llu (bulk %s)\n", g_nx_dma_blocks, g_nx_dma_block_units,
                 g_nx_dma_block ? "on" : "off");
}
void dump_words(const char* d);
void dump_at_exit() {
    gpu2d_stats_at_exit();
    std::fprintf(stderr, "[srnx] 3D busy reads %llu, skips %llu, halts %llu\n", g_srnx_gx_busy_reads, g_srnx_gx_busy_skips, g_srnx_gx_halts);
    svc_hist_print();
    shot_at_exit();
    if (!std::getenv("NR_DUMP_AT")) dump_words(std::getenv("NR_DUMP"));
}
void dump_words(const char* d) {
    if (!d) return;
    std::string s(d);
    size_t p = 0;
    while (p < s.size()) {
        size_t q = s.find(',', p); if (q == std::string::npos) q = s.size();
        const std::string item = s.substr(p, q - p);
        const size_t c = item.find(':');
        const uint32_t a = (uint32_t)std::strtoul(item.substr(0, c).c_str(), nullptr, 16);
        const uint32_t n = c == std::string::npos ? 4 : (uint32_t)std::strtoul(item.substr(c + 1).c_str(), nullptr, 0);
        std::fprintf(stderr, "[dump] %08X:", a);
        for (uint32_t i = 0; i < n; ++i) std::fprintf(stderr, " %08X", bus_read_u32_slow(a + 4 * i));
        std::fprintf(stderr, "\n");
        p = q + 1;
    }
}
struct StateStep { std::string path; unsigned long long at = 0; bool done = true; };
StateStep g_save_step, g_load_step;
StateStep parse_step(const char* v) {
    StateStep st;
    if (!v) return st;
    std::string s(v);
    const size_t at = s.find('@');
    st.path = s.substr(0, at);
    st.at = at == std::string::npos ? 0 : std::strtoull(s.substr(at + 1).c_str(), nullptr, 0);
    st.done = false;
    return st;
}
// SonicRush-nx scripted play:
//   NR_INPUT="at+len:KEYS,..."  at/len in millions of ARM9 cycles, KEYS joined
//                               with '|' from A B X Y L R START SELECT UP DOWN
//                               LEFT RIGHT (e.g. "3500+40:START,4200+600:RIGHT|A")
//   NR_SHOTS="prefix@a,b,c"     screenshots prefix-<a>.ppm ... at a, b, c million cycles
struct Press { unsigned long long at, end; uint32_t bits; int tx = -1, ty = -1; };   // TOUCHx.y: a held touch
std::vector<Press> g_presses;
std::string g_shot_prefix;
std::vector<unsigned long long> g_shots;
size_t g_next_shot = 0;
uint32_t g_last_mask = 0xFFFFFFFFu;
uint32_t key_bit(const std::string& k) {
    static const char* names[] = {"A","B","SELECT","START","RIGHT","LEFT","UP","DOWN","R","L"};
    for (int i = 0; i < 10; ++i) if (k == names[i]) return 1u << i;
    if (k == "X") return 1u << 16;
    if (k == "Y") return 1u << 17;
    std::fprintf(stderr, "[input] unknown key %s\n", k.c_str());
    return 0;
}
void parse_input(const char* v) {
    if (!v) return;
    std::string s(v); size_t p = 0;
    while (p < s.size()) {
        size_t q = s.find(',', p); if (q == std::string::npos) q = s.size();
        const std::string it = s.substr(p, q - p);
        const size_t plus = it.find('+'), colon = it.find(':');
        if (plus != std::string::npos && colon != std::string::npos) {
            Press pr{};
            pr.at = std::strtoull(it.substr(0, plus).c_str(), nullptr, 0) * 1000000ull;
            pr.end = pr.at + std::strtoull(it.substr(plus + 1, colon - plus - 1).c_str(), nullptr, 0) * 1000000ull;
            std::string ks = it.substr(colon + 1); size_t a = 0;
            while (a <= ks.size()) {
                size_t b = ks.find('|', a); if (b == std::string::npos) b = ks.size();
                const std::string kk = ks.substr(a, b - a);
                if (kk.rfind("TOUCH", 0) == 0) std::sscanf(kk.c_str() + 5, "%d.%d", &pr.tx, &pr.ty);
                else pr.bits |= key_bit(kk);
                a = b + 1;
            }
            g_presses.push_back(pr);
        }
        p = q + 1;
    }
}
void parse_shots(const char* v) {
    if (!v) return;
    std::string s(v); const size_t at = s.find('@');
    if (at == std::string::npos) return;
    g_shot_prefix = s.substr(0, at); size_t p = at + 1;
    while (p < s.size()) {
        size_t q = s.find(',', p); if (q == std::string::npos) q = s.size();
        g_shots.push_back(std::strtoull(s.substr(p, q - p).c_str(), nullptr, 0) * 1000000ull);
        p = q + 1;
    }
}
//   NR_RAMDUMP="prefix@a,b"     main RAM (4 MiB) to prefix-<a>.bin at a, b million cycles
std::string g_ram_prefix;
std::vector<unsigned long long> g_rams;
size_t g_next_ram = 0;
//   NR_POKE16="Mcyc@addr=val,addr=val"   from Mcyc on, keep these halfwords set
unsigned long long g_poke_from = ~0ull;
std::vector<std::pair<uint32_t, uint16_t>> g_pokes;
extern "C" void bus_write_u16_slow(uint32_t addr, uint16_t val);
void play_hook(uint64_t cyc) {
    static long long dump_at = -2;
    if (dump_at == -2) { const char* e = std::getenv("NR_DUMP_AT"); dump_at = e ? std::atoll(e) * 1000000ll : -1; }
    if (dump_at >= 0 && (long long)cyc >= dump_at) { dump_words(std::getenv("NR_DUMP")); dump_at = -1; }
    if (cyc >= g_poke_from)
        for (const auto& pk : g_pokes)
            if ((bus_read_u32_slow(pk.first & ~3u) >> ((pk.first & 2u) * 8u) & 0xFFFFu) != pk.second)
                bus_write_u16_slow(pk.first, pk.second);
    while (g_next_ram < g_rams.size() && cyc >= g_rams[g_next_ram]) {
        const std::string f = g_ram_prefix + "-" + std::to_string(g_rams[g_next_ram] / 1000000ull) + ".bin";
        if (std::FILE* o = std::fopen(f.c_str(), "wb")) {
            std::vector<uint32_t> buf(0x100000);
            for (uint32_t i = 0; i < 0x100000; ++i) buf[i] = bus_read_u32_slow(0x02000000u + 4u * i);
            std::fwrite(buf.data(), 4, buf.size(), o);
            std::fclose(o);
        }
        const std::string fo = g_ram_prefix + "-" + std::to_string(g_rams[g_next_ram] / 1000000ull) + ".oam";
        if (std::FILE* o = std::fopen(fo.c_str(), "wb")) {
            if (const uint8_t* oam = nds_vram_oam_live()) std::fwrite(oam, 1, 0x800, o);
            std::fclose(o);
        }
        // ARM9-view video state: BG space of both engines, palettes, 2D registers.
        const std::string stem = g_ram_prefix + "-" + std::to_string(g_rams[g_next_ram] / 1000000ull);
        for (int eng = 0; eng < 2; ++eng)
            if (std::FILE* o = std::fopen((stem + (eng ? ".bgb" : ".bga")).c_str(), "wb")) {
                const uint32_t n = eng ? 0x20000u : 0x80000u;
                for (uint32_t a = 0; a < n; a += 4) { uint32_t w = nds_vram_read_bg(eng, a, 4); std::fwrite(&w, 4, 1, o); }
                std::fclose(o);
            }
        for (int eng = 0; eng < 2; ++eng) {
            if (std::FILE* o = std::fopen((stem + (eng ? ".objb" : ".obja")).c_str(), "wb")) {
                const uint32_t n = eng ? 0x20000u : 0x40000u;
                for (uint32_t a = 0; a < n; a += 4) { uint32_t w = nds_vram_read_obj(eng, a, 4); std::fwrite(&w, 4, 1, o); }
                std::fclose(o);
            }
            if (std::FILE* o = std::fopen((stem + (eng ? ".xpb" : ".xpa")).c_str(), "wb")) {
                for (uint32_t a = 0; a < 0x8000u; a += 4) { uint32_t w = nds_vram_read_bg_extpal(eng, a, 4); std::fwrite(&w, 4, 1, o); }
                for (uint32_t a = 0; a < 0x2000u; a += 4) { uint32_t w = nds_vram_read_obj_extpal(eng, a, 4); std::fwrite(&w, 4, 1, o); }
                std::fclose(o);
            }
        }
        if (std::FILE* o = std::fopen((stem + ".spr").c_str(), "wb")) {
            // widescreen sprite records: per engine u32 count, u32 frames, then SrnxSprite[count]
            for (int e = 0; e < 2; ++e) {
                const SrnxSprite* sp = nullptr;
                const uint32_t n = srnx_spr_last(e, &sp), fr = srnx_spr_frames(e);
                std::fwrite(&n, 4, 1, o); std::fwrite(&fr, 4, 1, o);
                std::fwrite(sp, sizeof(SrnxSprite), n, o);
            }
            std::fclose(o);
            if (g_wide_have) {
                static uint32_t wide[kSrnxWideMaxW * kSrnxWideMaxH];
                SrnxWideParams pp = g_wide_p;
                if (std::getenv("NR_WIDE_NOPASTE")) pp.main_valid = 0;
                {
                    const auto t0 = std::chrono::steady_clock::now();
                    for (int rep = 0; rep < 10; ++rep) srnx_wide_draw_cpu(pp, g_wide_data.data(), wide, g_wide_p.view_w);
                    const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count() / 10;
                    static uint32_t ref[kSrnxWideMaxW * kSrnxWideMaxH];
                    const auto t1 = std::chrono::steady_clock::now();
                    srnx_wide_draw_ref(pp, g_wide_data.data(), ref, g_wide_p.view_w);
                    const double rms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t1).count();
                    size_t diff = 0;
                    for (int i = 0; i < g_wide_p.view_w * g_wide_p.view_h; ++i) diff += ((ref[i] ^ wide[i]) & 0xFFFFFF) != 0;
                    std::fprintf(stderr, "[srnx] wide CPU draw %dx%d: fast %.3f ms, reference %.2f ms, %zu pixels differ; snapshot %.3f ms avg\n",
                                 g_wide_p.view_w, g_wide_p.view_h, ms, rms, diff, g_snap_n ? g_snap_ms / g_snap_n : 0.0);
                    std::fprintf(stderr, "[srnx] snapshot parts per frame: head %.3f vram %.3f extpal %.3f layouts %.3f sprites+3d %.3f align %.3f tail %.3f\n", g_snap_part[6] / g_snap_n,
                                 g_snap_part[1] / g_snap_n, g_snap_part[2] / g_snap_n, g_snap_part[3] / g_snap_n, g_snap_part[4] / g_snap_n, g_snap_part[5] / g_snap_n, g_snap_part[7] / g_snap_n);
                    for (double& d : g_snap_part) d = 0;
                    g_snap_ms = 0; g_snap_n = 0;
                }
                if (std::getenv("NR_WIDE_DATA"))
                    if (std::FILE* m = std::fopen((stem + ".wdata").c_str(), "wb")) {
                        std::fwrite(g_wide_data.data(), 1, g_wide_data.size(), m);
                        std::fclose(m);
                    }
                if (const uint32_t* f3 = nds_gpu2d_srnx_3d_frame(nullptr))
                    if (std::FILE* m = std::fopen((stem + ".w3d").c_str(), "wb")) {
                        std::fwrite(f3, 4, 256 * 192, m);
                        std::fclose(m);
                    }
                if (std::FILE* m = std::fopen((stem + ".wds").c_str(), "wb")) {
                    std::fwrite(g_wide_ds, 4, 2 * 256 * 192, m);
                    std::fclose(m);
                }
                if (std::FILE* w = std::fopen((stem + ".wide").c_str(), "wb")) {
                    std::fwrite(&g_wide_p, sizeof(g_wide_p), 1, w);
                    std::fwrite(wide, 4, (size_t)g_wide_p.view_w * g_wide_p.view_h, w);
                    std::fclose(w);
                }
            } else {
                std::fprintf(stderr, "[srnx] wide view: not a stage\n");
            }
            srnx_alloc_report();
            std::fprintf(stderr, "[srnx] sprites main %u sub %u frames %u/%u mgr %08X/%08X overflow %u\n",
                         srnx_spr_last(0, &g_spr_tmp), srnx_spr_last(1, &g_spr_tmp),
                         srnx_spr_frames(0), srnx_spr_frames(1), srnx_spr_mgr(0), srnx_spr_mgr(1), g_srnx_spr_overflow);
        }
        if (std::FILE* o = std::fopen((stem + ".pal").c_str(), "wb")) {
            if (const uint8_t* pal = nds_vram_palette_live()) std::fwrite(pal, 1, 0x800, o);
            std::fclose(o);
        }
        if (std::FILE* o = std::fopen((stem + ".regs").c_str(), "wb")) {
            for (uint32_t base : {0x04000000u, 0x04001000u})
                for (uint32_t a = base; a < base + 0x70u; a += 4) { uint32_t w = nds_gpu2d_read(a, 4); std::fwrite(&w, 4, 1, o); }
            for (int e = 0; e < 2; ++e) {   // latched scroll/affine state: bgx[4] bgy[4] refx[2] refy[2] pabcd[8]
                uint16_t bx[4], by[4]; int32_t rx[2], ry[2]; int16_t pm[8];
                nds_gpu2d_debug_regs(e, bx, by, rx, ry, pm);
                std::fwrite(bx, 2, 4, o); std::fwrite(by, 2, 4, o); std::fwrite(rx, 4, 2, o); std::fwrite(ry, 4, 2, o); std::fwrite(pm, 2, 8, o);
            }
            std::fclose(o);
        }
        ++g_next_ram;
    }
    if (!g_presses.empty()) {
        uint32_t held = 0;
        int tx = -1, ty = -1;
        for (const Press& pr : g_presses) if (cyc >= pr.at && cyc < pr.end) { held |= pr.bits; if (pr.tx >= 0) { tx = pr.tx; ty = pr.ty; } }
        static bool touching = false;
        if (tx >= 0) { nds_set_touch((uint16_t)tx, (uint16_t)ty, true); touching = true; }
        else if (touching) { nds_set_touch(0, 0, false); touching = false; }
        const uint32_t mask = 0x000303FFu & ~held;
        {   // switch-test36: A presses ask for a robot homing attack (as on the Switch)
            static bool a_was = false; const bool a = held & 1u;
            if (a && !a_was) srnx_home_request();
            a_was = a;
        }
        if (mask != g_last_mask) {
            nds_set_key_mask(mask);
            std::fprintf(stderr, "[input] %llu Mcyc keys %05X\n", (unsigned long long)(cyc / 1000000ull), held);
            g_last_mask = mask;
        }
    }
    while (g_next_shot < g_shots.size() && cyc >= g_shots[g_next_shot]) {
        const std::string f = g_shot_prefix + "-" + std::to_string(g_shots[g_next_shot] / 1000000ull) + ".ppm";
        write_shot(f.c_str());
        ++g_next_shot;
    }
}
// our stage view at every VBlank start: latch the camera, then snapshot with
// the DS screens just drawn (as the Switch frontend does when it presents)
void wide_vblank() {
    srnx_wide_vblank();
    if (!g_srnx_wide) return;
    if (g_wide_data.empty()) g_wide_data.resize(kSrnxDataBytes);
    const auto t0 = std::chrono::steady_clock::now();
    host_own3d_tick();
    g_wide_have = srnx_wide_stage_active() &&
                  srnx_wide_snapshot(g_wide_p, g_wide_data.data(), nds_gpu2d_framebuffer(0), nds_gpu2d_framebuffer(1));
    g_snap_ms += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    ++g_snap_n;
    g_srnx_hide_hud = 1;
    if (g_wide_have) {   // the DS screens at this VBlank, for host checks
        for (int sc = 0; sc < 2; ++sc)
            if (const uint32_t* fb = nds_gpu2d_framebuffer(sc)) std::memcpy(g_wide_ds[sc], fb, sizeof(g_wide_ds[sc]));
    }
}
void round_hook(uint64_t cyc) {
    {   // switch-test35: NR_LVL=zone,act (1-based zone; act 1, 2, 3 = boss) arms the level select
        static bool done = false;
        if (!done) { done = true;
            if (const char* e = std::getenv("NR_LVL")) {
                int z = 0, a = 0;
                if (std::sscanf(e, "%d,%d", &z, &a) == 2) { g_srnx_lvl_zone = z - 1; g_srnx_lvl_act = a - 1; g_srnx_lvl_armed = 1; }
            }
        }
    }
    {   // switch-test31: NR_WIDE=1 builds the stage view once a frame (the
        // hook that used to do this was lost), for the NR_SHOT exit check
        static uint64_t wnext = 0;
        if (g_srnx_wide && cyc >= wnext) {
            wnext = cyc + 1120380ull; wide_vblank();
            static bool lastw = false;
            if (g_wide_have != lastw) { lastw = g_wide_have; std::fprintf(stderr, "[srnx] host stage view %s at %.0fM\n", lastw ? "on" : "off", cyc / 1e6); }
        }
    }
    // switch-test27 host check: NR_CODESCENE=1 runs the Switch's per-frame
    // screen identification once a frame (560190 system cycles = 1120380 ARM9
    // cycles) and logs each change
    {
        static int on = -1; static uint64_t next = 0; static int last = -1;
        if (on < 0) on = std::getenv("NR_CODESCENE") ? 1 : 0;
        if (on && cyc >= next) {
            next = cyc + 1120380ull;
            const int sc = srnx_code_frame();
            if (sc != last) { std::fprintf(stderr, "[codescene] %.0fM scene %d\n", cyc / 1e6, sc); last = sc; }
            // NR_CODELIST=1: every frame's list of overlay functions that ran
            static int cl = -1; if (cl < 0) cl = std::getenv("NR_CODELIST") ? 1 : 0;
            if (cl) {
                std::fprintf(stderr, "[codelist] %.0f", cyc / 1e6);
                for (int i = 0; i < g_srnx_codelist_n; ++i) std::fprintf(stderr, " %08X", g_srnx_codelist[i]);
                std::fprintf(stderr, "\n");
            }
        }
    }
    play_hook(cyc);

    const NdsSavestateIdentity id{"srnx-host", "bc617530bb9b6e4179f2e54d7816c38676812dde"};
    static unsigned waits = 0;
    if (!g_save_step.done && cyc >= g_save_step.at) {
        if (!nx_state_ready()) { ++waits; return; }
        std::string err;
        const bool ok = nx_state_save(g_save_step.path, id, &err);
        std::fprintf(stderr, "[state] save at %llu cycles (%u rounds waited): %s %s\n",
                     (unsigned long long)cyc, waits, ok ? "ok" : "FAILED", err.c_str());
        g_save_step.done = true;
    }
    if (!g_load_step.done && cyc >= g_load_step.at) {
        std::string err;
        const bool ok = nx_state_load(g_load_step.path, id, &err);
        std::fprintf(stderr, "[state] load at %llu cycles: %s %s\n", (unsigned long long)cyc,
                     ok ? "ok" : "FAILED", err.c_str());
        g_load_step.done = true;
    }
}
struct Install {
    Install() {
        const char* off = std::getenv("NR_DISABLE");
        if (!off || off[0] != '1') nr_install();
        if (const char* fc = std::getenv("NR_FASTCARD")) g_nx_fast_card = fc[0] == '1';
        if (std::getenv("NR_DMA_BLOCK_OFF")) g_nx_dma_block = false;
        std::atexit(dump_at_exit);
        g_save_step = parse_step(std::getenv("NR_STATE_SAVE"));
        g_load_step = parse_step(std::getenv("NR_STATE_LOAD"));
        if (std::getenv("SRNX_NOSKIP")) g_srnx_gx_skip = false;
        if (const char* w = std::getenv("NR_WIDE")) g_srnx_wide = w[0] == '1';
        {
          if (const char* w = std::getenv("NR_ACT_EXTRA")) g_srnx_act_extra = std::atoi(w);
          if (std::getenv("NR_ACT_OFF")) g_srnx_act_off = 1;
          if (const char* w = std::getenv("NR_ACT_DBG")) g_srnx_act_dbg = std::atoi(w);
          if (const char* w = std::getenv("NR_OBJ_DBG")) g_srnx_obj_dbg = std::atoi(w); }
        if (const char* w = std::getenv("NR_SPRLOG")) g_srnx_sprlog = std::atoi(w);
        if (const char* w = std::getenv("NR_WIDE_LAG")) g_srnx_cam_lag = std::atoi(w);
        if (const char* w = std::getenv("NR_WIDE_ZOOM")) g_srnx_zoom = std::atoi(w);
        g_srnx_vblank_hook = wide_vblank;
        if (const char* w = std::getenv("NR_WIDE_LRTB"))
            std::sscanf(w, "%d,%d,%d,%d", &g_srnx_wide_l, &g_srnx_wide_r, &g_srnx_wide_t, &g_srnx_wide_b);
        parse_input(std::getenv("NR_INPUT"));
        parse_shots(std::getenv("NR_SHOTS"));
        if (const char* v = std::getenv("NR_POKE16")) {
            std::string sv(v); const size_t at = sv.find('@');
            if (at != std::string::npos) {
                g_poke_from = std::strtoull(sv.substr(0, at).c_str(), nullptr, 0) * 1000000ull;
                size_t p = at + 1;
                while (p < sv.size()) {
                    size_t q = sv.find(',', p); if (q == std::string::npos) q = sv.size();
                    const std::string it = sv.substr(p, q - p); const size_t eq = it.find('=');
                    g_pokes.push_back({(uint32_t)std::strtoul(it.substr(0, eq).c_str(), nullptr, 16),
                                       (uint16_t)std::strtoul(it.substr(eq + 1).c_str(), nullptr, 0)});
                    p = q + 1;
                }
            }
        }
        if (const char* v = std::getenv("NR_RAMDUMP")) {
            std::string sv(v); const size_t at = sv.find('@');
            if (at != std::string::npos) {
                g_ram_prefix = sv.substr(0, at); size_t p = at + 1;
                while (p < sv.size()) {
                    size_t q = sv.find(',', p); if (q == std::string::npos) q = sv.size();
                    g_rams.push_back(std::strtoull(sv.substr(p, q - p).c_str(), nullptr, 0) * 1000000ull);
                    p = q + 1;
                }
            }
        }
        if (!g_save_step.done || !g_load_step.done || !g_presses.empty() || !g_shots.empty() || !g_rams.empty() || !g_pokes.empty() || std::getenv("NR_DUMP_AT"))
            g_nx_host_round_hook = round_hook;
    }
} g_install;
}
