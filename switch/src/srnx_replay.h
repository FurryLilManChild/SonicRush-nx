// SonicRush-nx switch-test16: instant replay for debugging.
//
// While the stage view is on, the last ~2 seconds are kept in memory: per
// frame the two DS screens, our own 3D layer and the sprite list (game
// thread), and the drawn stage view (presenter thread), matched by a frame
// serial. L3 writes the whole ring to captures/rpl-NNN.bin; the game pauses
// while it saves. tools/widescreen/replay.py turns a file into PNGs.
#pragma once
#include <cstdint>

struct SrnxWideParams;

// Game thread, each stage frame after the snapshot: record and tag the frame.
void srnx_replay_record(SrnxWideParams& p, uint8_t* data, const uint32_t* top, const uint32_t* bottom);
// Presenter thread, after drawing: keep the stage view of that frame.
void srnx_replay_view(const SrnxWideParams& p, const uint8_t* data, const uint32_t* view, int pitch);
// Game thread: save the ring. Returns the file number, or 0 on failure / limit.
int srnx_replay_save(char* msg, int msg_len);
// Not on a stage: drop the ring (the next stage starts clean).
void srnx_replay_reset();
