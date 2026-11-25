#ifndef GENERATOR_RESOURCE_H
#define GENERATOR_RESOURCE_H

#include "engine/memory/resource.h"
#include "engine/pcg/generator_base.h"

namespace PCG {
class GeneratorResource : public Resource {
public:
  GeneratorResource() = default;

  void SetGenerator(std::unique_ptr<Generator> generator) {
    generator_ = std::move(generator);
  }

  Generator* Get() { return generator_.get(); }
  const Generator* Get() const { return generator_.get(); }

  GeneratorType GetGeneratorType() const {
    return generator_ ? generator_->GetType() : GeneratorType::Preset;
  }

  // Accessors
  FieldGenerator* GetAsField() {
    return dynamic_cast<FieldGenerator*>(generator_.get());
  }
  const FieldGenerator* GetAsField() const {
    return dynamic_cast<const FieldGenerator*>(generator_.get());
  }

  InstanceGenerator* GetAsInstance() {
    return dynamic_cast<InstanceGenerator*>(generator_.get());
  }
  const InstanceGenerator* GetAsInstance() const {
    return dynamic_cast<const InstanceGenerator*>(generator_.get());
  }

  // Resource interface
  void Destroy() override { generator_.reset(); }
  std::string GetName() const {
    return generator_ ? generator_->GetDisplayName() : "Empty Generator";
  }

  // The profile asset its settings start from, project-relative; empty: none
  // (see generator_profile.h)
  std::string profile_;

private:
  std::unique_ptr<Generator> generator_;
};

}  // namespace PCG

#endif  // GENERATOR_RESOURCE_H