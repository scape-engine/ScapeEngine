// ---------------------------------------------------------------------------
// scene_serializer.cpp
// Scene format (version 1):
// { "version": 1, "entities": [ { "name", "parent": <index|-1>, "position",
//   "rotation", "euler", "scale", "<component>": {...} } ],
//   "editor_camera": { "position", "rotation", "euler", "move_speed" } }
// "editor_camera" is optional.
// Parents are always written before their children.
// ---------------------------------------------------------------------------

#include "scene_serializer.h"

#include <functional>
#include <vector>

#include "engine/context/engine_context.h"
#include "engine/core/logger.h"
#include "engine/ecs/components/fade_component.h"
#include "engine/ecs/components/procedural_instance_component.h"
#include "engine/ecs/components/pcg_world_component.h"
#include "engine/ecs/components/viewer_component.h"
#include "engine/ecs/components/volume_component.h"
#include "engine/ecs/world.h"
#include "engine/pcg/generator_profile.h"
#include "engine/pcg/generator_resource.h"
#include "engine/pcg/procworld/generator_settings.h"
#include "engine/scene/glm_json.h"

namespace fs = std::filesystem;

constexpr int scene_version = 1;

/* ---------- components (persisted fields only) ---------- */
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(CameraComponent, enabled_, fov_,
                                                near_plane_, far_plane_)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
    DirectionalLightComponent, enabled_, color_, intensity_, cast_shadows_,
    shadow_bias_, normal_bias_, shadow_resolution_, shadow_softness_)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
    PointLightComponent, enabled_, color_, intensity_, range_, falloff_,
    cast_shadows_, shadow_bias_, shadow_resolution_, shadow_softness_)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
    SpotlightComponent, enabled_, color_, intensity_, range_, falloff_,
    inner_angle_, outer_angle_, cast_shadows_, shadow_bias_, shadow_resolution_,
    shadow_softness_)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(FadeComponent, visibility_,
                                                target_, duration_,
                                                destroy_when_hidden_)
// recapture_ is a one-shot request, not state
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
    SkyLightComponent, enabled_, color_, intensity_, cast_shadows_,
    source_type_, cubemap_path_, source_cubemap_angle_, real_time_capture_,
    lower_hemisphere_is_solid_color_, lower_hemisphere_color_)

// TODO: graph generators (volumes keep only region generators)
// TODO: material overrides (materials currently come only from the glTF
// source)
// TODO: move world settings (sky, post-process) into ECS components so
// SceneSerializer covers them

/* ---------- helpers ---------- */
template <typename T>
static void _Write(ECS& world, Entity entity, nlohmann::json& out,
                   const char* key) {
  if (world.Has<T>(entity))
    out[key] = world.Get<T>(entity);
}

template <typename T>
static void _Read(ECS& world, Entity entity, const nlohmann::json& in,
                  const char* key) {
  if (auto it = in.find(key); it != in.end())
    world.Add<T>(entity, it->get<T>());
}

// Paths outside the project stay absolute
static std::string _Relative(const std::string& path, const fs::path& root) {
  if (path.empty())
    return path;
  std::error_code ec;
  const fs::path relative = fs::relative(path, root, ec);
  if (ec || relative.empty() || *relative.begin() == "..")
    return path;
  return relative.generic_string();
}

static std::string _Absolute(const std::string& path, const fs::path& root) {
  if (path.empty() || fs::path(path).is_absolute())
    return path;
  return (root / path).string();
}

// A volume and its generator settings (resources are runtime only); a
// generator with a profile keeps only its overrides
static nlohmann::json _WriteVolume(const VolumeComponent& volume,
                                   const fs::path& root) {
  nlohmann::json out = {{"bounds_min", volume.bounds_min},
                        {"bounds_max", volume.bounds_max},
                        {"resolution", volume.resolution},
                        {"lod_level", volume.lod_level}};
  auto generator =
      EngineContext::resourceManager().GetResourceAs<PCG::GeneratorResource>(
          volume.generator_id);
  if (!generator || !generator->Get())
    return out;
  if (!generator->edits_.empty()) {
    out["edits"] = generator->edits_;
    if (generator->edit_layer_ &&
        !PCG::SaveEditLayer(_Absolute(generator->edits_, root),
                            *generator->edit_layer_))
      Logger::getInstance().Log(
          LogLevel::Error,
          "SceneSerializer: can't write edit layer " + generator->edits_);
  }
  nlohmann::json settings = ProcWorld::WriteSettings(*generator->Get());
  if (settings.is_null())
    return out;
  if (generator->profile_.empty()) {
    out["generator"] = std::move(settings);
  } else {
    // Only what differs from the profile; all of it when the profile is gone
    out["generator"] = {
        {"profile", generator->profile_},
        {"overrides",
         PCG::ProfileOverrides(
             PCG::LoadProfile(_Absolute(generator->profile_, root)),
             settings)}};
  }
  return out;
}

static void _ReadVolume(VolumeComponent& volume, const nlohmann::json& in,
                        const fs::path& root) {
  volume.bounds_min = in.value("bounds_min", volume.bounds_min);
  volume.bounds_max = in.value("bounds_max", volume.bounds_max);
  volume.resolution = in.value("resolution", volume.resolution);
  volume.lod_level = in.value("lod_level", volume.lod_level);
  auto it = in.find("generator");
  if (it == in.end())
    return;
  nlohmann::json settings = *it;
  std::string profile;
  if (auto path = it->find("profile"); path != it->end()) {
    profile = path->get<std::string>();
    settings = PCG::LoadProfile(_Absolute(profile, root));
    if (settings.is_null())
      Logger::getInstance().Log(LogLevel::Warning,
                                "SceneSerializer: missing profile " + profile);
    settings.merge_patch(it->value("overrides", nlohmann::json::object()));
  }
  auto generator = ProcWorld::CreateGenerator(settings);
  if (!generator)
    return;
  auto [id, resource] =
      EngineContext::resourceManager().Create<PCG::GeneratorResource>(
          PCG::GetGeneratorTypeName(generator->GetType()));
  resource->SetGenerator(std::move(generator));
  resource->profile_ = profile;
  resource->edits_ = in.value("edits", std::string());
  if (!resource->edits_.empty()) {
    auto layer = std::make_shared<PCG::EditLayer>();
    if (!PCG::LoadEditLayer(_Absolute(resource->edits_, root), *layer))
      Logger::getInstance().Log(
          LogLevel::Warning,
          "SceneSerializer: unreadable edit layer " + resource->edits_);
    resource->edit_layer_ = std::move(layer);
  }
  volume.generator_id = id;
  volume.dirty = true;
}

/* ---------- serialize ---------- */
nlohmann::json SceneSerializer::Serialize(ECS& world, const fs::path& root) {
  nlohmann::json entities = nlohmann::json::array();

  // Depth-first, so a parent is always written before its children and
  // children keep their order
  std::function<void(Entity, int)> write = [&](Entity entity, int parent) {
    // Made by a PCG system: remade from its source on load
    if (world.Has<ProceduralInstanceComponent>(entity))
      return;
    const TransformComponent& transform = world.Get<TransformComponent>(entity);
    const int index = static_cast<int>(entities.size());

    nlohmann::json out = {
        {"name", transform.name_},          {"parent", parent},
        {"position", transform.position_},  {"rotation", transform.rotation_},
        {"euler", transform.euler_angles_}, {"scale", transform.scale_}};

    _Write<CameraComponent>(world, entity, out, "camera");
    _Write<DirectionalLightComponent>(world, entity, out, "directional_light");
    _Write<PointLightComponent>(world, entity, out, "point_light");
    _Write<SpotlightComponent>(world, entity, out, "spotlight");
    _Write<FadeComponent>(world, entity, out, "fade");

    if (world.Has<SkyLightComponent>(entity)) {
      SkyLightComponent sky = world.Get<SkyLightComponent>(entity);
      sky.cubemap_path_ = _Relative(sky.cubemap_path_, root);
      out["sky_light"] = sky;
    }

    if (world.Has<PCGWorldComponent>(entity))
      out["pcg_world"] = nlohmann::json::object();

    if (world.Has<ViewerComponent>(entity))
      out["viewer"] = nlohmann::json::object();

    if (world.Has<VolumeComponent>(entity))
      out["volume"] = _WriteVolume(world.Get<VolumeComponent>(entity), root);

    if (world.Has<MeshRendererComponent>(entity)) {
      const auto& renderer = world.Get<MeshRendererComponent>(entity);
      out["mesh_renderer"] = {{"source", renderer.source_},
                              {"mesh_index", renderer.mesh_index_},
                              {"enabled", renderer.enabled_}};
    }

    entities.push_back(std::move(out));

    // Copy: the reference above may not outlive the recursion
    const std::vector<Entity> children = transform.children_;
    for (Entity child : children)
      write(child, index);
  };

  // Roots oldest first (views iterate newest first), so loading recreates
  // them in the same order
  std::vector<Entity> roots;
  for (auto [entity, transform] : world.View<TransformComponent>().each()) {
    if (transform.parent_ == entt::null)
      roots.push_back(entity);
  }
  for (auto it = roots.rbegin(); it != roots.rend(); ++it)
    write(*it, -1);

  return {{"version", scene_version}, {"entities", std::move(entities)}};
}

/* ---------- deserialize ---------- */
bool SceneSerializer::Deserialize(ECS& world, const nlohmann::json& scene,
                                  const fs::path& root) {
  if (!scene.is_object() || scene.value("version", 0) != scene_version) {
    Logger::getInstance().Log(LogLevel::Error,
                              "[SceneSerializer] Unsupported scene version");
    return false;
  }

  world.Clear();
  try {
    std::vector<Entity> created;
    for (const nlohmann::json& in : scene.at("entities")) {
      auto [entity, transform] =
          world.CreateEntity(in.value("name", std::string("Entity")));
      transform.position_ = in.value("position", transform.position_);
      transform.rotation_ = in.value("rotation", transform.rotation_);
      transform.euler_angles_ = in.value("euler", transform.euler_angles_);
      transform.scale_ = in.value("scale", transform.scale_);

      // Parents are written first, so the index is already created
      const int parent = in.value("parent", -1);
      if (parent >= 0 && parent < static_cast<int>(created.size()))
        world.SetParent(entity, created[parent]);

      _Read<CameraComponent>(world, entity, in, "camera");
      _Read<DirectionalLightComponent>(world, entity, in, "directional_light");
      _Read<PointLightComponent>(world, entity, in, "point_light");
      _Read<SpotlightComponent>(world, entity, in, "spotlight");
      _Read<FadeComponent>(world, entity, in, "fade");

      if (auto it = in.find("sky_light"); it != in.end()) {
        auto& sky =
            world.Add<SkyLightComponent>(entity, it->get<SkyLightComponent>());
        sky.cubemap_path_ = _Absolute(sky.cubemap_path_, root);
      }

      if (in.contains("pcg_world"))
        world.Add<PCGWorldComponent>(entity);

      if (in.contains("viewer"))
        world.Add<ViewerComponent>(entity);

      if (auto it = in.find("volume"); it != in.end())
        _ReadVolume(world.Add<VolumeComponent>(entity), *it, root);

      if (auto it = in.find("mesh_renderer"); it != in.end()) {
        auto& renderer = world.Add<MeshRendererComponent>(entity);
        renderer.source_ = it->value("source", std::string());
        renderer.mesh_index_ = it->value("mesh_index", 0u);
        renderer.enabled_ = it->value("enabled", true);
      }

      created.push_back(entity);
    }
  } catch (const nlohmann::json::exception& e) {
    Logger::getInstance().Log(
        LogLevel::Error,
        "[SceneSerializer] Invalid scene: " + std::string(e.what()));
    world.Clear();
    return false;
  }
  return true;
}

/* ---------- editor camera ---------- */
void SceneSerializer::WriteEditorCamera(nlohmann::json& scene,
                                        const TransformComponent& camera,
                                        float move_speed) {
  scene["editor_camera"] = {{"position", camera.position_},
                            {"rotation", camera.rotation_},
                            {"euler", camera.euler_angles_},
                            {"move_speed", move_speed}};
}

bool SceneSerializer::ReadEditorCamera(const nlohmann::json& scene,
                                       TransformComponent& camera,
                                       float& move_speed) {
  auto it = scene.find("editor_camera");
  if (it == scene.end() || !it->is_object())
    return false;
  try {
    const glm::vec3 position = it->at("position").get<glm::vec3>();
    const glm::quat rotation = it->at("rotation").get<glm::quat>();
    const glm::vec3 euler = it->at("euler").get<glm::vec3>();
    move_speed = it->value("move_speed", move_speed);
    camera.position_ = position;
    camera.rotation_ = rotation;
    camera.euler_angles_ = euler;
    camera.modified_ = true;
  } catch (const nlohmann::json::exception& e) {
    Logger::getInstance().Log(
        LogLevel::Error,
        "[SceneSerializer] Invalid editor camera: " + std::string(e.what()));
    return false;
  }
  return true;
}
