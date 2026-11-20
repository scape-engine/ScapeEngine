#include "region_instancer.h"

#include <cstring>
#include <glm/gtc/type_ptr.hpp>

#include "engine/context/engine_context.h"
#include "engine/ecs/components/procedural_instance_component.h"
#include "engine/pcg/pcg_engine.h"
#include "engine/pcg/procworld/region_mesher.h"
#include "engine/scene/model_entity_factory.h"

namespace ProcWorld {

static constexpr const char* primitive_prefix = "primitive:";
static constexpr const char* procmodel_prefix = "procmodel:";

static bool _StartsWith(const std::string& text, const char* prefix) {
  return text.rfind(prefix, 0) == 0;
}

// One instance of an object; entt::null if its source can't be made
static Entity _CreateObject(ECS& ecs, const std::string& id, Entity source,
                            uint64_t seed, const std::filesystem::path& root) {
  if (_StartsWith(id, primitive_prefix)) {
    auto [entity, transform] = ecs.CreateEntity(id, source);
    ModelEntityFactory::AttachPrimitive(
        ecs, entity, id.substr(std::strlen(primitive_prefix)));
    return entity;
  }
  if (_StartsWith(id, procmodel_prefix)) {
    const auto path = root / id.substr(std::strlen(procmodel_prefix));
    return EngineContext::PCG()
        .GetProcModel()
        .SpawnInstance(path.string(), seed, source)
        .root;
  }
  const Entity entity = ModelEntityFactory::InstanceModel(ecs, id, root);
  if (entity != entt::null)
    ecs.SetParent(entity, source);
  return entity;
}

// Rows of x, y, z axes and the translation (worldgen's layout) as local TRS
static void _SetTransform(TransformComponent& transform,
                          const std::array<float, 16>& rows) {
  glm::mat4 m = glm::make_mat4(rows.data());  // rows read as glm columns
  const glm::vec3 scale(glm::length(glm::vec3(m[0])),
                        glm::length(glm::vec3(m[1])),
                        glm::length(glm::vec3(m[2])));
  const glm::mat3 rotation(glm::vec3(m[0]) / scale.x,
                           glm::vec3(m[1]) / scale.y,
                           glm::vec3(m[2]) / scale.z);
  transform.position_ = glm::vec3(m[3]);
  transform.rotation_ = glm::quat_cast(rotation);
  transform.euler_angles_ = glm::degrees(glm::eulerAngles(transform.rotation_));
  transform.scale_ = scale;
  transform.modified_ = true;
}

RegionInstancer::InstanceResult RegionInstancer::Instance(
    ECS& ecs, const Worldgen::Region& region,
    const Worldgen::RegionContent& content, Entity source,
    MaterialHandle terrain_material, const std::filesystem::path& root,
    float skirt_depth) {
  InstanceResult result;

  if (!region.mesh.empty()) {
    const glm::vec3 origin(region.origin[0], region.origin[1],
                           region.origin[2]);
    result.terrain_mesh = std::make_unique<Mesh>(
        RegionMesher(region.mesh, origin, skirt_depth).Generate());
    auto [terrain, transform] = ecs.CreateEntity("Terrain", source);
    auto& renderer = ecs.Add<MeshRendererComponent>(terrain);
    renderer.mesh_ = result.terrain_mesh.get();
    renderer.material_ = terrain_material;
    ecs.Add<ProceduralInstanceComponent>(terrain, source);
    result.terrain = terrain;
  }

  uint64_t seed = 0;
  for (const Worldgen::ObjectInstances& instances : region.objects) {
    const std::string& id = content.objects[instances.object].id;
    for (const std::array<float, 16>& matrix : instances.matrices) {
      const Entity entity = _CreateObject(ecs, id, source, ++seed, root);
      if (entity == entt::null)
        break;  // the source is missing: skip the rest of this kind
      _SetTransform(ecs.Get<TransformComponent>(entity), matrix);
      ecs.Add<ProceduralInstanceComponent>(entity, source);
      result.object_entities.push_back(entity);
    }
  }
  return result;
}

}  // namespace ProcWorld
