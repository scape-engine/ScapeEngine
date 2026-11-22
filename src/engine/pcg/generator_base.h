#ifndef GENERATOR_BASE_H
#define GENERATOR_BASE_H

#include <glm/glm.hpp>
#include <memory>
#include <string>

#include "./core/sample.h"

namespace PCG {

class ProgramGraph;

enum class GeneratorType {
  Preset,  // PCG::Generator with Config
  Graph,   // Node graph
  Region,  // Worldgen region: terrain and objects (ProcWorld)
  Planet,  // Worldgen planet: regions around the viewer (ProcWorld)
  Craft,   // Worldgen craft: a fleet in the volume's bounds (ProcWorld)
};

inline const char* GetGeneratorTypeName(GeneratorType type) {
  switch (type) {
    case GeneratorType::Preset:
      return "Preset";
    case GeneratorType::Graph:
      return "Graph";
    case GeneratorType::Region:
      return "Region";
    case GeneratorType::Planet:
      return "Planet";
    case GeneratorType::Craft:
      return "Craft";
    default:
      return "Unknown";
  }
}

//
// Generator Interface
//
class Generator {
public:
  virtual ~Generator() = default;

  virtual GeneratorType GetType() const = 0;
  virtual std::string GetDisplayName() const = 0;

  // Graph access
  virtual ProgramGraph* GetGraph() { return nullptr; }
  virtual const ProgramGraph* GetGraph() const { return nullptr; }

  // Clone for copy operations
  virtual std::unique_ptr<Generator> Clone() const;
};

class FieldGenerator : public Generator {
public:
  // Point evaluation
  virtual Sample Eval(float x, float y) const = 0;
};

class InstanceGenerator : public Generator {
public:
  // TODO: concrete signature TBD when ProcModel is wired up
  // Possibilities:
  //   virtual InstanceModel Generate(uint64_t seed) = 0;
  //   virtual InstantiateResult Instantiate(uint64_t seed, Entity parent) = 0;
};

//
// Factory
//
std::unique_ptr<FieldGenerator> CreateGenerator(GeneratorType type);

}  // namespace PCG

#endif  // GENERATOR_BASE_H