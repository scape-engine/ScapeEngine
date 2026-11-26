#include "region_instancer.h"

#include <cstring>
#include <glm/gtc/type_ptr.hpp>

#include "engine/context/engine_context.h"
#include "engine/content/cache/material_cache.h"
#include "engine/content/cache/mesh_cache.h"
#include "engine/diagnostics/frame_stats.h"
#include "engine/ecs/components/animator_component.h"
#include "engine/ecs/components/ground_component.h"
#include "engine/ecs/components/instanced_mesh_component.h"
#include "engine/ecs/components/mesh_owner_component.h"
#include "engine/ecs/components/mesh_renderer_component.h"
#include "engine/ecs/components/procedural_instance_component.h"
#include "engine/pcg/pcg_engine.h"
#include "engine/pcg/procworld/herd_movement.h"
#include "engine/pcg/procworld/region_mesher.h"
#include "engine/scene/model_entity_factory.h"

#include "placement/nav_query.h"

namespace ProcWorld {

static constexpr const char* primitive_prefix = "primitive:";
static constexpr const char* procmodel_prefix = "procmodel:";

static bool _StartsWith(const std::string& text, const char* prefix) {
  return text.rfind(prefix, 0) == 0;
}

// The first object seed of a region: from its origin, so each region's
// objects differ (each object takes the next)
static uint64_t _ObjectSeed(const Worldgen::Region& region) {
  uint64_t seed = 0;
  for (const float o : region.origin) {
    uint32_t bits;
    std::memcpy(&bits, &o, sizeof(bits));
    seed = (seed ^ bits) * 0x100000001b3ull;
  }
  return seed;
}

// Every creature of a species on a planet is the same model: the species'
// seed, else one per model id and planet
static uint64_t _KindSeed(const Worldgen::RegionContent& content, size_t spawn,
                          const Worldgen::TextureInput* planet) {
  if (spawn < content.herd_seeds.size())
    return content.herd_seeds[spawn];
  uint64_t seed = planet ? planet->seed : 0;
  for (const char c : content.herd_ids[spawn])
    seed = (seed ^ static_cast<uint8_t>(c)) * 0x100000001b3ull;
  return seed;
}

RegionInstancer::ObjectTags RegionInstancer::Tags(
    const Worldgen::Region& region) {
  ObjectTags tags;
  uint64_t seed = _ObjectSeed(region);
  for (const Worldgen::ObjectInstances& instances : region.objects) {
    auto& kind = tags.emplace_back(instances.matrices.size());
    for (ObjectTag& tag : kind)
      tag.seed = ++seed;
  }
  return tags;
}

RegionInstancer::ObjectModels RegionInstancer::GenerateObjects(
    const Worldgen::Region& region, const Worldgen::RegionContent& content,
    bool herds, const Archetypes& archetypes,
    const ProcModel::Subsystem& procmodel,
    const Worldgen::TextureInput* planet, const ObjectTags* tags) {
  ObjectModels models;
  const auto generate = [&](const std::string& id, uint64_t seed) {
    const auto archetype = archetypes.find(id);
    if (archetype == archetypes.end() || !archetype->second)
      return;
    auto [it, added] = models.try_emplace({id, seed});
    if (added)
      it->second = procmodel.Generate(*archetype->second, seed);
  };
  uint64_t seed = _ObjectSeed(region);
  for (size_t kind = 0; kind < region.objects.size(); ++kind) {
    const Worldgen::ObjectInstances& instances = region.objects[kind];
    const std::string& id = content.objects[instances.object].id;
    for (size_t i = 0; i < instances.matrices.size(); ++i) {
      ++seed;
      generate(id, tags ? (*tags)[kind][i].seed : seed);
    }
  }
  if (herds)
    for (size_t spawn = 0;
         spawn < region.herds.size() && spawn < content.herd_ids.size();
         ++spawn)
      if (!content.herd_ids[spawn].empty())
        generate(content.herd_ids[spawn], _KindSeed(content, spawn, planet));
  return models;
}

Entity RegionInstancer::CreateObject(ECS& ecs, const std::string& id,
                                     Entity source, uint64_t seed,
                                     const std::filesystem::path& root,
                                     const Worldgen::TextureInput* planet,
                                     const ObjectModels* models) {
  if (_StartsWith(id, primitive_prefix)) {
    auto [entity, transform] = ecs.CreateEntity(id, source);
    ModelEntityFactory::AttachPrimitive(
        ecs, entity, id.substr(std::strlen(primitive_prefix)));
    return entity;
  }
  if (_StartsWith(id, procmodel_prefix)) {
    const auto path = root / id.substr(std::strlen(procmodel_prefix));
    if (models) {
      if (auto it = models->find({id, seed}); it != models->end())
        return it->second ? EngineContext::PCG()
                                .GetProcModel()
                                .SpawnInstance(path.string(), *it->second,
                                               source, planet)
                                .root
                          : entt::null;
    }
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

// A static procmodel object's parts, in its space, for instancing; nullopt
// for other objects and animated models, which CreateObject makes.
// `deformed` takes what keeps their deformed meshes alive
static std::optional<std::vector<ProcModel::ModelInstantiator::Part>>
_ObjectParts(const std::string& id, uint64_t seed,
             const std::filesystem::path& root,
             const Worldgen::TextureInput* planet,
             const RegionInstancer::ObjectModels* models,
             std::shared_ptr<const ProcModel::Subsystem::DeformedMeshes>&
                 deformed) {
  if (!_StartsWith(id, procmodel_prefix) || !models)
    return std::nullopt;
  const auto it = models->find({id, seed});
  if (it == models->end())
    return std::nullopt;
  if (!it->second)
    return std::nullopt;  // failed: CreateObject skips the kind
  const auto path = root / id.substr(std::strlen(procmodel_prefix));
  return EngineContext::PCG().GetProcModel().InstanceParts(
      path.string(), *it->second, planet, &deformed);
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
    float skirt_depth, const Worldgen::TextureInput* planet,
    const ObjectModels* models, bool objects) {
  Diagnostics::ScopedSection section("Terrain");
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

  if (objects) {
    ObjectProgress progress;
    InstanceObjects(ecs, region, content, source, root, planet, models,
                    progress, [] { return true; });
    result.objects = progress.made;
  }
  return result;
}

bool RegionInstancer::InstanceObjects(
    ECS& ecs, const Worldgen::Region& region,
    const Worldgen::RegionContent& content, Entity source,
    const std::filesystem::path& root, const Worldgen::TextureInput* planet,
    const ObjectModels* models, ObjectProgress& progress,
    const std::function<bool()>& more, std::vector<Entity>* made,
    const ObjectTags* tags) {
  Diagnostics::ScopedSection section("Objects");
  if (!progress.started) {
    progress.seed = _ObjectSeed(region);
    progress.started = true;
  }
  while (progress.kind < region.objects.size()) {
    const Worldgen::ObjectInstances& instances = region.objects[progress.kind];
    if (progress.at >= instances.matrices.size()) {
      progress.kind++;
      progress.at = 0;
      continue;
    }
    if (!more())
      return false;
    const Worldgen::RegionObject& object = content.objects[instances.object];
    const std::string& id = object.id;
    ++progress.seed;
    const ObjectTag* tag = tags ? &(*tags)[progress.kind][progress.at] : nullptr;
    const uint64_t seed = tag ? tag->seed : progress.seed;
    const PCG::InstanceKey key = tag ? tag->key : PCG::InstanceKey{};
    glm::mat4 matrix = glm::make_mat4(
        instances.matrices[progress.at].data());  // rows read as glm columns
    // The decorator's w lanes are not a projective row (_SetTransform skips
    // them too)
    matrix[0][3] = matrix[1][3] = matrix[2][3] = 0.0f;
    matrix[3][3] = 1.0f;
    if (progress.imposter_kind != progress.kind) {
      progress.imposter_kind = progress.kind;
      progress.imposter =
          _StartsWith(id, procmodel_prefix)
              ? EngineContext::PCG().GetProcModel().Imposter(
                    (root / id.substr(std::strlen(procmodel_prefix))).string(),
                    planet)
              : nullptr;
      if (progress.imposter)
        progress.owned.push_back(progress.imposter);
    }
    const ProcModel::Subsystem::ModelImposter* imposter =
        progress.imposter.get();
    // Its meshes, up to the imposter's distance once its card is baked
    const bool card = imposter && imposter->imposter->Baked();
    const float distance = card ? imposter->distance : 0.0f;
    const float blend = card ? imposter->blend : 0.0f;
    const glm::vec3 origin(matrix[3]);
    const auto place = [&](const Mesh* mesh, MaterialHandle material,
                           const glm::mat4& local, float from, float until) {
      auto& placed = progress.instances[{mesh, material,
                                         object.spawn.fade_start,
                                         object.spawn.fade_end, from, until,
                                         blend}];
      placed.matrices.push_back(local);
      placed.anchors.push_back(origin);
    };
    if (imposter) {
      progress.sources.push_back({origin, id, seed, key, matrix});
      const auto& variants = imposter->imposter->Variants();
      const size_t variant = seed % variants.size();
      for (const Imposter::Part& part : variants[variant])
        place(part.mesh, part.material, matrix * part.matrix, 0.0f, distance);
      if (card)
        place(imposter->imposter->Card(),
              imposter->imposter->Material(variant), matrix, distance, 0.0f);
      progress.at++;
      progress.made++;
      continue;
    }
    std::shared_ptr<const ProcModel::Subsystem::DeformedMeshes> deformed;
    if (const auto parts =
            _ObjectParts(id, seed, root, planet, models, deformed)) {
      progress.sources.push_back({origin, id, seed, key, matrix});
      for (const ProcModel::ModelInstantiator::Part& part : *parts)
        place(part.mesh, part.material, matrix * part.matrix, 0.0f, 0.0f);
      if (deformed)
        progress.owned.push_back(std::move(deformed));
      progress.at++;
      progress.made++;
      continue;
    }
    // An entity of its own (an animated model, a primitive), keyed as the
    // instanced ones are
    const Entity entity =
        CreateObject(ecs, id, source, seed, root, planet, models);
    if (entity == entt::null) {
      // The source is missing: skip the rest of this kind
      progress.kind++;
      progress.at = 0;
      continue;
    }
    _SetTransform(ecs.Get<TransformComponent>(entity),
                  instances.matrices[progress.at++]);
    ecs.Add<ProceduralInstanceComponent>(entity, source);
    auto& sources = ecs.Add<InstanceSourceComponent>(entity);
    sources.sources_.push_back({origin, id, seed, key, matrix});
    sources.stage_ = progress.stage;
    sources.cell_ = progress.cell;
    progress.made++;
    if (made)
      made->push_back(entity);
  }
  if (!progress.instances.empty()) {
    auto [entity, transform] = ecs.CreateEntity("Objects", source);
    auto& set = ecs.Add<InstancedMeshComponent>(entity);
    for (auto& [key, placed] : progress.instances) {
      InstancedMeshComponent::Part part;
      std::tie(part.mesh, part.material, part.fade_start, part.fade_end,
               part.from, part.until, part.blend) = key;
      part.first = static_cast<uint32_t>(set.locals_.size());
      part.count = static_cast<uint32_t>(placed.matrices.size());
      set.parts_.push_back(part);
      set.locals_.insert(set.locals_.end(), placed.matrices.begin(),
                         placed.matrices.end());
      set.anchors_.insert(set.anchors_.end(), placed.anchors.begin(),
                          placed.anchors.end());
    }
    if (!progress.owned.empty())
      ecs.Add<MeshOwnerComponent>(entity).meshes_ =
          std::move(progress.owned);
    auto& sources = ecs.Add<InstanceSourceComponent>(entity);
    sources.sources_ = std::move(progress.sources);
    sources.stage_ = progress.stage;
    sources.cell_ = progress.cell;
    ecs.Add<ProceduralInstanceComponent>(entity, source);
    progress.instances.clear();
    progress.owned.clear();
    progress.sources.clear();
    if (made)
      made->push_back(entity);
  }
  return true;
}

// The largest bounding radius of the meshes the entity and its children draw,
// in its space
static float _BodyRadius(ECS& ecs, Entity entity) {
  float radius = 0.0f;
  if (const auto* renderer = ecs.Reg().try_get<MeshRendererComponent>(entity);
      renderer && renderer->mesh_)
    radius = renderer->mesh_->BoundsRadius();
  for (const Entity child : ecs.Get<TransformComponent>(entity).children_)
    radius = std::max(radius, _BodyRadius(ecs, child));
  return radius;
}

std::vector<Entity> RegionInstancer::InstanceHerds(
    ECS& ecs, const Worldgen::Region& region,
    const Worldgen::RegionContent& content, Entity source,
    const std::filesystem::path& root, const Worldgen::TextureInput* planet,
    const ObjectModels* models) {
  Diagnostics::ScopedSection section("Creatures");
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
  // Its navmesh for them to roam on (HerdMovement), in the planet's space
  if (!region.herds.empty() && region.navmesh.polygons > 0) {
    auto& herd = ecs.Reg().get_or_emplace<HerdComponent>(source);
    herd.path_ = std::make_shared<Worldgen::NavMesh::PathfinderMesh>(
        region.navmesh.path);
    herd.origin_ =
        glm::vec3(region.origin[0], region.origin[1], region.origin[2]);
  }
  // The matrices hold their scale. Skinned models animate (ModelInstantiator)
  for (size_t spawn = 0;
       spawn < region.herds.size() && spawn < content.herd_ids.size();
       ++spawn) {
    const std::string& id = content.herd_ids[spawn];
    const std::vector<float>& matrices = region.herds[spawn].matrices;
    const uint64_t kind_seed = _KindSeed(content, spawn, planet);
    // The placement stands every creature of the spawn on one spot (the
    // game's runtime spreads them out): they go round it on a sunflower
    // spiral a body apart, on spots the navmesh reaches
    float spacing = 0.0f;
    int slot = 0;
    for (size_t at = 0; !id.empty() && at + 12 <= matrices.size(); at += 12) {
      std::array<float, 16> rows{};  // 12 floats: rows without w
      for (int row = 0; row < 4; ++row)
        for (int k = 0; k < 3; ++k)
          rows[row * 4 + k] = matrices[at + row * 3 + k];
      rows[15] = 1.0f;
      const Entity entity =
          CreateObject(ecs, id, source, kind_seed, root, planet, models);
      if (entity == entt::null)
        break;  // the source is missing: skip the rest of this kind
      const glm::vec3 anchor(rows[12], rows[13], rows[14]);
      if (at == 0)
        spacing = 2.2f * _BodyRadius(ecs, entity) *
                  glm::length(glm::vec3(rows[0], rows[1], rows[2]));
      else if (spacing > 0.0f && ground) {
        const glm::vec3 up = ground->Up();
        const glm::vec3 side = glm::normalize(glm::cross(
            up,
            std::abs(up.x) < 0.9f ? glm::vec3(1, 0, 0) : glm::vec3(0, 1, 0)));
        const glm::vec3 ahead = glm::cross(up, side);
        glm::vec3 hit;
        const float height =
            ground->Under(anchor, hit) ? glm::dot(anchor - hit, up) : 0.0f;
        const auto* herd = ecs.Reg().try_get<HerdComponent>(source);
        for (int tries = 0; tries < 8; ++tries) {
          ++slot;
          const float angle = 2.39996323f * static_cast<float>(slot);
          const glm::vec3 spot =
              anchor + (side * std::cos(angle) + ahead * std::sin(angle)) *
                           (spacing * std::sqrt(static_cast<float>(slot)));
          if (!ground->Under(spot, hit))
            continue;
          const glm::vec3 placed = hit + up * height;
          if (herd && herd->path_) {
            const glm::vec3 world = placed + herd->origin_;
            if (Worldgen::NavMesh::FindPolygon(*herd->path_,
                                               {world.x, world.y, world.z},
                                               herd->settings_.nav_reach) < 0)
              continue;
          }
          rows[12] = placed.x;
          rows[13] = placed.y;
          rows[14] = placed.z;
          break;
        }
      }
      _SetTransform(ecs.Get<TransformComponent>(entity), rows);
      ecs.Add<ProceduralInstanceComponent>(entity, source);
      if (auto* animator = ecs.Reg().try_get<AnimatorComponent>(entity);
          animator && ground)
        animator->ground_ = source;
      // Resting from a random share of the longest rest, so they set out
      // apart
      auto& roam = ecs.Add<RoamComponent>(entity);
      roam.home_ = glm::vec3(rows[12], rows[13], rows[14]);
      roam.random_ = (static_cast<uint32_t>(kind_seed ^ (kind_seed >> 32)) ^
                      static_cast<uint32_t>(at * 2654435761u)) |
                     1u;
      if (const auto* herd = ecs.Reg().try_get<HerdComponent>(source))
        roam.rest_ = herd->settings_.rest_max *
                     ((roam.random_ >> 8) * (1.0f / 16777216.0f));
      glm::vec3 hit;
      if (ground && ground->Under(roam.home_, hit))
        roam.height_ = glm::dot(roam.home_ - hit, ground->Up());
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
      it->second.matrices.push_back(matrix);
      it->second.anchors.push_back(glm::vec3(place[3]));
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
  Diagnostics::ScopedSection section("Buildings");
  std::vector<Entity> entities;
  if (!pieces.models.empty()) {
    auto [entity, transform] = ecs.CreateEntity("Building pieces", source);
    auto& set = ecs.Add<InstancedMeshComponent>(entity);
    for (const auto& [model, placed] : pieces.models) {
      const uint32_t first = static_cast<uint32_t>(set.locals_.size());
      set.locals_.insert(set.locals_.end(), placed.matrices.begin(),
                         placed.matrices.end());
      set.anchors_.insert(set.anchors_.end(), placed.anchors.begin(),
                          placed.anchors.end());
      for (const auto& [mesh, material] :
           ModelEntityFactory::Meshes(model, root)) {
        InstancedMeshComponent::Part part;
        part.mesh = mesh;
        part.material = material;
        part.first = first;
        part.count = static_cast<uint32_t>(placed.matrices.size());
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
