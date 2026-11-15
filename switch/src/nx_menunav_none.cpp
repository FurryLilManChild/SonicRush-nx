// SonicRush-nx: MPH-nx's touch-screen menu recognition is Hunters-only (its
// screen fingerprints and touch targets are Hunters' menus). Sonic Rush's
// menus already work with the D-pad and A/B, so every hook is inert here.
#include "nx_menunav.h"
bool g_nx_menunav = false;
void nx_menunav_detect(const uint32_t*, int) {}
bool nx_menunav_owns_pad() { return false; }
bool nx_menunav_owns_a() { return false; }
int nx_menunav_mode() { return kNxModeUnknown; }
bool nx_menunav_special_filled() { return false; }
bool nx_menunav_weapon_arc() { return false; }
bool nx_menunav_owns_weapon_dpad() { return false; }
NxNavOut nx_menunav_input(const NxNavPad&) { return NxNavOut{}; }
bool nx_menunav_draw(const uint32_t*, uint32_t*, int) { return false; }
