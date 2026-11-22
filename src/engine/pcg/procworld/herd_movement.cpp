#include "herd_movement.h"

#include <algorithm>
#include <cctype>
#include <map>
#include <tuple>

#include <glm/gtc/quaternion.hpp>

#include "engine/animation/animation.h"
#include "engine/ecs/components/ground_component.h"
#include "engine/ecs/components/transform_component.h"

#include "placement/nav_query.h"

namespace ProcWorld::HerdMovement {

namespace NavMesh = Worldgen::NavMesh;

static float _Random(uint32_t& state) {
  state ^= state << 13;
  state ^= state >> 17;
  state ^= state << 5;
  return (state >> 8) * (1.0f / 16777216.0f);
}

// The first clip whose name holds `name`, in any case; -1: none
static int _FindClip(const SkeletalData& data, const std::string& name) {
  if (name.empty())
    return -1;
  const auto lower = [](std::string s) {
    for (char& c : s)
      c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
  };
  const std::string wanted = lower(name);
  for (size_t i = 0; i < data.clips.size(); ++i)
    if (lower(data.clips[i].name).find(wanted) != std::string::npos)
      return static_cast<int>(i);
  return -1;
}

// The clip's StrideSpeed, measured once per model and clip (models live as
// long as their cache)
static float _Stride(const SkeletalData& data, int clip,
                     const glm::vec3& forward) {
  static std::map<std::tuple<const SkeletalData*, int, float, float, float>,
                  float>
      strides;
  auto [it, added] = strides.try_emplace(
      {&data, clip, forward.x, forward.y, forward.z}, 0.0f);
  if (added)
    it->second = StrideSpeed(data, clip, forward);
  return it->second;
}

static void _Play(AnimatorComponent* animator, const std::string& name,
                  float fade, float speed) {
  if (!animator || !animator->data_)
    return;
  const int clip = _FindClip(*animator->data_, name);
  if (clip < 0)
    return;
  Animation::Play(*animator, clip, fade);
  animator->speed_ = speed;
}

// Sets out for a point near home: the navmesh way there, or none
static void _SetOut(const HerdComponent& herd, const TransformComponent& own,
                    RoamComponent& roam) {
  const HerdSettings& settings = herd.settings_;
  const glm::vec3 up = glm::normalize(roam.home_ + herd.origin_);
  const glm::vec3 side = glm::normalize(glm::cross(
      up, std::abs(up.x) < 0.9f ? glm::vec3(1, 0, 0) : glm::vec3(0, 1, 0)));
  const glm::vec3 ahead = glm::cross(up, side);
  const float angle = 6.2831853f * _Random(roam.random_);
  const float radius = settings.roam_radius * std::sqrt(_Random(roam.random_));
  const glm::vec3 target =
      roam.home_ + (side * std::cos(angle) + ahead * std::sin(angle)) * radius;

  const auto point = [&](const glm::vec3& p) {
    const glm::vec3 planet = p + herd.origin_;
    return NavMesh::Point{planet.x, planet.y, planet.z};
  };
  const NavMesh::Point start = point(own.position_), end = point(target);
  const int from = NavMesh::FindPolygon(*herd.path_, start, settings.nav_reach);
  const int to = NavMesh::FindPolygon(*herd.path_, end, settings.nav_reach);
  if (from < 0 || to < 0)
    return;
  const std::vector<int> polygons = NavMesh::FindPath(*herd.path_, from, to);
  if (polygons.empty())
    return;
  for (const NavMesh::Point& w :
       NavMesh::Waypoints(*herd.path_, polygons, start, end))
    roam.waypoints_.push_back(glm::vec3(w[0], w[1], w[2]) - herd.origin_);
}

void Update(ECS& ecs, float dt) {
  auto& registry = ecs.Reg();
  const glm::vec3 viewer = Animation::Viewer();
  registry.view<RoamComponent, TransformComponent>().each(
      [&](Entity entity, RoamComponent& roam, TransformComponent& own) {
        const auto* herd = registry.try_get<HerdComponent>(own.parent_);
        if (!herd)
          return;
        const HerdSettings& settings = herd->settings_;
        const glm::vec3 forward = glm::length(settings.forward) > 1e-4f
                                      ? glm::normalize(settings.forward)
                                      : glm::vec3(0, 0, 1);
        auto* animator = registry.try_get<AnimatorComponent>(entity);
        if (animator)
          animator->settings_ = settings.animation;
        if (!settings.roam || !herd->path_ ||
            glm::distance(glm::vec3(own.model_[3]), viewer) >
                settings.roam_distance)
          return;

        if (roam.waypoints_.empty()) {
          roam.rest_ -= dt;
          if (roam.rest_ > 0.0f)
            return;
          roam.rest_ = settings.rest_min + (settings.rest_max -
                                            settings.rest_min) *
                                               _Random(roam.random_);
          _SetOut(*herd, own, roam);
          if (roam.waypoints_.empty() || !animator || !animator->data_)
            return;
          const int walk = _FindClip(*animator->data_, settings.walk_clip);
          const float stride =
              settings.match_stride && walk >= 0
                  ? _Stride(*animator->data_, walk, forward) *
                        glm::length(own.scale_ * forward)
                  : 0.0f;
          _Play(animator, settings.walk_clip, settings.fade,
                stride > 1e-4f ? settings.walk_speed / stride : 1.0f);
          return;
        }

        // Turn towards the next corner across up, and walk ahead as far as
        // it faces it
        const glm::vec3 up = glm::normalize(own.position_ + herd->origin_);
        glm::vec3 to = roam.waypoints_.front() - own.position_;
        to -= up * glm::dot(to, up);
        const float step = settings.walk_speed * dt;
        if (glm::length(to) <= std::max(step, 1e-3f)) {
          roam.waypoints_.erase(roam.waypoints_.begin());
          if (roam.waypoints_.empty())
            _Play(animator, settings.rest_clip, settings.fade, 1.0f);
          return;
        }
        glm::vec3 facing = own.rotation_ * forward;
        facing -= up * glm::dot(facing, up);
        facing = glm::length(facing) > 1e-4f ? glm::normalize(facing)
                                             : glm::normalize(to);
        const glm::vec3 way = glm::normalize(to);
        const float off = std::atan2(glm::dot(glm::cross(facing, way), up),
                                     glm::dot(facing, way));
        const float limit = glm::radians(settings.turn_rate) * dt;
        const float turn = std::clamp(off, -limit, limit);
        facing = glm::angleAxis(turn, up) * facing;
        own.position_ += facing * step * std::max(std::cos(off - turn), 0.0f);

        // On the ground, at the height it was placed over it (along the
        // ground's up, as it was measured)
        const glm::vec3 now_up = glm::normalize(own.position_ + herd->origin_);
        if (const auto* ground = registry.try_get<GroundComponent>(own.parent_);
            ground && ground->mesh_) {
          glm::vec3 hit;
          if (ground->mesh_->Under(own.position_, hit))
            own.position_ = hit + ground->mesh_->Up() * roam.height_;
        }
        // The rotation taking the model's up (+y) and forward to these
        const glm::vec3 model_side = glm::normalize(
            glm::cross(glm::vec3(0, 1, 0), forward));
        const glm::mat3 model(model_side, glm::vec3(0, 1, 0),
                              glm::cross(model_side, glm::vec3(0, 1, 0)));
        const glm::vec3 side = glm::normalize(glm::cross(now_up, facing));
        own.rotation_ = glm::quat_cast(
            glm::mat3(side, now_up, glm::cross(side, now_up)) *
            glm::transpose(model));
        own.euler_angles_ = glm::degrees(glm::eulerAngles(own.rotation_));
        own.modified_ = true;
      });
}

}  // namespace ProcWorld::HerdMovement
