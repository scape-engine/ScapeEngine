#include "frame_stats.h"

#include <algorithm>
#include <cfloat>

#include <bgfx/bgfx.h>

#include "engine/core/engine_globals.h"
#include "engine/core/view_ids.h"
#include "engine/ecs/world.h"
#include "engine/time/time.h"

namespace Diagnostics {

namespace {

constexpr float kPublishInterval = 0.25f;  // seconds (4x per second)

FrameStatsSnapshot g_snapshot;
bool g_pass_timings = false;

// Render views grouped into passes, by view id range [first, last]
struct PassViews {
  const char* name;
  uint16_t first;
  uint16_t last;
};

constexpr PassViews kPasses[] = {
    {"Shadows", ViewID::SHADOW_PASS, ViewID::SHADOW_PASS},
    {"Sky light", ViewID::SKY_CAPTURE_BASE, ViewID::PRE_PASS - 1},
    {"Pre-pass", ViewID::PRE_PASS, ViewID::PRE_PASS},
    {"SSAO", ViewID::SSAO, ViewID::SSAO_BLUR},
    {"Scene", ViewID::SCENE_TERRAIN, ViewID::DEBUG_PASS},
    {"Forward", ViewID::SCENE_FORWARD, ViewID::SCENE_SKYBOX},
    {"Previews", ViewID::PREVIEW_PASS_BASE, ViewID::PREVIEW_PASS_MAX},
    {"UI", ViewID::UI, ViewID::UI},
};
static_assert(sizeof(kPasses) / sizeof(kPasses[0]) <= kMaxPassTimings);

void ReadPassTimings(const bgfx::Stats& stats, FrameStatsSnapshot& snap) {
  snap.pass_count = 0;
  if (!g_pass_timings)
    return;

  for (const PassViews& pass : kPasses) {
    PassTiming& timing = snap.passes[snap.pass_count++];
    timing = PassTiming{pass.name, 0.0f, 0.0f};

    for (uint16_t i = 0; i < stats.numViews; ++i) {
      const bgfx::ViewStats& view = stats.viewStats[i];
      if (view.view < pass.first || view.view > pass.last)
        continue;
      if (stats.gpuTimerFreq > 0 && view.gpuTimeEnd > view.gpuTimeBegin)
        timing.gpu_ms += static_cast<float>(
            double(view.gpuTimeEnd - view.gpuTimeBegin) * 1000.0 /
            double(stats.gpuTimerFreq));
      if (stats.cpuTimerFreq > 0 && view.cpuTimeEnd > view.cpuTimeBegin)
        timing.cpu_ms += static_cast<float>(
            double(view.cpuTimeEnd - view.cpuTimeBegin) * 1000.0 /
            double(stats.cpuTimerFreq));
    }
  }
}

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

  ReadPassTimings(*stats, snap);
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

void SetPassTimingsEnabled(bool enabled) {
  if (enabled == g_pass_timings)
    return;
  g_pass_timings = enabled;
  bgfx::setDebug(enabled ? BGFX_DEBUG_PROFILER : BGFX_DEBUG_NONE);
}

const FrameStatsSnapshot& GetFrameStats() {
  return g_snapshot;
}

}  // namespace Diagnostics