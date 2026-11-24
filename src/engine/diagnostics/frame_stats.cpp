#include "frame_stats.h"

#include <algorithm>
#include <cfloat>
#include <cstring>
#include <list>

#include <bgfx/bgfx.h>

#include "engine/core/engine_globals.h"
#include "engine/ecs/world.h"
#include "engine/time/time.h"

namespace Diagnostics {

constexpr size_t kHistory = 240;  // frames

// A section's milliseconds by name within the section it runs in; one that
// didn't run in the interval isn't published
struct SectionTotal {
  const char* name;
  SectionTotal* parent;
  int depth;
  double frame_ms = 0.0;  // this frame so far
  double ms = 0.0;        // over the interval
  double max_ms = 0.0;
  bool ran = false;
  std::vector<float> history = std::vector<float>(kHistory);
};

namespace {

constexpr float kPublishInterval = 0.25f;  // seconds (4x per second)

FrameStatsSnapshot g_snapshot;

// Accumulators for the current interval
float g_elapsed = 0.0f;
float g_sum_ms = 0.0f;
float g_min_ms = FLT_MAX;
float g_max_ms = 0.0f;
uint32_t g_frames = 0;
// Every one after the section it runs in (a list, so open ones stay put)
std::list<SectionTotal> g_sections;
SectionTotal* g_open = nullptr;
// Rings of the last frames, written at g_head
std::vector<float> g_frame_history(kHistory);
size_t g_head = 0;

// The render thread's work, read from bgfx each frame
std::vector<SectionTotal> g_render = {
    {"Render thread", nullptr, 0},
    {"Waited for it", nullptr, 0},
    {"Buffers added", nullptr, 0},
    {"Textures added", nullptr, 0}};

std::vector<float> OldestFirst(const std::vector<float>& ring) {
  std::vector<float> ordered(ring.begin() + g_head, ring.end());
  ordered.insert(ordered.end(), ring.begin(), ring.begin() + g_head);
  return ordered;
}

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

// The bgfx frame's render thread times and the buffers and textures it has
// more of than the frame before
void ReadRenderThread() {
  const bgfx::Stats* stats = bgfx::getStats();
  if (!stats || stats->cpuTimerFreq <= 0)
    return;
  static int buffers = -1, textures = -1;
  const int now_buffers =
      stats->numVertexBuffers + stats->numIndexBuffers +
      stats->numDynamicVertexBuffers + stats->numDynamicIndexBuffers;
  const double to_ms = 1000.0 / static_cast<double>(stats->cpuTimerFreq);
  g_render[0].frame_ms =
      static_cast<double>(stats->cpuTimeEnd - stats->cpuTimeBegin) * to_ms;
  g_render[1].frame_ms = static_cast<double>(stats->waitRender) * to_ms;
  g_render[2].frame_ms = buffers < 0 ? 0 : std::max(now_buffers - buffers, 0);
  g_render[3].frame_ms =
      textures < 0 ? 0 : std::max(stats->numTextures - textures, 0);
  buffers = now_buffers;
  textures = stats->numTextures;
  for (SectionTotal& row : g_render)
    row.ran = true;
}

// A frame's time into the interval's and the history
void Roll(SectionTotal& section) {
  section.ms += section.frame_ms;
  section.max_ms = std::max(section.max_ms, section.frame_ms);
  section.history[g_head] = static_cast<float>(section.frame_ms);
  section.frame_ms = 0.0;
}

// The interval's per frame into `to` if it ran, and a new interval
void Publish(SectionTotal& section, std::vector<FrameStatsSnapshot::Section>& to) {
  if (section.ran)
    to.push_back(
        {section.name, section.depth,
         static_cast<float>(section.ms / static_cast<double>(g_frames)),
         static_cast<float>(section.max_ms), OldestFirst(section.history)});
  section.ms = section.max_ms = 0.0;
  section.ran = false;
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

  g_frame_history[g_head] = ms;
  ReadRenderThread();
  for (SectionTotal& section : g_sections)
    Roll(section);
  for (SectionTotal& row : g_render)
    Roll(row);
  g_head = (g_head + 1) % kHistory;

  if (g_elapsed < kPublishInterval)
    return;

  // Publish
  g_snapshot.frame_ms_avg = g_sum_ms / static_cast<float>(g_frames);
  g_snapshot.frame_ms_min = g_min_ms;
  g_snapshot.frame_ms_max = g_max_ms;
  g_snapshot.fps = 1000.0f / g_snapshot.frame_ms_avg;

  g_snapshot.frame_history = OldestFirst(g_frame_history);
  g_snapshot.sections.clear();
  for (SectionTotal& section : g_sections)
    Publish(section, g_snapshot.sections);
  g_snapshot.render_thread.clear();
  for (SectionTotal& row : g_render)
    Publish(row, g_snapshot.render_thread);

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

ScopedSection::ScopedSection(const char* name) {
  auto it = std::find_if(
      g_sections.begin(), g_sections.end(), [&](const SectionTotal& section) {
        return section.parent == g_open && std::strcmp(section.name, name) == 0;
      });
  if (it == g_sections.end()) {
    // Last of the open section's
    auto at = g_sections.end();
    if (g_open) {
      at = std::next(std::find_if(
          g_sections.begin(), g_sections.end(),
          [](const SectionTotal& section) { return &section == g_open; }));
      while (at != g_sections.end() && at->depth > g_open->depth)
        ++at;
    }
    it = g_sections.insert(
        at, SectionTotal{name, g_open, g_open ? g_open->depth + 1 : 0});
  }
  total_ = &*it;
  g_open = total_;
  start_ = std::chrono::steady_clock::now();
}

ScopedSection::~ScopedSection() {
  total_->frame_ms += std::chrono::duration<double, std::milli>(
                          std::chrono::steady_clock::now() - start_)
                          .count();
  total_->ran = true;
  g_open = total_->parent;
}

}  // namespace Diagnostics