#ifndef COMPUTE_JOB_COMPONENT_H
#define COMPUTE_JOB_COMPONENT_H

#include <cstdint>
#include <string>
#include <vector>

// Compute shaders the compute system runs every frame for an entity, before
// the scene is drawn. Each reads the entity's ComputeBufferComponent buffers
// and writes its RenderTextureComponent textures, picked by index; their
// stages follow the order listed, buffers first. Uniforms come from the
// entity's ShaderParamsComponent
struct ComputeJobComponent {
  struct Dispatch {
    std::string shader;             // compiled compute shader, e.g. "cs_x.bin"
    std::vector<uint8_t> buffers;   // read, into ComputeBufferComponent
    std::vector<uint8_t> textures;  // written, into RenderTextureComponent
    uint32_t groups[3] = {1, 1, 1};
  };
  std::vector<Dispatch> dispatches_;
  bool enabled_ = true;
};

#endif  // COMPUTE_JOB_COMPONENT_H
