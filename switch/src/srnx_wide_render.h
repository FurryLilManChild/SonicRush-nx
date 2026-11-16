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
    uint32_t pad[2];
};

constexpr uint32_t kSrnxBgVram = 512 * 1024, kSrnxObjVram = 256 * 1024;
constexpr uint32_t kSrnxLayMax = 64 * 1024 * 2;        // layout bytes each (u16 block indices)
constexpr uint32_t kSrnxBlkMax = 2048 * 128;           // block data bytes
constexpr uint32_t kSrnxSprMax = 384;                  // sprites (world + HUD)
constexpr uint32_t kSrnxDataBytes = kSrnxBgVram + kSrnxObjVram + 512 + 512 + 2 * 8192 +
                                    2 * kSrnxLayMax + kSrnxBlkMax + kSrnxSprMax * 16 + 256 * 192 * 4 + 4096;

// Sprite entry in the data block: x, y (view pixels, top-left), attr0|attr1<<16, attr2.
struct SrnxWideSpr { int32_t x, y; uint32_t a01, a2; };

// True while a stage is on screen (our view applies).
bool srnx_wide_stage_active();
// End of frame, emulation thread: update the camera and fill params + data
// (data must hold kSrnxDataBytes). top/bottom are the DS screens as drawn.
bool srnx_wide_snapshot(SrnxWideParams& p, uint8_t* data, const uint32_t* top, const uint32_t* bottom);
// CPU drawing of a snapshot into out (p.view_w x p.view_h, 0xAARRGGBB, pitch in pixels).
// Reference drawing, one pixel at a time (host checks).
void srnx_wide_draw_ref(const SrnxWideParams& p, const uint8_t* data, uint32_t* out, int pitch);
void srnx_wide_draw_cpu(const SrnxWideParams& p, const uint8_t* data, uint32_t* out, int pitch);
