#ifndef FRAME_BUDGET_H
#define FRAME_BUDGET_H

// Main-thread time a frame gives to making what workers generated (entities,
// mesh uploads), shared by every system that does so: with several
// generators streaming, the frame still spends the budget once, not once each
namespace FrameBudget {

// Starts a frame's budget; once a frame, before the systems spend it
void Begin();

// Milliseconds a frame may spend
float Total();
void SetTotal(float ms);

// Times the work in its scope against the frame's budget; one at a time
class Spend {
public:
  Spend();
  ~Spend();
  Spend(const Spend&) = delete;
  Spend& operator=(const Spend&) = delete;
};

// Milliseconds left this frame, counting the open Spend
float Left();
// Whether any is left
inline bool More() {
  return Left() > 0.0f;
}

}  // namespace FrameBudget

#endif  // FRAME_BUDGET_H
