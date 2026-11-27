#ifndef PCG_ENGINE_H
#define PCG_ENGINE_H

#include <functional>
#include <memory>
#include <queue>
#include <unordered_set>

#include "engine/pcg/graph/program_graph.h"
#include "engine/pcg/points/spawner_system.h"
#include "engine/pcg/procmodel/procmodel.h"
#include "engine/pcg/procworld/procworld.h"
#include "engine/pcg/water/water_subsystem.h"

struct GenerationRequest {
  uint32_t volume_id;  // Entity ID (VolumeComponent lookup)
  uint8_t priority;    // Higher = sooner
};

class PCGEngine {
public:
  PCGEngine() = default;
  ~PCGEngine();

  void Create();

  //
  // GENERATION QUEUE
  //
  void RequestGeneration(uint32_t volume_entity_id, uint8_t priority = 0) {
    pending_requests_.push({volume_entity_id, priority});
  }

  void CancelGeneration(uint32_t volume_entity_id) {
    cancelled_.insert(volume_entity_id);
  }

  // ? Called from EngineContext::NextFrame()
  void ProcessQueued();

  ProcModel::Subsystem& GetProcModel() { return procmodel_; }
  const ProcModel::Subsystem& GetProcModel() const { return procmodel_; }

  ProcWorld::Subsystem& GetProcWorld() { return procworld_; }
  const ProcWorld::Subsystem& GetProcWorld() const { return procworld_; }

  Water::Subsystem& GetWater() { return water_; }

  // TODO:
  // ProcTerrain::Subsystem& GetTerrain() { return terrain_; }
  // ProcTextures::Subsystem& GetTextures() { return textures_; }

private:
  std::queue<GenerationRequest> pending_requests_;
  std::unordered_set<uint32_t> cancelled_;

  ProcModel::Subsystem procmodel_;
  PCG::SpawnerSystem spawner_;
  ProcWorld::Subsystem procworld_;
  Water::Subsystem water_;

  // TODO:
  // ProcTerrain::Subsystem terrain_;
  // ProcTextures::Subsystem textures_;
};

#endif  // PCG_ENGINE_H
