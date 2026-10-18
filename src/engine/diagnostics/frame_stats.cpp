#include "frame_stats.h"

#include <algorithm>
#include <cfloat>

#include <bgfx/bgfx.h>

#include "engine/core/engine_globals.h"
#include "engine/ecs/world.h"
#include "engine/time/time.h"

namespace Diagnostics {

namespace {

constexpr float kPublishInterval = 0.25f;  // seconds (4x per second)

FrameStatsSnapshot g_snapshot;

// Accumulators for the current interval
float g_elapsed = 0.0f;
float g_sum_ms = 0.0f;
float g_min_ms = FLT_MAX;
float g_max_ms = 0.0f;
uint32_t g_frames = 0;

void ReadRendererStats(FrameStatsSnapshot& snap) {
  const bgfx::Stats* stats = bgfx::getStats();
  if (!stats)
    return;

  if (stats->cpuTimerFreq > 0)
    snap.cpu_ms = static_cast<float>(double(stats->cpuTimeFrame) * 1000.0 /
                                     double(stats->cpuTimerFreq));

  if (stats->gpuTimerFreq > 0 && stats->gpuTimeEnd > stats->gpuTimeBegin)
    snap.gpu_ms =
        static_cast<float>(double(stats->gpuTimeEnd - stats->gpuTimeBegin) *
                           1000.0 / double(stats->gpuTimerFreq));

  snap.draw_calls = stats->numDraw;
  snap.triangles = stats->numPrims[bgfx::Topology::TriList] +
                   stats->numPrims[bgfx::Topology::TriStrip];
}

}  // namespace

void UpdateStats() {
  const float dt = Time::UnscaledDeltaf();
  if (dt <= 0.0f)
    return;

  const float ms = dt * 1000.0f;
  g_sum_ms += ms;
  g_min_ms = std::min(g_min_ms, ms);
  g_max_ms = std::max(g_max_ms, ms);
  g_frames++;
  g_elapsed += dt;

  if (g_elapsed < kPublishInterval)
    return;

  // Publish
  g_snapshot.frame_ms_avg = g_sum_ms / static_cast<float>(g_frames);
  g_snapshot.frame_ms_min = g_min_ms;
  g_snapshot.frame_ms_max = g_max_ms;
  g_snapshot.fps = 1000.0f / g_snapshot.frame_ms_avg;

  ReadRendererStats(g_snapshot);
  g_snapshot.entities =
      static_cast<uint32_t>(ECS::Main().View<TransformComponent>().size());
  g_snapshot.viewport_w = canvasViewportW;
  g_snapshot.viewport_h = canvasViewportH;

  // Reset the interval
  g_elapsed = 0.0f;
  g_sum_ms = 0.0f;
  g_min_ms = FLT_MAX;
  g_max_ms = 0.0f;
  g_frames = 0;
}

const FrameStatsSnapshot& GetFrameStats() {
  return g_snapshot;
}

}  // namespace Diagnostics