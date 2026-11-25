#ifndef PENDING_MODEL_COMPONENT_H
#define PENDING_MODEL_COMPONENT_H

#include <string>

/* ────────────────────────────── PENDING MODEL ─────────────────────────── */
// A model added to the scene while it loads in the background. Once loaded,
// ModelEntityFactory gives the entity its mesh renderers (one child per mesh
// when there are several) and removes this component
struct PendingModelComponent {
  // Project-relative model path
  std::string source_;
};

#endif  // PENDING_MODEL_COMPONENT_H
