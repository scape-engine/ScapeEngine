#ifndef ACTIVATION_COMPONENT_H
#define ACTIVATION_COMPONENT_H

#include <cstdint>

// Whether the entity's system does its full work (a generator streams, an
// actor thinks) or a cheap stand-in: active while a viewer is within
// `distance` of it. Activation (engine/paging/activation.h) decides; the
// entity's own system reads active_
struct ActivationComponent {
  // From the viewer to the entity's extent; 0: always active
  float distance = 0.0f;
  // At most this many of the parent's children with the component are
  // active, the nearest; 0: no cap
  uint32_t max_active = 0;
  // Its radius round its position, written by its system (a planet's
  // radius); distances are to it
  float extent = 0.0f;

  // ─────────── State ───────────
  bool active_ = false;
};

#endif  // ACTIVATION_COMPONENT_H
