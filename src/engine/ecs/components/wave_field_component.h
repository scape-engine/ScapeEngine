#ifndef WAVE_FIELD_COMPONENT_H
#define WAVE_FIELD_COMPONENT_H

#include <memory>

#include "water/wave_cascades.h"
#include "water/water_waves.h"

// The waves of a water surface (a planet's sea, a lake, a flat sea scene)
// and how they are split into cascades. Water::Subsystem moves them on and
// fills the entity's compute buffers, shader params and transform: the
// surface lies on the parent's sphere of `radius`, or on its y = 0 plane when
// the radius is 0, under the viewer
struct WaveFieldComponent {
  // The states' waves and foam (content); null: no waves
  std::shared_ptr<const Worldgen::WaterWaveSettings> waves;
  Worldgen::WaveCascadeSettings cascades;
  float radius = 0.0f;
  // TODO: the terrain's depth under the surface, from a depth map
  float depth = 180.0f;
  Worldgen::WaveField field;  // state
};

#endif  // WAVE_FIELD_COMPONENT_H
