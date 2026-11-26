#include "entity_inspectable.h"

#include <imgui.h>

#include "editor/editor_ui.h"
#include "editor/gui/components/im_components.h"
#include "editor/gui/components/inspectable_components.h"
#include "editor/gui/hierarchy_panel.h"
#include "editor/gui/search/search_popup.h"
#include "editor/gui/styles/editor_styles.h"
#include "editor/registry/component_registry.h"
#include "editor/runtime/runtime.h"
#include "engine/ecs/components/instance_source_component.h"
#include "engine/ecs/components/instanced_mesh_component.h"
#include "engine/pcg/stage.h"

#include <filesystem>

#include <glm/gtc/matrix_transform.hpp>

//---------------------------------------------------------------------------
// Dynamic inspector for entities
//---------------------------------------------------------------------------

EntityInspectable::EntityInspectable(HierarchyItem& item) : item(item) {}

// Generator > stage, the generator selectable
static void _DrawOrigin(const InspectableComponents::GeneratedOrigin& origin) {
  ImGui::PushFont(EditorStyles::GetFonts().s);
  const std::string& name =
      ECS::Main().Get<TransformComponent>(origin.volume).name_;
  const ImVec2 position = ImGui::GetCursorScreenPos();
  ImGui::InvisibleButton("##generator", ImGui::CalcTextSize(name.c_str()));
  const bool hovered = ImGui::IsItemHovered();
  if (hovered)
    ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
  if (ImGui::IsItemClicked())
    Runtime::State().SelectEntity(origin.volume);
  ImGui::GetWindowDrawList()->AddText(
      position, hovered ? EditorColor::accent_hover : EditorColor::text,
      name.c_str());

  if (const PCG::Stage* stage = InspectableComponents::FindStage(origin)) {
    ImGui::SameLine();
    ImGui::PushStyleColor(ImGuiCol_Text, EditorColor::text_dim);
    ImGui::TextUnformatted(ICON_FA_CHEVRON_RIGHT);
    ImGui::SameLine();
    ImGui::TextUnformatted(
        (stage->name.empty() ? origin.stage : stage->name).c_str());
    ImGui::PopStyleColor();
  }
  ImGui::PopFont();
}

int EntityInspectable::SelectedInstance() const {
  const auto& state = Runtime::State();
  const auto* set =
      ECS::Main().Reg().try_get<InstancedMeshComponent>(item.entity.Handle());
  return set && state.selected_entity == item.entity.Handle() &&
                 state.selected_instance >= 0 &&
                 state.selected_instance <
                     static_cast<int>(set->locals_.size())
             ? state.selected_instance
             : -1;
}

void EntityInspectable::RenderStaticContent(ImDrawList& draw_list) {

  // Entity name header; an instance's, its object's
  const int instance = SelectedInstance();
  IMComponents::Label(
      instance >= 0
          ? InspectableComponents::InstanceName(item.entity.Handle(), instance)
          : item.entity.Name(),
      EditorStyles::GetFonts().h3_bold);
  // Generated: where it comes from instead, as only its generator adds to it
  const auto origin = InspectableComponents::FindOrigin(item.entity.Handle());
  if (origin.volume != entt::null) {
    _DrawOrigin(origin);
    return;
  }
  ImGui::Dummy(ImVec2(0.0f, 3.0f));

  IMComponents::BeginCentered("AddComponent");
  if (IMComponents::ButtonBig(ICON_FA_PLUS "  Add Component")) {
    const ImVec2 search_position =
        ImVec2(ImGui::GetItemRectMin().x, ImGui::GetItemRectMax().y + 4.0f);
    SearchPopup::SearchComponents(search_position, item.entity.Handle());
  }
  IMComponents::EndCentered();
}

void EntityInspectable::RenderDynamicContent(ImDrawList& draw_list) {
  // An instance: its own details and its stage's
  if (const int instance = SelectedInstance(); instance >= 0) {
    const Entity entity = item.entity.Handle();
    InspectableComponents::DrawInstance(entity, instance);
    if (auto* generated =
            ECS::Main().Reg().try_get<ProceduralInstanceComponent>(entity))
      InspectableComponents::DrawProceduralInstanceComponent(entity,
                                                             *generated);
    return;
  }

  // Iterate registered components and draw their inspectors
  const auto& registry = ComponentRegistry::Get();
  const auto& keys_ordered = ComponentRegistry::OrderedKeys();

  InspectableComponents::AllowRemoving(
      InspectableComponents::FindOrigin(item.entity.Handle()).volume ==
      entt::null);
  for (const auto& key : keys_ordered) {
    auto it = registry.find(key);
    if (it != registry.end()) {
      it->second.try_draw_inspectable(item.entity.Handle());
    }
  }
  InspectableComponents::AllowRemoving(true);
}
// Its meshes and its children's, in `space` (none past `room`); generators'
// volumes not: what they make isn't a model
static void _CollectMeshes(ECS& ecs, Entity entity, const glm::mat4& space,
                           size_t room, std::vector<PreviewMesh>& meshes) {
  if (ecs.Has<VolumeComponent>(entity) || ecs.Has<PCGWorldComponent>(entity))
    return;
  const auto& transform = ecs.Get<TransformComponent>(entity);
  if (const auto* renderer = ecs.Reg().try_get<MeshRendererComponent>(entity);
      renderer && renderer->mesh_ && meshes.size() < room)
    meshes.push_back({renderer->mesh_, space * transform.model_});
  for (const Entity child : transform.children_)
    _CollectMeshes(ecs, child, space, room, meshes);
}

std::vector<PreviewMesh> EntityInspectable::PreviewMeshes() {
  auto& ecs = ECS::Main();
  const Entity entity = item.entity.Handle();
  if (!ecs.Reg().valid(entity))
    return {};
  if (ProcModelPreview* procmodel = ProcModel();
      procmodel && !procmodel->Meshes().empty())
    return procmodel->Meshes();
  std::vector<PreviewMesh> meshes;
  if (const int instance = SelectedInstance(); instance >= 0) {
    // Its object, about its anchor
    const auto& set = ecs.Get<InstancedMeshComponent>(entity);
    const glm::vec3 anchor = set.anchors_.size() == set.locals_.size()
                                 ? set.anchors_[instance]
                                 : glm::vec3(set.locals_[instance][3]);
    const glm::mat4 space = glm::translate(glm::mat4(1.0f), -anchor);
    for (const auto& draw : set.ObjectOf(instance))
      meshes.push_back({draw.part->mesh, space * set.locals_[draw.instance]});
    return meshes;
  }
  // A preview for a model, not a whole scene
  constexpr size_t room = 256;
  _CollectMeshes(ecs, entity,
                 glm::inverse(ecs.Get<TransformComponent>(entity).model_),
                 room, meshes);
  return meshes;
}

ProcModelPreview* EntityInspectable::ProcModel() {
  const int instance = SelectedInstance();
  if (instance == procmodel_instance_)
    return procmodel_.get();
  procmodel_instance_ = instance;
  procmodel_.reset();
  if (instance < 0)
    return nullptr;
  // What made its object
  auto& ecs = ECS::Main();
  const Entity entity = item.entity.Handle();
  const auto& set = ecs.Get<InstancedMeshComponent>(entity);
  const auto* sources = ecs.Reg().try_get<InstanceSourceComponent>(entity);
  if (!sources || set.anchors_.size() != set.locals_.size())
    return nullptr;
  static constexpr std::string_view prefix = "procmodel:";
  const auto* source = sources->Find(set.anchors_[instance]);
  if (source && source->id.rfind(prefix, 0) == 0)
    procmodel_ = std::make_unique<ProcModelPreview>(
        source->id.substr(prefix.size()), source->seed);
  return procmodel_.get();
}

void EntityInspectable::RenderPreviewButtons(AssetPreview& preview) {
  if (ProcModelPreview* procmodel = ProcModel())
    procmodel->RenderButtons(preview);
}

void EntityInspectable::RenderPreviewOverlay(AssetPreview& preview) {
  if (ProcModelPreview* procmodel = ProcModel())
    procmodel->RenderOverlay(preview);
}

void EntityInspectable::RenderPreviewRows(AssetPreview& preview) {
  if (ProcModelPreview* procmodel = ProcModel())
    procmodel->RenderRows();
}
