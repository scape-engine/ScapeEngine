#ifndef CRAFT_GENERATOR_H
#define CRAFT_GENERATOR_H

#include <cstdint>
#include <string>

#include "engine/pcg/generator_base.h"

namespace ProcWorld {

// A volume's worldgen craft: the fleet its content (JSON, project-relative)
// gives for the seed, each craft a model placed in the volume's bounds. A
// fleet of one fixed slot is a single craft
class CraftGenerator : public PCG::Generator {
public:
  PCG::GeneratorType GetType() const override {
    return PCG::GeneratorType::Craft;
  }
  std::string GetDisplayName() const override { return "Craft"; }
  std::unique_ptr<PCG::Generator> Clone() const override {
    return std::make_unique<CraftGenerator>(*this);
  }

  std::string content_path;
  uint64_t seed = 1;
  // A lawless area: its fleet's craft change class at the content's chances
  bool lawless = false;

  // Last build, written by ProcWorld::Subsystem
  std::string error;
  size_t craft_count = 0;
  size_t missing_count = 0;  // slots no model fits
  float build_ms = 0.0f;
};

}  // namespace ProcWorld

#endif  // CRAFT_GENERATOR_H
