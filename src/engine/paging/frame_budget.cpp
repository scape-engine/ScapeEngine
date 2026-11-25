#include "frame_budget.h"

#include <cassert>
#include <chrono>
#include <optional>

namespace FrameBudget {

using Clock = std::chrono::steady_clock;

static float s_total_ms = 2.0f;
static float s_spent_ms = 0.0f;
static std::optional<Clock::time_point> s_open;

static float _Since(Clock::time_point start) {
  return std::chrono::duration<float, std::milli>(Clock::now() - start).count();
}

void Begin() {
  s_spent_ms = 0.0f;
}

float Total() {
  return s_total_ms;
}

void SetTotal(float ms) {
  s_total_ms = ms;
}

Spend::Spend() {
  assert(!s_open && "FrameBudget::Spend scopes don't nest");
  s_open = Clock::now();
}

Spend::~Spend() {
  s_spent_ms += _Since(*s_open);
  s_open.reset();
}

float Left() {
  return s_total_ms - s_spent_ms - (s_open ? _Since(*s_open) : 0.0f);
}

}  // namespace FrameBudget
