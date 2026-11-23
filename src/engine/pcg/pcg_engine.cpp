#include "pcg_engine.h"

#include "engine/content/loaders/mesh_loader.h"
#include "engine/context/engine_context.h"
#include "engine/ecs/components/mesh_renderer_component.h"
#include "engine/ecs/components/volume_component.h"
#include "engine/ecs/world.h"

#include "generator_resource.h"

void PCGEngine::Create() {

  // Initialize ProcGen systems
  procmodel_.Init();
  procworld_.Init();

  // TODO:
  // procterrain_.Init();
  // proctextures_.Init();
}

void PCGEngine::ProcessQueued() {
  auto& ecs = ECS::Main();  // should not be called directly (tight coupling)
  auto& resource_mgr = EngineContext::resourceManager();

  // Region volumes
  procworld_.Update();
  // Water surfaces, some made by the planets above
  water_.Update(ecs);

  // Process all dirty volumes
  ecs.View<VolumeComponent, MeshRendererComponent>().each(
      [&](Entity entity, VolumeComponent& volume,
          MeshRendererComponent& mesh_renderer) {
        if (!volume.dirty)
          return;

        // Check if cancelled
        if (cancelled_.count(static_cast<uint32_t>(entity))) {
          cancelled_.erase(static_cast<uint32_t>(entity));
          return;
        }

        // Get generator resource
        auto gen_ref = resource_mgr.GetResourceAs<PCG::GeneratorResource>(
            volume.generator_id);
        if (!gen_ref || !gen_ref->Get())
          return;

        auto* generator = gen_ref->Get();

        // TODO: Generate mesh using generator->Eval()
        // ! narrow from Generator* to FieldGenerator*

        volume.dirty = false;
      });

  // Clear processed requests
  while (!pending_requests_.empty()) {
    pending_requests_.pop();
  }
}

PCGEngine::~PCGEngine() {
  procworld_.Shutdown();
  water_.Shutdown();
  procmodel_.Shutdown();
}
