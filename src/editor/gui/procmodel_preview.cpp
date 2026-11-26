#include "procmodel_preview.h"

#define IMGUI_DEFINE_MATH_OPERATORS
#include <imgui.h>

#include <algorithm>
#include <functional>
#include <random>

#include "editor/gui/asset_preview.h"
#include "editor/gui/components/im_components.h"
#include "editor/gui/styles/editor_styles.h"
#include "editor/runtime/runtime.h"
#include "editor/vendor/IconFontCppHeaders/IconsFontAwesome6.h"
#include "engine/context/engine_context.h"
#include "engine/pcg/pcg_engine.h"
#include "engine/pcg/procmodel/instantiator/model_instantiator.h"
#include "engine/renderer/model/mesh.h"

namespace {

// A part's colour, the same for its id every time
glm::vec4 _PartColour(const std::string& id) {
  const float hue =
      static_cast<float>(std::hash<std::string>{}(id) % 1000) / 1000.0f;
  glm::vec4 colour(1.0f);
  ImGui::ColorConvertHSVtoRGB(hue, 0.6f, 1.0f, colour.r, colour.g, colour.b);
  return colour;
}

// The buttons' glyphs while off
constexpr ImU32 kPartsColour = IM_COL32(240, 170, 80, 255);
constexpr ImU32 kOriginColour = IM_COL32(240, 90, 90, 255);

}  // namespace

ProcModelPreview::ProcModelPreview(std::string descriptor, uint64_t seed)
    : descriptor_(std::move(descriptor)), start_seed_(seed), seed_(seed) {
  Generate();
}

void ProcModelPreview::Generate() {
  parts_.clear();
  deformed_.reset();
  auto& procmodel = EngineContext::PCG().GetProcModel();
  archetype_ =
      procmodel.Archetype(Runtime::Project().AbsolutePath(descriptor_).string());
  const auto instance =
      archetype_ ? procmodel.Generate(*archetype_, seed_) : std::nullopt;
  if (instance) {
    // Deformed as placed, but not merged: each part keeps its meshes
    deformed_ = procmodel.Deformed(*archetype_, *instance);
    for (const ProcModel::ResolvedDescriptor& descriptor :
         instance->model.descriptors) {
      const ProcModel::InstanceModel one{instance->model.model_id,
                                         instance->model.seed,
                                         {descriptor},
                                         instance->model.model_scale};
      const auto meshes = ProcModel::ModelInstantiator::Parts(
          one, archetype_->GetGraph(), nullptr,
          deformed_ ? &deformed_->meshes : nullptr);
      if (!meshes) {  // skinned: it animates, which this doesn't show
        parts_.clear();
        break;
      }
      auto part = std::find_if(parts_.begin(), parts_.end(), [&](const Part& p) {
        return p.id == descriptor.descriptor_id;
      });
      if (part == parts_.end())
        part = parts_.insert(parts_.end(), Part{descriptor.descriptor_id, {}});
      for (const auto& mesh : *meshes)
        part->meshes.push_back({mesh.mesh, mesh.matrix});
    }
  }
  extent_ = 0.0f;
  for (const Part& part : parts_)
    for (const PreviewMesh& mesh : part.meshes)
      extent_ = std::max(
          extent_, glm::length(glm::vec3(mesh.transform *
                                         glm::vec4(mesh.mesh->BoundsCenter(),
                                                   1.0f))) +
                       mesh.mesh->BoundsRadius() *
                           glm::length(glm::vec3(mesh.transform[0])));
  Show();
}

void ProcModelPreview::Show() {
  meshes_.clear();
  for (const Part& part : parts_) {
    if ((!solo_.empty() && part.id != solo_) ||
        std::find(hidden_.begin(), hidden_.end(), part.id) != hidden_.end())
      continue;
    const glm::vec4 colour = colours_ ? _PartColour(part.id) : glm::vec4(1.0f);
    for (const PreviewMesh& mesh : part.meshes)
      meshes_.push_back({mesh.mesh, mesh.transform, colour});
  }
}

void ProcModelPreview::RenderButtons(AssetPreview& preview) {
  if (preview.Button(ICON_FA_LOCATION_CROSSHAIRS, origin_, "Origin",
                     kOriginColour))
    origin_ = !origin_;
  if (preview.Button(ICON_FA_PALETTE, colours_, "Colour individual parts",
                     kPartsColour)) {
    colours_ = !colours_;
    Show();
  }
}

void ProcModelPreview::RenderOverlay(AssetPreview& preview) {
  if (!origin_)
    return;
  // Its axes from the origin, a quarter of its reach long
  ImDrawList& draw_list = *ImGui::GetWindowDrawList();
  const ImVec2 origin = preview.Project(glm::vec3(0.0f));
  const float length = std::max(extent_, 1e-3f) * 0.25f;
  const ImU32 axes[3] = {IM_COL32(230, 70, 70, 255), IM_COL32(110, 200, 60, 255),
                         IM_COL32(80, 130, 240, 255)};
  for (int axis = 0; axis < 3; ++axis) {
    glm::vec3 end(0.0f);
    end[axis] = length;
    draw_list.AddLine(origin, preview.Project(end), axes[axis], 2.0f);
  }
  draw_list.AddCircleFilled(origin, 3.5f, IM_COL32(255, 255, 255, 255));
}

void ProcModelPreview::RenderRows(const std::function<void(uint64_t)>& use) {
  // Any seed of the model; back to its own, or made its own
  IMComponents::Row(
      "Seed",
      [&] {
        // The value, then the buttons at the field's right end, room kept for
        // the reset and use so they stay put; a value too long is clipped
        const float button = ImGui::GetFrameHeight();
        const float buttons = use ? 5.0f : 4.0f;
        const ImVec2 at = ImGui::GetCursorScreenPos();
        const float buttons_x =
            at.x + std::max(ImGui::GetContentRegionAvail().x -
                                button * buttons - 2.0f * (buttons - 1.0f),
                            0.0f);
        const std::string value =
            std::to_string(static_cast<unsigned long long>(seed_));
        ImGui::AlignTextToFramePadding();
        ImGui::PushClipRect(at, ImVec2(buttons_x - 4.0f, at.y + button), true);
        ImGui::TextUnformatted(value.c_str());
        ImGui::PopClipRect();
        ImGui::SameLine();
        ImGui::SetCursorScreenPos(ImVec2(buttons_x, at.y));
        bool changed = false;
        if (IMComponents::ToolButton(ICON_FA_CHEVRON_LEFT, false,
                                     ImVec2(button, button))) {
          --seed_;
          changed = true;
        }
        ImGui::SameLine(0.0f, 2.0f);
        if (IMComponents::ToolButton(ICON_FA_CHEVRON_RIGHT, false,
                                     ImVec2(button, button))) {
          ++seed_;
          changed = true;
        }
        ImGui::SameLine(0.0f, 2.0f);
        if (IMComponents::ToolButton(ICON_FA_DICE, false,
                                     ImVec2(button, button))) {
          static std::mt19937_64 random{std::random_device{}()};
          seed_ = random();
          changed = true;
        }
        if (ImGui::IsItemHovered())
          ImGui::SetTooltip("A random seed");
        if (seed_ != start_seed_) {
          ImGui::SameLine(0.0f, 2.0f);
          if (IMComponents::ToolButton(ICON_FA_ROTATE_LEFT, false,
                                       ImVec2(button, button))) {
            seed_ = start_seed_;
            changed = true;
          }
          if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Back to %llu",
                              static_cast<unsigned long long>(start_seed_));
          if (use) {
            ImGui::SameLine(0.0f, 2.0f);
            if (IMComponents::ToolButton(ICON_FA_CHECK, false,
                                         ImVec2(button, button))) {
              start_seed_ = seed_;
              use(seed_);
            }
            if (ImGui::IsItemHovered())
              ImGui::SetTooltip("Use this seed");
          }
        }
        if (changed)
          Generate();
        return changed;
      });

  if (parts_.empty() || !IMComponents::Foldout("Parts##preview_parts"))
    return;
  // Each part: shown or hidden, alone on a click
  ImDrawList& draw_list = *ImGui::GetWindowDrawList();
  const float height = ImGui::GetFrameHeight();
  bool changed = false;
  for (const Part& part : parts_) {
    ImGui::PushID(part.id.c_str());
    const auto hidden = std::find(hidden_.begin(), hidden_.end(), part.id);
    const bool shown = hidden == hidden_.end();
    if (IMComponents::GlyphButton(
            draw_list, "##shown", shown ? ICON_FA_EYE : ICON_FA_EYE_SLASH,
            ImGui::GetCursorScreenPos(), ImVec2(height, height),
            shown ? EditorColor::text : EditorColor::text_dim,
            shown ? "Hide" : "Show")) {
      if (shown)
        hidden_.push_back(part.id);
      else
        hidden_.erase(hidden);
      changed = true;
    }
    ImGui::SameLine(0.0f, 4.0f);
    if (colours_) {
      const ImVec2 at = ImGui::GetCursorScreenPos() + ImVec2(0.0f, height * 0.3f);
      const glm::vec4 c = _PartColour(part.id);
      draw_list.AddRectFilled(at, at + ImVec2(height * 0.4f, height * 0.4f),
                              ImGui::ColorConvertFloat4ToU32(
                                  ImVec4(c.r, c.g, c.b, 1.0f)),
                              2.0f);
      ImGui::Dummy(ImVec2(height * 0.4f, height));
      ImGui::SameLine(0.0f, 6.0f);
    }
    const std::string label =
        part.meshes.size() > 1
            ? part.id + "  x" + std::to_string(part.meshes.size())
            : part.id;
    if (ImGui::Selectable(label.c_str(), solo_ == part.id, 0,
                          ImVec2(0.0f, height))) {
      solo_ = solo_ == part.id ? std::string() : part.id;
      changed = true;
    }
    if (ImGui::IsItemHovered())
      ImGui::SetTooltip(solo_ == part.id ? "Show every part"
                                         : "Show this part");
    ImGui::PopID();
  }
  if (changed)
    Show();
}
