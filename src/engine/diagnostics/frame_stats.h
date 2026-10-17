#ifndef FRAME_STATS_H
#define FRAME_STATS_H

#include <cstdint>

//---------------------------------------------------------------------------
// FRAME STATS
// Engine-side frame diagnostics: timing, renderer counters, scene size.
// Sampled every frame, published as a snapshot a few times per second so
// values stay readable. No editor or UI dependencies.
//---------------------------------------------------------------------------
namespace Diagnostics {

struct FrameStatsSnapshot {
  float fps = 0.0f;
  float frame_ms_avg = 0.0f;  // over the last publish interval
  float frame_ms_min = 0.0f;
  float frame_ms_max = 0.0f;
  float cpu_ms = 0.0f;  // bgfx CPU frame time
  float gpu_ms = 0.0f;  // bgfx GPU frame time, 0 if the backend has no timers
  uint32_t draw_calls = 0;  // all views, including the editor UI
  uint32_t triangles = 0;
  uint32_t entities = 0;  // entities with a TransformComponent
  uint16_t viewport_w = 0;
  uint16_t viewport_h = 0;
};

// Call once per frame
void UpdateStats();

// Latest published values (refreshed every publish interval)
const FrameStatsSnapshot& Snapshot();

}  // namespace Diagnostics

#endif  // FRAME_STATS_H