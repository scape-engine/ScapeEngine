#ifndef REGION_GENERATOR_H
#define REGION_GENERATOR_H

#include <glm/glm.hpp>
#include <string>

#include "engine/pcg/generator_base.h"

namespace ProcWorld {

// A volume's worldgen region: content (JSON, project-relative) and the cell
// it builds. The volume's lod_level is the region level
class RegionGenerator : public PCG::Generator {
public:
  PCG::GeneratorType GetType() const override {
    return PCG::GeneratorType::Region;
  }
  std::string GetDisplayName() const override { return "Region"; }
  std::unique_ptr<PCG::Generator> Clone() const override {
    return std::make_unique<RegionGenerator>(*this);
  }

  std::string content_path;
  // Cell centre in planet space; zero: above the north pole (0, radius + 16, 0)
  glm::ivec3 cell{0};

  // Last build, written by ProcWorld::Subsystem
  std::string error;
  size_t vertex_count = 0;
  size_t instance_count = 0;
  float build_ms = 0.0f;
};

}  // namespace ProcWorld

#endif  // REGION_GENERATOR_H
