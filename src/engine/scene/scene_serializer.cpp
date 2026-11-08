// ---------------------------------------------------------------------------
// scene_serializer.cpp
// Scene format (version 1):
// { "version": 1, "entities": [ { "name", "parent": <index|-1>, "position",
//   "rotation", "euler", "scale", "<component>": {...} } ] }
// Parents are always written before their children.
// ---------------------------------------------------------------------------

#include "scene_serializer.h"

#include <functional>
#include <vector>

#include "engine/core/logger.h"
#include "engine/ecs/world.h"

namespace fs = std::filesystem;

constexpr int scene_version = 1;

/* ---------- glm ---------- */
namespace nlohmann {
template <>
struct adl_serializer<glm::vec3> {
  static void to_json(json& j, const glm::vec3& v) { j = {v.x, v.y, v.z}; }
  static void from_json(const json& j, glm::vec3& v) {
    v = glm::vec3(j.at(0).get<float>(), j.at(1).get<float>(),
                  j.at(2).get<float>());
  }
};

// Stored as w, x, y, z
template <>
struct adl_serializer<glm::quat> {
  static void to_json(json& j, const glm::quat& q) { j = {q.w, q.x, q.y, q.z}; }
  static void from_json(const json& j, glm::quat& q) {
    q = glm::quat(j.at(0).get<float>(), j.at(1).get<float>(),
                  j.at(2).get<float>(), j.at(3).get<float>());
  }
};
}  // namespace nlohmann

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
// recapture_ is a one-shot request, not state
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
    SkyLightComponent, enabled_, color_, intensity_, cast_shadows_,
    source_type_, cubemap_path_, source_cubemap_angle_, real_time_capture_,
    lower_hemisphere_is_solid_color_, lower_hemisphere_color_)

// TODO: VolumeComponent serialization (no scene uses it yet)
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

/* ---------- serialize ---------- */
nlohmann::json SceneSerializer::Serialize(ECS& world, const fs::path& root) {
  nlohmann::json entities = nlohmann::json::array();

  // Depth-first, so a parent is always written before its children and
  // children keep their order
  std::function<void(Entity, int)> write = [&](Entity entity, int parent) {
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

    if (world.Has<SkyLightComponent>(entity)) {
      SkyLightComponent sky = world.Get<SkyLightComponent>(entity);
      sky.cubemap_path_ = _Relative(sky.cubemap_path_, root);
      out["sky_light"] = sky;
    }

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

      if (auto it = in.find("sky_light"); it != in.end()) {
        auto& sky =
            world.Add<SkyLightComponent>(entity, it->get<SkyLightComponent>());
        sky.cubemap_path_ = _Absolute(sky.cubemap_path_, root);
      }

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
