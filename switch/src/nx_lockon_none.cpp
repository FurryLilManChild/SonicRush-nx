// SonicRush-nx: MPH-nx's lock-on reads and steers Hunters' game memory
// (player, entity list, camera addresses). It must never run against Sonic
// Rush, so every hook is inert here.
#include "nx_lockon.h"
#include <cstdio>
bool g_nx_lockon = false;
bool g_nx_lockon_debug = false;
NxLockOut nx_lockon_frame(bool) { return NxLockOut{}; }
void nx_lockon_stats(char* buf, int n) { if (buf && n > 0) std::snprintf(buf, n, "off"); }
