// SonicRush-nx: wide stage view built on the CPU (see srnx_wide_render.cpp).
#pragma once
#include <cstdint>

constexpr int kSrnxWideMaxW = 832;
constexpr int kSrnxWideMaxH = 464;

extern int g_srnx_wide_w, g_srnx_wide_h;   // wide view size (default 825 x 464)

// True while a stage with level layouts is on screen (the wide view applies).
bool srnx_wide_stage_active();
// Builds the wide view into out (g_srnx_wide_w x g_srnx_wide_h, 0xAARRGGBB)
// from the game's current state, then pastes the two DS screens (256 x 192
// each) into their windows. False: not available, show the normal layout.
bool srnx_wide_render(uint32_t* out, const uint32_t* top, const uint32_t* bottom);
