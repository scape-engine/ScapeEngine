#include "instance_edits.h"

#include <imgui.h>

#include <cstring>
#include <functional>
#include <memory>
#include <optional>
#include <random>
#include <string>

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/type_ptr.hpp>

#include "editor/gui/components/im_components.h"
#include "editor/gui/components/inspectable_components.h"
#include "editor/gui/styles/editor_styles.h"
#include "editor/runtime/runtime.h"
#include "editor/vendor/IconFontCppHeaders/IconsFontAwesome6.h"
#include "engine/context/engine_context.h"
#include "engine/ecs/components/fade_component.h"
#include "engine/ecs/components/instance_source_component.h"
#include "engine/ecs/components/instanced_mesh_component.h"
#include "engine/ecs/components/volume_component.h"
#include "engine/pcg/pcg_engine.h"
#include "engine/ecs/world.h"

namespace InstanceEdits {

namespace {

// An instance that takes edits: its object's source and its generator
struct Target {
  Entity volume = entt::null;
  std::shared_ptr<PCG::GeneratorResource> resource;
  const InstanceSourceComponent* sources = nullptr;
  const InstanceSourceComponent::Source* source = nullptr;
};

// An instance of the entity's set, or (instance -1) the entity as an object
// of its own
std::optional<Target> _Target(Entity entity, int instance) {
  auto& ecs = ECS::Main();
  if (!ecs.Reg().valid(entity))
    return std::nullopt;
  const auto* set = ecs.Reg().try_get<InstancedMeshComponent>(entity);
  const auto* sources = ecs.Reg().try_get<InstanceSourceComponent>(entity);
  if (!sources || sources->stage_.empty())
    return std::nullopt;
  Target target;
  target.sources = sources;
  if (!set && instance < 0 && sources->sources_.size() == 1)
    target.source = &sources->sources_.front();
  else if (set && instance >= 0 &&
           set->anchors_.size() == set->locals_.size() &&
           instance < static_cast<int>(set->anchors_.size()))
    target.source = sources->Find(set->anchors_[instance]);
  target.volume = InspectableComponents::FindOrigin(entity).volume;
  if (!target.source || target.volume == entt::null)
    return std::nullopt;
  target.resource =
      EngineContext::resourceManager().GetResourceAs<PCG::GeneratorResource>(
          ecs.Get<VolumeComponent>(target.volume).generator_id);
  if (!target.resource)
    return std::nullopt;
  return target;
}

// The edit on a placed instance; null: none
const PCG::InstanceEdit* _EditOf(const Target& target) {
  const auto& layer = target.resource->edit_layer_;
  const PCG::CellEdits* cell =
      layer ? layer->Find(target.sources->stage_, target.sources->cell_)
            : nullptr;
  if (!cell || target.source->key.addition >= 0)
    return nullptr;
  for (const PCG::InstanceEdit& edit : cell->edits)
    if (edit.object == target.source->key.object &&
        edit.index == target.source->key.index)
      return &edit;
  return nullptr;
}

// The instance, once made again after an edit, to select
struct Follow {
  Entity volume = entt::null;
  Entity from = entt::null;  // the entity it was selected in
  std::string stage;
  uint64_t cell = 0;
  PCG::InstanceKey key;
  int frames = 0;  // given up on after these
};
Follow s_follow;

// A copy of the generator's layer, changed, handed to the generator; the
// layer's file defaults to one named after the volume
// TODO: undo and redo of edits; and saving them apart from the scene (they
// are written only when the scene is saved)
void _Change(Entity volume, PCG::GeneratorResource& resource,
             const std::string& stage, uint64_t cell_key,
             const std::function<void(PCG::EditLayer&, PCG::CellEdits&)>&
                 change) {
  auto layer = resource.edit_layer_
                   ? std::make_shared<PCG::EditLayer>(*resource.edit_layer_)
                   : std::make_shared<PCG::EditLayer>();
  change(*layer, layer->Cell(stage, cell_key));
  // Edits left with no change go, and a cell left with none
  auto& cells = layer->stages_[stage];
  if (const auto cell = cells.find(cell_key); cell != cells.end()) {
    std::erase_if(cell->second.edits, [](const PCG::InstanceEdit& edit) {
      return !edit.removed && !edit.position && !edit.rotation &&
             !edit.scale && edit.replacement.empty() && !edit.seed;
    });
    if (cell->second.edits.empty() && cell->second.additions.empty())
      cells.erase(cell);
  }
  if (cells.empty())
    layer->stages_.erase(stage);
  if (resource.edits_.empty())
    resource.edits_ =
        "edits/" + ECS::Main().Get<TransformComponent>(volume).name_ +
        ".edits";
  resource.edit_layer_ = std::move(layer);
  Runtime::Project().MarkSceneDirty();
}

void _Change(const Target& target,
             const std::function<void(PCG::EditLayer&, PCG::CellEdits&)>&
                 change) {
  _Change(target.volume, *target.resource, target.sources->stage_,
          target.sources->cell_, change);
}

// Changes the instance, then follows it into its remade cell
void _ChangeInstance(Entity entity, const Target& target,
                     const std::function<void(PCG::InstanceEdit&)>& placed,
                     const std::function<void(PCG::InstanceAddition&)>& added) {
  const PCG::InstanceKey& key = target.source->key;
  _Change(target, [&](PCG::EditLayer& layer, PCG::CellEdits& cell) {
    if (key.addition >= 0) {
      if (key.addition < static_cast<int>(cell.additions.size()))
        added(cell.additions[key.addition]);
    } else {
      placed(layer.Edit(target.sources->stage_, target.sources->cell_, key));
    }
  });
  s_follow = {target.volume, entity, target.sources->stage_,
              target.sources->cell_, key, 0};
}

// The position, rotation (degrees) and scale being typed for an instance,
// handed over once no field is active
struct Draft {
  Entity entity = entt::null;
  int instance = -1;
  glm::mat4 matrix{1.0f};       // what it was drawn from
  PCG::PointTransform drawn;    // that, as fields
  PCG::PointTransform typed;    // the fields now
  bool edited = false;
};
Draft s_draft;

PCG::PointMatrix _PointMatrix(const glm::mat4& m) {
  PCG::PointMatrix matrix;
  std::memcpy(matrix.data(), glm::value_ptr(m), sizeof(matrix));
  return matrix;
}

}  // namespace

bool Editable(Entity entity, int instance) {
  return _Target(entity, instance).has_value();
}

void SetSeed(Entity entity, int instance, uint64_t seed) {
  if (const auto target = _Target(entity, instance))
    _ChangeInstance(
        entity, *target, [&](PCG::InstanceEdit& edit) { edit.seed = seed; },
        [&](PCG::InstanceAddition& addition) { addition.seed = seed; });
}

bool Place(Entity volume, const std::string& object, const glm::vec3& point) {
  auto& ecs = ECS::Main();
  if (!ecs.Reg().valid(volume) || !ecs.Has<VolumeComponent>(volume))
    return false;
  const auto resource =
      EngineContext::resourceManager().GetResourceAs<PCG::GeneratorResource>(
          ecs.Get<VolumeComponent>(volume).generator_id);
  const auto placement =
      EngineContext::PCG().GetProcWorld().Placement(volume, object, point);
  if (!resource || !placement)
    return false;
  static std::mt19937_64 random{std::random_device{}()};
  const uint64_t seed = random();
  int index = 0;
  _Change(volume, *resource, placement->stage, placement->cell,
          [&](PCG::EditLayer&, PCG::CellEdits& cell) {
            index = static_cast<int>(cell.additions.size());
            cell.additions.push_back({object, seed, placement->matrix});
          });
  Runtime::State().ClearSelection();
  s_follow = {volume,          entt::null, placement->stage,
              placement->cell, {object, 0, {}, index}, 0};
  return true;
}

void Draw(Entity entity, int instance) {
  const auto target = _Target(entity, instance);
  if (!target)
    return;
  const InstanceSourceComponent::Source& source = *target->source;
  const PCG::InstanceEdit* edit = _EditOf(*target);

  // Where it stands in its cell
  if (s_draft.entity != entity || s_draft.instance != instance ||
      (!s_draft.edited && s_draft.matrix != source.matrix)) {
    const PCG::PointTransform drawn =
        PCG::Decompose(_PointMatrix(source.matrix));
    s_draft = {entity, instance, source.matrix, drawn, drawn};
  }
  // Each field is marked where it overrides what the generator placed, and
  // reverts on its own; an addition is all its own, so none are
  PCG::PointTransform& typed = s_draft.typed;
  const auto field = [&](const char* label, glm::vec3& value, float speed,
                         const char* tooltip, bool overridden,
                         std::optional<glm::vec3> PCG::InstanceEdit::*part) {
    IMComponents::MarkOverride(overridden);
    IMComponents::Input(label, value, speed, tooltip);
    if (IMComponents::RevertChosen()) {
      _ChangeInstance(
          entity, *target, [&](PCG::InstanceEdit& e) { (e.*part).reset(); },
          [](PCG::InstanceAddition&) {});
      s_draft.entity = entt::null;
    }
  };
  const PCG::PointTransform before = typed;
  // TODO: a gizmo in the viewport to move it, writing the same edit
  field("Position", typed.position, 0.1f, "Where it stands, in its cell",
        edit && edit->position, &PCG::InstanceEdit::position);
  field("Rotation", typed.rotation, 0.5f, "Degrees", edit && edit->rotation,
        &PCG::InstanceEdit::rotation);
  field("Scale", typed.scale, 0.01f, "", edit && edit->scale,
        &PCG::InstanceEdit::scale);
  s_draft.edited |= typed.position != before.position ||
                    typed.rotation != before.rotation ||
                    typed.scale != before.scale;
  // Handed over once no field is active: only the parts typed in
  if (s_draft.edited && !ImGui::IsAnyItemActive()) {
    const PCG::PointTransform& drawn = s_draft.drawn;
    _ChangeInstance(
        entity, *target,
        [&](PCG::InstanceEdit& e) {
          if (typed.position != drawn.position)
            e.position = typed.position;
          if (typed.rotation != drawn.rotation)
            e.rotation = typed.rotation;
          if (typed.scale != drawn.scale)
            e.scale = typed.scale;
        },
        [&](PCG::InstanceAddition& a) { a.matrix = PCG::Compose(typed); });
    s_draft.edited = false;
  }

  // What it is
  std::string object = edit ? edit->replacement : std::string();
  IMComponents::MarkOverride(edit && !edit->replacement.empty());
  const bool swapped = IMComponents::InputText(
      "Object", object, source.id.c_str(),
      "Another object to stand here (.object asset, project-relative); "
      "empty: its own");
  if (IMComponents::RevertChosen())
    _ChangeInstance(
        entity, *target, [](PCG::InstanceEdit& e) { e.replacement.clear(); },
        [](PCG::InstanceAddition&) {});
  else if (swapped)
    _ChangeInstance(
        entity, *target, [&](PCG::InstanceEdit& e) { e.replacement = object; },
        [&](PCG::InstanceAddition& a) {
          if (!object.empty())
            a.object = object;
        });

  uint64_t seed = source.seed;
  IMComponents::MarkOverride(edit && edit->seed);
  const bool seeded = IMComponents::Row("Seed", [&] {
    ImGui::InputScalar("##seed", ImGuiDataType_U64, &seed);
    return ImGui::IsItemDeactivatedAfterEdit();
  });
  if (IMComponents::RevertChosen())
    _ChangeInstance(
        entity, *target, [](PCG::InstanceEdit& e) { e.seed.reset(); },
        [](PCG::InstanceAddition&) {});
  else if (seeded)
    SetSeed(entity, instance, seed);
}

void DrawOverrides(Entity entity, int instance) {
  const auto target = _Target(entity, instance);
  if (!target)
    return;
  const PCG::InstanceKey key = target->source->key;
  const PCG::InstanceEdit* edit = _EditOf(*target);
  const bool added = key.addition >= 0;

  // Unity's prefab row: what overrides the generator, then removing it
  const float width =
      (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) *
      0.5f;
  ImGui::BeginDisabled(!edit && !added);
  if (ImGui::Button("Overrides " ICON_FA_CARET_DOWN, ImVec2(width, 0.0f)))
    ImGui::OpenPopup("##overrides");
  ImGui::EndDisabled();
  ImGui::SameLine();
  if (ImGui::Button("Remove", ImVec2(width, 0.0f))) {
    _Change(*target, [&](PCG::EditLayer& layer, PCG::CellEdits&) {
      if (added)
        layer.Revert(target->sources->stage_, target->sources->cell_, key);
      else
        layer.Edit(target->sources->stage_, target->sources->cell_, key)
            .removed = true;
    });
    Runtime::State().ClearSelection();
  }
  IMComponents::Tooltip(added ? "Takes back what was placed by hand"
                              : "Its generator no longer places it here");

  if (!ImGui::BeginPopup("##overrides"))
    return;
  IMComponents::Label("Review or revert overrides",
                      EditorStyles::GetFonts().p_bold);
  ImGui::Separator();
  if (added) {
    IMComponents::Label(ICON_FA_PLUS "  Placed by hand");
  } else if (edit) {
    if (edit->position)
      IMComponents::Label("Position");
    if (edit->rotation)
      IMComponents::Label("Rotation");
    if (edit->scale)
      IMComponents::Label("Scale");
    if (!edit->replacement.empty())
      IMComponents::Label("Object");
    if (edit->seed)
      IMComponents::Label("Seed");
  }
  ImGui::Separator();
  if (ImGui::Button(added ? "Remove" : "Revert All")) {
    _Change(*target, [&](PCG::EditLayer& layer, PCG::CellEdits&) {
      layer.Revert(target->sources->stage_, target->sources->cell_, key);
    });
    if (added)
      Runtime::State().ClearSelection();
    else
      s_follow = {target->volume, entity, target->sources->stage_,
                  target->sources->cell_, key, 0};
    ImGui::CloseCurrentPopup();
  }
  // TODO: apply overrides to the generator's rules, as Unity applies them to
  // the prefab
  ImGui::EndPopup();
}

void DrawLayer(PCG::GeneratorResource& resource, size_t orphans) {
  if (!IMComponents::Foldout("Edits"))
    return;
  std::string path = resource.edits_;
  if (IMComponents::InputText(
          "File", path, "edits/planet.edits",
          "Hand edits on what it placed (.edits, project-relative), saved "
          "with the scene. An existing file is read")) {
    resource.edits_ = path;
    // An existing file's edits replace these; else these go there
    if (const auto absolute = Runtime::Project().AbsolutePath(path);
        !path.empty() && std::filesystem::exists(absolute)) {
      auto layer = std::make_shared<PCG::EditLayer>();
      PCG::LoadEditLayer(absolute, *layer);
      resource.edit_layer_ = std::move(layer);
    }
    Runtime::Project().MarkSceneDirty();
  }
  const size_t count =
      resource.edit_layer_ ? resource.edit_layer_->Count() : 0;
  IMComponents::KeyValue("Edits", std::to_string(count));
  if (orphans > 0)
    IMComponents::KeyValue("Orphaned", std::to_string(orphans),
                           EditorColor::warning);
  if (orphans > 0)
    IMComponents::Tooltip(
        "Edits in the loaded cells whose instances its rules no longer "
        "place there; they change nothing until the rules place them again");
  if (count > 0 &&
      IMComponents::ButtonBig("Clear Edits", "Back to what it places")) {
    resource.edit_layer_ = std::make_shared<PCG::EditLayer>();
    Runtime::Project().MarkSceneDirty();
  }
}

void Update() {
  if (s_follow.volume == entt::null)
    return;
  auto& ecs = ECS::Main();
  auto& state = Runtime::State();
  // Given up on once something else is selected, or after a while
  if (state.selected_entity != s_follow.from || ++s_follow.frames > 600 ||
      !ecs.Reg().valid(s_follow.volume)) {
    s_follow = {};
    return;
  }
  for (auto [entity, sources] : ecs.View<InstanceSourceComponent>().each()) {
    if (entity == s_follow.from || sources.stage_ != s_follow.stage ||
        sources.cell_ != s_follow.cell)
      continue;
    // A cell fading out is the old one
    if (const auto* fade = ecs.Reg().try_get<FadeComponent>(entity);
        fade && fade->target_ <= 0.0f)
      continue;
    if (InspectableComponents::FindOrigin(entity).volume != s_follow.volume)
      continue;
    for (const InstanceSourceComponent::Source& source : sources.sources_) {
      const PCG::InstanceKey& key = source.key;
      if (key.object != s_follow.key.object ||
          key.index != s_follow.key.index ||
          key.addition != s_follow.key.addition)
        continue;
      const auto* set = ecs.Reg().try_get<InstancedMeshComponent>(entity);
      if (!set) {
        state.SelectEntity(entity);
        s_follow = {};
        return;
      }
      for (size_t i = 0; i < set->anchors_.size(); ++i)
        if (set->anchors_[i] == source.anchor) {
          state.SelectInstance(entity, static_cast<int>(i));
          s_follow = {};
          return;
        }
    }
  }
}

}  // namespace InstanceEdits
