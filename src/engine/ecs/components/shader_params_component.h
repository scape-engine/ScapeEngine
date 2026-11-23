#ifndef SHADER_PARAMS_COMPONENT_H
#define SHADER_PARAMS_COMPONENT_H

#include <glm/glm.hpp>

#include <string>
#include <vector>

// Uniforms an entity's shaders read, on top of its material's: set for its
// compute jobs and its draws, by name (vec4 arrays of a fixed length)
struct ShaderParamsComponent {
  struct Uniform {
    std::string name;
    std::vector<glm::vec4> values;
  };
  std::vector<Uniform> uniforms_;
};

#endif  // SHADER_PARAMS_COMPONENT_H
