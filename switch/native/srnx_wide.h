// Sonic Rush widescreen sprite hooks (see srnx_wide.cpp).
#pragma once
#include <stdint.h>

#define SRNX_MAX_SPR 256

#ifdef __cplusplus
extern "C" {
#endif

typedef struct SrnxSprite {
    int32_t x, y;             // camera-relative position, 20.12 (main engine: top screen camera)
    uint16_t slot;            // OAM slot index
    uint16_t attr0, attr1, attr2;
} SrnxSprite;

extern int g_srnx_wide;       // wide view active: hooks widen the clip
extern int g_srnx_wide_l, g_srnx_wide_r, g_srnx_wide_t, g_srnx_wide_b;   // extra pixels around the 256x192 view
extern uint32_t g_srnx_spr_overflow;

int srnx_spr_clip(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t mgr);
int srnx_spr_box(int32_t x0, int32_t x1, int32_t y0, int32_t y1, uint32_t mgr);
void srnx_spr_record(uint32_t slot, uint32_t x, uint32_t y, uint32_t mgr);
uint32_t srnx_spr_last(int engine, const SrnxSprite** out);
uint32_t srnx_spr_frames(int engine);
uint32_t srnx_spr_mgr(int engine);
void srnx_alloc_count(uint32_t lr, uint32_t mgr);
void srnx_alloc_report(void);

#ifdef __cplusplus
}
#endif
