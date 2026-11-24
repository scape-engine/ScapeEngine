#ifndef PROFILER_WINDOW_H
#define PROFILER_WINDOW_H

#include "editor/gui/window_base.h"

//---------------------------------------------------------------------------
// PROFILER WINDOW
// - The last frames' times, and each part of the frame the engine's systems
//   time (Diagnostics::ScopedSection): only those that ran, under the ones
//   they ran in, with what no section timed
//---------------------------------------------------------------------------
class ProfilerWindow : public WindowBase {
public:
  void Render() override;
};

#endif  // PROFILER_WINDOW_H
