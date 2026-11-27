#include "graph_volumes.h"

#include <algorithm>

#include "engine/context/engine_context.h"
#include "engine/ecs/components/mesh_renderer_component.h"
#include "engine/ecs/components/procedural_instance_component.h"
#include "engine/ecs/components/volume_component.h"
#include "engine/geometry/meshing/heightmap_mesher.h"
#include "engine/paging/jobs.h"
#include "engine/pcg/generator_resource.h"
#include "generator_graph.h"

namespace PCG {
namespace {

// The volume's graph generator; null: it has another
const GeneratorGraph* _Graph(const VolumeComponent& volume) {
  auto resource =
      EngineContext::resourceManager().GetResourceAs<GeneratorResource>(
          volume.generator_id);
  if (!resource || !resource->Get() ||
      resource->GetGeneratorType() != GeneratorType::Graph)
    return nullptr;
  return static_cast<const GeneratorGraph*>(resource->Get());
}

}  // namespace

Geometry::MeshData GraphVolumes::Heightfield(const GeneratorGraph& graph,
                                             glm::vec2 min, glm::vec2 max,
                                             uint16_t resolution) {
  if (!graph.IsCompiled())
    return {};
  const size_t n = std::max<uint16_t>(resolution, 2);
  const glm::vec2 step = (max - min) / float(n - 1);
  std::vector<float> heights(n * n);
  Jobs::ForEach(n, [&](size_t row) {
    for (size_t column = 0; column < n && !Jobs::Stopping(); ++column) {
      const glm::vec2 at = min + glm::vec2(column, row) * step;
      heights[row * n + column] = graph.Eval(at.x, at.y).height;
    }
  });
  Geometry::MesherParams params;
  params.size = static_cast<uint16_t>(n);
  params.origin = min;
  params.spacing = step;
  return Geometry::HeightmapMesher(
             [&](float x, float y) {
               return Geometry::HeightSample{
                   heights[size_t(y) * n + size_t(x)], 0.0f, glm::vec3(1.0f)};
             },
             params)
      .Generate();
}

void GraphVolumes::Update(ECS& ecs) {
  // Volumes gone, or no longer running a graph
  for (auto it = terrains_.begin(); it != terrains_.end();) {
    if (!ecs.Reg().valid(it->first) || !ecs.Has<VolumeComponent>(it->first) ||
        !_Graph(ecs.Get<VolumeComponent>(it->first))) {
      Release(ecs, it->second);
      it = terrains_.erase(it);
    } else {
      ++it;
    }
  }

  for (auto [entity, volume] : ecs.View<VolumeComponent>().each()) {
    const GeneratorGraph* graph = _Graph(volume);
    if (!graph)
      continue;
    Terrain& terrain = terrains_[entity];

    // Made: shown in place of the old one
    if (terrain.making.valid() &&
        terrain.making.wait_for(std::chrono::seconds(0)) ==
            std::future_status::ready) {
      const Geometry::MeshData data = terrain.making.get();
      std::unique_ptr<Mesh> old = std::move(terrain.mesh);
      if (data.vertices.empty()) {
        if (ecs.Reg().valid(terrain.entity))
          ecs.DestroyEntity(terrain.entity);
        terrain.entity = entt::null;
      } else {
        terrain.mesh = std::make_unique<Mesh>(data);
        if (!ecs.Reg().valid(terrain.entity)) {
          terrain.entity = std::get<0>(ecs.CreateEntity("Terrain", entity));
          ecs.Add<MeshRendererComponent>(terrain.entity);
          ecs.Add<ProceduralInstanceComponent>(terrain.entity, entity);
        }
        ecs.Get<MeshRendererComponent>(terrain.entity).mesh_ =
            terrain.mesh.get();
      }
    }

    // One at a time: edits while one is made wait for it
    if (volume.dirty && !terrain.making.valid()) {
      volume.dirty = false;
      // Its own copy: the editor may reload the graph meanwhile
      std::shared_ptr<const Generator> copy = graph->Clone();
      terrain.making = Jobs::Submit(
          [copy, min = glm::vec2(volume.bounds_min.x, volume.bounds_min.z),
           max = glm::vec2(volume.bounds_max.x, volume.bounds_max.z),
           resolution = volume.resolution] {
            return Heightfield(static_cast<const GeneratorGraph&>(*copy), min,
                               max, resolution);
          });
    }
  }
}

void GraphVolumes::Shutdown() {
  terrains_.clear();
}

void GraphVolumes::Release(ECS& ecs, Terrain& terrain) {
  if (ecs.Reg().valid(terrain.entity))
    ecs.DestroyEntity(terrain.entity);
  terrain = Terrain();
}

}  // namespace PCG
