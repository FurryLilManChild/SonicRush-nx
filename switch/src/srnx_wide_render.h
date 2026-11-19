// SonicRush-nx: our own stage view (see srnx_wide_render.cpp).
//
// The emulation thread takes a snapshot of everything the view needs at the
// end of each frame (srnx_wide_snapshot); the presenter draws it on the GPU
// (shaders/WideStage.glsl), or srnx_wide_draw_cpu draws the same thing on the
// CPU (host checks, and the fallback when the shader is missing).
#pragma once
#include <cstdint>

constexpr int kSrnxWideMaxW = 832;
constexpr int kSrnxWideMaxH = 464;

// View sizes in game pixels for the port menu's zoom setting.
struct SrnxZoom { int w, h; const char* name; };
constexpr SrnxZoom kSrnxZooms[4] = {
    {424, 240, "Close (424x240)"}, {512, 288, "Medium (512x288)"},
    {640, 360, "Far (640x360)"},   {825, 464, "Full (825x464)"}};
extern int g_srnx_zoom;            // index into kSrnxZooms
extern int g_srnx_cam_lag;         // which VBlank's camera frames the drawn DS screen

// Call at the start of every VBlank (latches the game's camera).
void srnx_wide_vblank();

// Snapshot layout. Params is a std140-compatible block of 32-bit fields (the
// shader's uniform block mirrors it); the data block holds byte offsets into
// one buffer of 32-bit words.
struct SrnxWideParams {
    int32_t view_w, view_h;        // view size in game pixels
    int32_t main_x, main_y;        // main engine screen (3D layer) top-left in view pixels
    uint32_t dispcnt;              // main engine DISPCNT
    uint32_t bgcnt[4];
    uint32_t order[4];             // BGs back to front (bg index, 4 = none)
    int32_t bg1_sx, bg1_sy;        // far background scroll at the view's top-left
    int32_t bg1_clamp;             // rows past the picture repeat its edge tile row instead of wrapping
    int32_t world_x, world_y;      // view's top-left in world pixels
    uint32_t layout_w[2], layout_h[2];
    uint32_t nspr;                 // world sprites, then HUD sprites
    uint32_t nhud;
    int32_t hud_dx, hud_dy;        // HUD pinning: added to right / bottom anchored items
    uint32_t bright;               // MASTER_BRIGHT
    uint32_t backdrop;             // 0xAARRGGBB
    uint32_t off_bg, off_obj, off_bgpal, off_objpal, off_xpal, off_lay0, off_lay1, off_blk, off_spr, off_main;
    uint32_t main_valid;
    uint32_t out_pitch;            // GPU output row length in pixels (set by the presenter)
    uint32_t off_v3d;              // view-size 3D layer drawn by our own projection (v3d_valid)
    uint32_t v3d_valid;
};

constexpr uint32_t kSrnxBgVram = 512 * 1024, kSrnxObjVram = 256 * 1024;
constexpr uint32_t kSrnxLayMax = 64 * 1024 * 2;        // layout bytes each (u16 block indices)
constexpr uint32_t kSrnxBlkMax = 2048 * 128;           // block data bytes
constexpr uint32_t kSrnxSprMax = 384;                  // sprites (world + HUD)
constexpr uint32_t kSrnxDataBytes = kSrnxBgVram + kSrnxObjVram + 512 + 512 + 2 * 8192 +
                                    2 * kSrnxLayMax + kSrnxBlkMax + kSrnxSprMax * 16 + 512 + 256 * 192 * 4 + 1024 * 512 * 4 + 4096;

// After the sprites (off_spr + kSrnxSprMax * 16): 32 OAM affine groups (PA, PB, PC, PD as
// s16, 8.8), then BLDCNT and BLDALPHA (u32 each), engine A.
constexpr uint32_t kSrnxAffOff = kSrnxSprMax * 16;
// Sprite entry in the data block: x, y (view pixels, top-left), attr0|attr1<<16, attr2.
struct SrnxWideSpr { int32_t x, y; uint32_t a01, a2; };

// True while a stage is on screen (our view applies).
bool srnx_wide_stage_active();
// Not on a stage (or the stage view is off): the 3D engine draws the DS screen again.
void srnx_wide_inactive();
// switch-test18: copy sprite memory again (one frame after the snapshot).
void srnx_wide_refresh_obj(const SrnxWideParams& p, uint8_t* data);
extern int g_srnx_obj_late;
extern int g_srnx_act_start, g_srnx_card_bottom;
// switch-test20: special stages. Detected from the two DS screens (the ring
// panel on the top screen, 3D track on the bottom), with hysteresis; drawn as
// the bottom screen at full height plus the ring count (left) and the
// progress bar (right, upright) in the side bars. 424x240.
// switch-test23: g_srnx_special is the one-screen mode outside stages:
// 0 none, 1 special stage, 2 boss fight (top screen, health gauge upright in
// the right bar), 3 top screen, 4 bottom screen, 5 special stage intro
// (bottom screen), 6 emerald result (bottom screen, banner, emerald ring).
// 7 both screens shrunk (the fallback), 8 logos, 9 title, 10 main menu, 11 world map.
// switch-test26: never stacked; an unrecognised screen is 7.
extern int g_srnx_special;
int srnx_special_detect(const uint32_t* top, const uint32_t* bottom, bool main_on_top, int code_scene);
// switch-test27: once a frame, which overlay functions ran (g_srnx_codelist,
// address | 0x80000000 for overlay 1) and the screen they identify (8 logos,
// 9 title, 10 main menu, 0 not known by code).
int srnx_code_frame();
extern uint32_t g_srnx_codelist[1800];
extern int g_srnx_codelist_n;
// switch-test22: the numbered circle the game highlights next during a
// special-stage jump (its 0x0871FB fill), in DS bottom-screen pixels. Kept for
// 30 frames through the highlight's blink. False when there is none.
bool srnx_special_next_circle(int* x, int* y);
void srnx_special_compose(int mode, const uint32_t* top, const uint32_t* bottom, uint32_t* out, int pitch);
extern int g_srnx_autocap;
extern unsigned g_srnx_cam_swaps;
// End of frame, emulation thread: update the camera and fill params + data
// (data must hold kSrnxDataBytes). top/bottom are the DS screens as drawn.
bool srnx_wide_snapshot(SrnxWideParams& p, uint8_t* data, const uint32_t* top, const uint32_t* bottom);
// CPU drawing of a snapshot into out (p.view_w x p.view_h, 0xAARRGGBB, pitch in pixels).
// Reference drawing, one pixel at a time (host checks).
void srnx_wide_draw_ref(const SrnxWideParams& p, const uint8_t* data, uint32_t* out, int pitch);
void srnx_wide_draw_cpu(const SrnxWideParams& p, const uint8_t* data, uint32_t* out, int pitch);
