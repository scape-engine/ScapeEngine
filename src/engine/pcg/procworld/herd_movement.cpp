#include "herd_movement.h"

#include <algorithm>
#include <cctype>
#include <map>
#include <tuple>

#include <glm/gtc/quaternion.hpp>

#include "engine/animation/animation.h"
#include "engine/ecs/components/ground_component.h"
#include "engine/ecs/components/transform_component.h"

#include "content/creature_gait.h"
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

// The clip's StrideVelocity, measured once per model and clip (models live
// as long as their cache)
static glm::vec3 _Stride(const SkeletalData& data, int clip) {
  static std::map<std::pair<const SkeletalData*, int>, glm::vec3> strides;
  auto [it, added] = strides.try_emplace({&data, clip}, glm::vec3(0.0f));
  if (added)
    it->second = StrideVelocity(data, clip);
  return it->second;
}

static void _Play(AnimatorComponent* animator, int clip, float fade,
                  float speed) {
  if (!animator || clip < 0)
    return;
  Animation::Play(*animator, clip, fade);
  animator->speed_ = speed;
}

static void _Play(AnimatorComponent* animator, const std::string& name,
                  float fade, float speed) {
  if (animator && animator->data_)
    _Play(animator, _FindClip(*animator->data_, name), fade, speed);
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
        auto* animator = registry.try_get<AnimatorComponent>(entity);
        if (animator)
          animator->settings_ = settings.animation;
        if (!settings.roam || !herd->path_ ||
            glm::distance(glm::vec3(own.model_[3]), viewer) >
                settings.roam_distance)
          return;

        // The creature's gaits: its model's, else its walk clip at the pace
        // its planted feet keep; the way the model faces: the setting, else
        // the way its first gait's clip walks, else +z
        const SkeletalData* data = animator ? animator->data_ : nullptr;
        const GaitSet* set = animator && animator->gaits_ &&
                                     !animator->gaits_->gaits.empty()
                                 ? animator->gaits_.get()
                                 : nullptr;
        static std::vector<Worldgen::Gait> gaits;
        static std::vector<int> gait_clips;
        gaits.clear();
        gait_clips.clear();
        if (set)
          for (const GaitSet::Gait& g : set->gaits) {
            gaits.push_back({g.pace, g.min_rate, g.max_rate,
                             g.clip_index >= 0 && g.pace > 0.0f});
            gait_clips.push_back(g.clip_index);
          }
        const int first =
            set ? set->gaits[0].clip_index
                : data ? _FindClip(*data, settings.walk_clip) : -1;
        const glm::vec3 stride =
            data && first >= 0 ? _Stride(*data, first) : glm::vec3(0.0f);
        glm::vec3 forward(0, 0, 1);
        if (glm::length(settings.forward) > 1e-4f)
          forward = glm::normalize(settings.forward);
        else if (glm::length(stride) > 1e-4f)
          forward = glm::normalize(stride);
        const float scale = glm::length(own.scale_ * forward);
        if (!set) {
          const float pace = std::max(glm::dot(stride, forward), 0.0f);
          gaits.push_back({pace, settings.walk_min_rate, settings.walk_max_rate,
                           first >= 0 && pace > 1e-4f});
          gait_clips.push_back(first);
        }
        const float speed =
            settings.walk_speed * (set ? set->speed_scale : 1.0f);
        float reached = 0.0f;
        const int gait =
            settings.match_stride && scale > 0.0f
                ? Worldgen::PickGait(gaits.data(),
                                     static_cast<int>(gaits.size()), scale,
                                     speed, &reached)
                : -1;
        const int clip = gait >= 0 ? gait_clips[gait] : first;
        const float rate =
            gait >= 0 ? Worldgen::GaitRate(gaits[gait], scale, reached) : 1.0f;

        if (roam.waypoints_.empty()) {
          roam.rest_ -= dt;
          if (roam.rest_ > 0.0f)
            return;
          roam.rest_ = settings.rest_min + (settings.rest_max -
                                            settings.rest_min) *
                                               _Random(roam.random_);
          _SetOut(*herd, own, roam);
          if (!roam.waypoints_.empty())
            _Play(animator, clip, settings.fade / std::max(rate, 1e-3f), rate);
          return;
        }
        // A new gait as the speed or size changes
        if (animator && clip >= 0 && animator->clip_ != clip)
          _Play(animator, clip, settings.fade / std::max(rate, 1e-3f), rate);

        // Turn towards the next corner across up, and walk ahead as far as
        // it faces it
        const glm::vec3 up = glm::normalize(own.position_ + herd->origin_);
        glm::vec3 to = roam.waypoints_.front() - own.position_;
        to -= up * glm::dot(to, up);
        const float step =
            (gait >= 0 && settings.keep_pace ? reached : speed) * dt;
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
        const float moved = step * std::max(std::cos(off - turn), 0.0f);
        own.position_ += facing * moved;
        // The gait plays as fast as the ground covered asks, within its rates
        if (animator && gait >= 0 && animator->clip_ == clip && dt > 0.0f)
          animator->speed_ =
              Worldgen::GaitRate(gaits[gait], scale, moved / dt);

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
