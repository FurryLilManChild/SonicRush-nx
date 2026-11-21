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
void srnx_act_debug(uint32_t sp, uint32_t r0, uint32_t r1);
extern int g_srnx_act_dbg;
extern int g_srnx_keep_margin;
void srnx_send_debug(uint32_t src, uint32_t dst, uint32_t bytes, uint32_t lr);
extern unsigned long long g_srnx_send_calls[4], g_srnx_send_words[4];
extern int g_srnx_obj_dbg;
void srnx_obj_debug(int kind, uint32_t obj);
extern int g_srnx_act_l, g_srnx_act_r, g_srnx_act_t, g_srnx_act_b;   // object activation widening (pixels)
extern int g_srnx_hide_hud;     // wide view: keep HUD sprites out of the DS picture

int srnx_spr_clip(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t mgr);
int srnx_spr_box(int32_t x0, int32_t x1, int32_t y0, int32_t y1, uint32_t mgr);
// Records the sprite and returns the y to put in OAM (pushed off the DS
// screen when the sprite is only in the wide view).
uint32_t srnx_spr_record(uint32_t slot, uint32_t x, uint32_t y, uint32_t mgr);
void srnx_spr_ctx(int hud);
void srnx_spr_enter(int routine, uint32_t lr);
int srnx_is_hud_caller(int routine, uint32_t lr, uint32_t lr_multi);
extern int g_srnx_sprlog;
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

// switch-test15: overlay 0 camera-record swap finished (srnx_wide_render.cpp)
#ifdef __cplusplus
extern "C"
#endif
void srnx_wide_cam_swapped(void);
// switch-test35: level select. The port menu arms a zone and act; the next
// time the game starts a stage (main 0x0205247C: r0 zone 0..6, r1 act 0, 1,
// 2 = boss) it starts the armed one instead. Returns 1 when it replaced them.
#ifdef __cplusplus
extern "C"
#endif
int srnx_lvl_take(uint32_t* zone, uint32_t* act);
// level select stage 3: start the zone's special stage (srnx.hooks @020524BC)
#ifdef __cplusplus
extern "C" {
#endif
int srnx_special_take(void);
int srnx_special_end(void);
// special stage robot homing (A or B), srnx.hooks @ov01:0231227C/02310214/023122A0
void srnx_home_request(void);
int srnx_home_pending(void);
void srnx_home_tick(void);
void srnx_home_done(uint32_t robot);
#ifdef __cplusplus
}
#endif
