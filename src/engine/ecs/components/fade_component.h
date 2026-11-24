#ifndef FADE_COMPONENT_H
#define FADE_COMPONENT_H

// How much of an entity and its children is shown, dithered (screen-door) by
// the standard shaders: 1 whole, 0 hidden. FadeSystem moves visibility_
// toward target_, so setting target_ fades the entity in or out, e.g. as it
// spawns or before it is removed
struct FadeComponent {
  float visibility_{1.0f};
  float target_{1.0f};
  // Seconds a full fade (0 to 1 or back) takes; 0: at once
  float duration_{0.5f};
  // Destroy the entity once it has faded out
  bool destroy_when_hidden_{false};
};

#endif  // FADE_COMPONENT_H
