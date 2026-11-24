#ifndef FRAME_STATS_H
#define FRAME_STATS_H

#include <chrono>
#include <cstdint>
#include <vector>

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
  // The last frames' times, oldest first
  std::vector<float> frame_history;
  // Each named part of the frame (ScopedSection) that ran during the
  // interval, every one after the section it ran in, `depth` deep
  struct Section {
    const char* name;
    int depth;
    float ms;      // per frame
    float max_ms;  // its slowest frame
    std::vector<float> history;  // as frame_history
  };
  std::vector<Section> sections;
  // bgfx's render thread as sections: its time issuing the frame, the main
  // thread's wait for it in bgfx::frame, and the buffers and textures added
  // (counts, not ms)
  std::vector<Section> render_thread;
};

struct SectionTotal;

// Times the main thread's work in its scope as part of the frame named
// `name` (a string literal), within the section open around it; a name's
// times add up within a frame. Systems time only the frames they have work
// in, so the stats list only the systems a project uses
class ScopedSection {
public:
  explicit ScopedSection(const char* name);
  ~ScopedSection();
  ScopedSection(const ScopedSection&) = delete;
  ScopedSection& operator=(const ScopedSection&) = delete;

private:
  SectionTotal* total_;
  std::chrono::steady_clock::time_point start_;
};

// Call once per frame
void UpdateStats();

// Latest published values (refreshed every publish interval)
const FrameStatsSnapshot& GetFrameStats();

}  // namespace Diagnostics

#endif  // FRAME_STATS_H