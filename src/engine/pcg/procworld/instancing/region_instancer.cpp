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
                            uint64_t seed, const std::filesystem::path& root,
                            const Worldgen::TextureInput* planet) {
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
        .SpawnInstance(path.string(), seed, source, planet)
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
    float skirt_depth, const Worldgen::TextureInput* planet) {
  InstanceResult result;

  if (!region.mesh.empty()) {
    const glm::vec3 origin(region.origin[0], region.origin[1],
                           region.origin[2]);
    const RegionMeshData data =
        RegionMesher(region.mesh, origin, skirt_depth).Generate();
    result.terrain_mesh = std::make_unique<Mesh>(
        data.vertices.data(), static_cast<uint32_t>(data.vertices.size()),
        RegionMeshData::Layout(), data.indices);
    auto [terrain, transform] = ecs.CreateEntity("Terrain", source);
    auto& renderer = ecs.Add<MeshRendererComponent>(terrain);
    renderer.mesh_ = result.terrain_mesh.get();
    renderer.material_ = terrain_material;
    ecs.Add<ProceduralInstanceComponent>(terrain, source);
    result.terrain = terrain;
  }

  // Seeds from the region's origin, so each region's objects differ
  uint64_t seed = 0;
  for (const float o : region.origin) {
    uint32_t bits;
    std::memcpy(&bits, &o, sizeof(bits));
    seed = (seed ^ bits) * 0x100000001b3ull;
  }
  for (const Worldgen::ObjectInstances& instances : region.objects) {
    const std::string& id = content.objects[instances.object].id;
    for (const std::array<float, 16>& matrix : instances.matrices) {
      const Entity entity = _CreateObject(ecs, id, source, ++seed, root, planet);
      if (entity == entt::null)
        break;  // the source is missing: skip the rest of this kind
      _SetTransform(ecs.Get<TransformComponent>(entity), matrix);
      ecs.Add<ProceduralInstanceComponent>(entity, source);
      result.object_entities.push_back(entity);
    }
  }
  return result;
}

std::vector<Entity> RegionInstancer::InstanceHerds(
    ECS& ecs, const Worldgen::Region& region,
    const Worldgen::RegionContent& content, Entity source,
    const std::filesystem::path& root, const Worldgen::TextureInput* planet) {
  std::vector<Entity> creatures;
  // Every creature of a kind on a planet is the same model (one seed per kind
  // and planet, as the game's spawns); the matrices hold their scale
  // TODO: skinning, so creatures take worldgen's poses (PoseValues) and
  // animate instead of standing in their bind pose
  for (size_t spawn = 0;
       spawn < region.herds.size() && spawn < content.herd_ids.size();
       ++spawn) {
    const std::string& id = content.herd_ids[spawn];
    const std::vector<float>& matrices = region.herds[spawn].matrices;
    uint64_t kind_seed = planet ? planet->seed : 0;
    for (const char c : id)
      kind_seed = (kind_seed ^ static_cast<uint8_t>(c)) * 0x100000001b3ull;
    for (size_t at = 0; !id.empty() && at + 12 <= matrices.size(); at += 12) {
      std::array<float, 16> rows{};  // 12 floats: rows without w
      for (int row = 0; row < 4; ++row)
        for (int k = 0; k < 3; ++k)
          rows[row * 4 + k] = matrices[at + row * 3 + k];
      rows[15] = 1.0f;
      const Entity entity =
          _CreateObject(ecs, id, source, kind_seed, root, planet);
      if (entity == entt::null)
        break;  // the source is missing: skip the rest of this kind
      _SetTransform(ecs.Get<TransformComponent>(entity), rows);
      ecs.Add<ProceduralInstanceComponent>(entity, source);
      creatures.push_back(entity);
    }
  }
  return creatures;
}

}  // namespace ProcWorld
