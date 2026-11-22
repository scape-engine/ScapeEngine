#include "region_instancer.h"

#include <cstring>
#include <glm/gtc/type_ptr.hpp>

#include "engine/context/engine_context.h"
#include "engine/content/cache/material_cache.h"
#include "engine/content/cache/mesh_cache.h"
#include "engine/ecs/components/animator_component.h"
#include "engine/ecs/components/ground_component.h"
#include "engine/ecs/components/instanced_mesh_component.h"
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

Entity RegionInstancer::CreateObject(ECS& ecs, const std::string& id,
                                     Entity source, uint64_t seed,
                                     const std::filesystem::path& root,
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
      const Entity entity = CreateObject(ecs, id, source, ++seed, root, planet);
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
  // The region's terrain as ground for the creatures' feet, in the source's
  // space (region-local, as the herd matrices); up is away from the planet's
  // centre
  std::shared_ptr<const GroundMesh> ground;
  if (!region.herds.empty() && !region.mesh.empty()) {
    std::vector<glm::vec3> triangles(region.mesh.size());
    glm::vec3 normal;
    for (size_t i = 0; i < region.mesh.size(); ++i)
      Worldgen::Polygonizer::DecodeVertex(region.mesh[i],
                                          glm::value_ptr(triangles[i]),
                                          glm::value_ptr(normal));
    const glm::vec3 up(region.origin[0], region.origin[1], region.origin[2]);
    ground = std::make_shared<GroundMesh>(
        std::move(triangles),
        glm::dot(up, up) > 0.0f ? up : glm::vec3(0, 1, 0));
    ecs.Reg().emplace_or_replace<GroundComponent>(source, ground);
  }
  // Every creature of a species on a planet is the same model: the species'
  // seed, else one per model id and planet; the matrices hold their scale.
  // Skinned models animate (ModelInstantiator)
  for (size_t spawn = 0;
       spawn < region.herds.size() && spawn < content.herd_ids.size();
       ++spawn) {
    const std::string& id = content.herd_ids[spawn];
    const std::vector<float>& matrices = region.herds[spawn].matrices;
    uint64_t kind_seed = planet ? planet->seed : 0;
    for (const char c : id)
      kind_seed = (kind_seed ^ static_cast<uint8_t>(c)) * 0x100000001b3ull;
    if (spawn < content.herd_seeds.size())
      kind_seed = content.herd_seeds[spawn];
    for (size_t at = 0; !id.empty() && at + 12 <= matrices.size(); at += 12) {
      std::array<float, 16> rows{};  // 12 floats: rows without w
      for (int row = 0; row < 4; ++row)
        for (int k = 0; k < 3; ++k)
          rows[row * 4 + k] = matrices[at + row * 3 + k];
      rows[15] = 1.0f;
      const Entity entity =
          CreateObject(ecs, id, source, kind_seed, root, planet);
      if (entity == entt::null)
        break;  // the source is missing: skip the rest of this kind
      _SetTransform(ecs.Get<TransformComponent>(entity), rows);
      ecs.Add<ProceduralInstanceComponent>(entity, source);
      if (auto* animator = ecs.Reg().try_get<AnimatorComponent>(entity);
          animator && ground)
        animator->ground_ = source;
      creatures.push_back(entity);
    }
  }
  return creatures;
}

RegionInstancer::BuildingPieces RegionInstancer::GatherBuildings(
    const Worldgen::Region& region, BuildingLibrary& library,
    const std::filesystem::path& root) {
  BuildingPieces pieces;
  for (const Worldgen::RegionBuilding& building : region.buildings) {
    const auto structure = library.Structure(building.cls, building.seed);
    if (structure->empty())
      continue;
    pieces.buildings++;
    const glm::mat4 place = glm::make_mat4(building.matrix.data());
    uint64_t seed = building.seed;
    for (const Worldgen::BuildingPiece& piece : *structure) {
      // rows read as glm columns
      const glm::mat4 matrix = place * glm::make_mat4(piece.matrix.data());
      ++seed;
      if (_StartsWith(piece.model, procmodel_prefix)) {
        pieces.objects.push_back({piece.model, seed, matrix});
        continue;
      }
      auto [it, added] = pieces.models.try_emplace(piece.model);
      it->second.push_back(matrix);
      if (added && !_StartsWith(piece.model, primitive_prefix)) {
        const std::string key = ModelEntityFactory::ModelKey(piece.model, root);
        Content::MeshCache::Instance().Get(key);
        Content::MaterialCache::Instance().Get(key);
      }
    }
  }
  return pieces;
}

std::vector<Entity> RegionInstancer::InstanceBuildings(
    ECS& ecs, const BuildingPieces& pieces, Entity source,
    const std::filesystem::path& root, const Worldgen::TextureInput* planet) {
  std::vector<Entity> entities;
  if (!pieces.models.empty()) {
    auto [entity, transform] = ecs.CreateEntity("Building pieces", source);
    auto& set = ecs.Add<InstancedMeshComponent>(entity);
    for (const auto& [model, matrices] : pieces.models) {
      const uint32_t first = static_cast<uint32_t>(set.locals_.size());
      set.locals_.insert(set.locals_.end(), matrices.begin(), matrices.end());
      for (const auto& [mesh, material] :
           ModelEntityFactory::Meshes(model, root)) {
        InstancedMeshComponent::Part part;
        part.mesh = mesh;
        part.material = material;
        part.first = first;
        part.count = static_cast<uint32_t>(matrices.size());
        set.parts_.push_back(part);
      }
    }
    ecs.Add<ProceduralInstanceComponent>(entity, source);
    entities.push_back(entity);
  }
  for (const BuildingPieces::Object& object : pieces.objects) {
    const Entity entity =
        CreateObject(ecs, object.id, source, object.seed, root, planet);
    if (entity == entt::null)
      continue;
    std::array<float, 16> rows;
    std::memcpy(rows.data(), glm::value_ptr(object.matrix), sizeof(rows));
    _SetTransform(ecs.Get<TransformComponent>(entity), rows);
    ecs.Add<ProceduralInstanceComponent>(entity, source);
    entities.push_back(entity);
  }
  return entities;
}

}  // namespace ProcWorld
