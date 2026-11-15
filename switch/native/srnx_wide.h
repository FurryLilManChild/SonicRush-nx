// Sonic Rush widescreen sprite hooks (see srnx_wide.cpp).
#pragma once
#include <stdint.h>

#define SRNX_MAX_SPR 256

#ifdef __cplusplus
extern "C" {
#endif

typedef struct SrnxSprite {
    int32_t x, y;             // camera-relative position, 20.12 (main engine: top screen camera)
    uint16_t slot;            // OAM slot index; bit 15 set: HUD sprite (screen-fixed)
    uint16_t attr0, attr1, attr2;
} SrnxSprite;

extern int g_srnx_wide;       // wide view active: hooks widen the clip
extern int g_srnx_wide_l, g_srnx_wide_r, g_srnx_wide_t, g_srnx_wide_b;   // extra pixels around the 256x192 view
extern uint32_t g_srnx_spr_overflow;
extern int g_srnx_hide_hud;     // wide view: keep HUD sprites out of the DS picture

int srnx_spr_clip(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t mgr);
int srnx_spr_box(int32_t x0, int32_t x1, int32_t y0, int32_t y1, uint32_t mgr);
// Records the sprite and returns the y to put in OAM (pushed off the DS
// screen when the sprite is only in the wide view).
uint32_t srnx_spr_record(uint32_t slot, uint32_t x, uint32_t y, uint32_t mgr);
void srnx_spr_ctx(int hud);
uint32_t srnx_spr_last(int engine, const SrnxSprite** out);
uint32_t srnx_spr_frames(int engine);
// camera (24.8) the last whole frame's sprites were placed by
void srnx_spr_last_cam(int engine, int32_t* x, int32_t* y);
uint32_t srnx_spr_mgr(int engine);
void srnx_alloc_count(uint32_t lr, uint32_t mgr);
void srnx_alloc_report(void);

#ifdef __cplusplus
}
#endif
