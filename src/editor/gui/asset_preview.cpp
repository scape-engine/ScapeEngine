#include "asset_preview.h"

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <string>

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

#include "editor/gui/components/im_components.h"
#include "editor/gui/inspectables/inspectable_base.h"
#include "editor/gui/styles/editor_styles.h"
#include "editor/runtime/runtime.h"
#include "editor/vendor/IconFontCppHeaders/IconsFontAwesome6.h"
#include "engine/math/transformation.h"
#include "engine/renderer/model/mesh.h"
#include "platform/window_manager.h"

namespace {

// The camera's vertical field of view (PreviewPipeline's)
constexpr float kFov = 45.0f;

}  // namespace

AssetPreview::AssetPreview()
    : output_(Runtime::GetPreviewPipeline().CreateOutput()) {}

void AssetPreview::Reset() {
  yaw_ = 0.6f;
  pitch_ = 0.35f;
  zoom_ = 1.0f;
  lod_ = 0;
}

void AssetPreview::RenderButtons(InspectableBase& item, ImVec2 top_right,
                                 float height) {
  button_at_ = top_right;
  button_size_ = height;

  if (Button(ICON_FA_ROTATE_LEFT, false, "Reset view"))
    Reset();
  if (Button(ICON_FA_CIRCLE_HALF_STROKE, light_background_,
             "Light background"))
    light_background_ = !light_background_;
  if (Button(ICON_FA_CUBE, bounds_, "Show bounds"))
    bounds_ = !bounds_;
  // TODO: Toggling this switches the scene water's lighting instead of the
  // preview's
  if (Button(ICON_FA_SUN, lit_, "Lighting; off: flat colours"))
    lit_ = !lit_;
  // Through its LODs, when its meshes have any
  size_t lods = 0;
  for (const PreviewMesh& preview : item.PreviewMeshes())
    lods = std::max(lods, preview.mesh->Lods().size());
  if (lods > 0) {
    if (Button(ICON_FA_LAYER_GROUP, lod_ > 0, "Show next LOD"))
      lod_ = (lod_ + 1) % static_cast<int>(lods + 1);
  } else {
    lod_ = 0;
  }
  item.RenderPreviewButtons(*this);
}

bool AssetPreview::Button(const char* icon, bool on, const char* tooltip,
                          ImU32 color) {
  button_at_.x -= button_size_;
  ImGui::SetCursorScreenPos(button_at_);
  const bool clicked =
      IMComponents::ToolButton(icon, on, ImVec2(button_size_, button_size_),
                               color);
  if (ImGui::IsItemHovered())
    ImGui::SetTooltip("%s", tooltip);
  button_at_.x -= 2.0f;
  return clicked;
}

void AssetPreview::RenderView(InspectableBase& item, ImVec2 size) {
  std::vector<PreviewMesh> meshes = item.PreviewMeshes();
  if (meshes.empty() || size.x < 1.0f)
    return;
  // At the LOD shown, those that have it
  uint32_t triangles = 0;
  bool has_lods = false;
  for (PreviewMesh& preview : meshes) {
    const auto& lods = preview.mesh->Lods();
    has_lods = has_lods || !lods.empty();
    if (lod_ > 0 && lod_ <= static_cast<int>(lods.size()) &&
        lods[lod_ - 1].mesh)
      preview.mesh = lods[lod_ - 1].mesh;
    triangles += preview.mesh->IndexCount() / 3;
  }

  // The facts and the item's rows below, at last frame's height
  const float facts = ImGui::GetFrameHeight();
  const ImVec2 view_size(size.x,
                         std::max(size.y - facts - rows_height_, 60.0f));
  const ImVec2 at = ImGui::GetCursorScreenPos();
  view_min_ = at;
  view_max_ = at + view_size;

  // A sphere around the meshes, scaled to a unit one: any size frames alike
  glm::vec3 low(FLT_MAX), high(-FLT_MAX);
  for (const PreviewMesh& preview : meshes) {
    const glm::mat3 basis(preview.transform);
    const float scale = std::sqrt(std::max(
        {glm::dot(basis[0], basis[0]), glm::dot(basis[1], basis[1]),
         glm::dot(basis[2], basis[2])}));
    const glm::vec3 centre =
        preview.transform * glm::vec4(preview.mesh->BoundsCenter(), 1.0f);
    const float radius = preview.mesh->BoundsRadius() * scale;
    low = glm::min(low, centre - radius);
    high = glm::max(high, centre + radius);
  }
  const glm::vec3 centre = (low + high) * 0.5f;
  const float radius = std::max(glm::length(high - low) * 0.5f, 1e-4f);
  const glm::mat4 space =
      glm::scale(glm::mat4(1.0f), glm::vec3(1.0f / radius)) *
      glm::translate(glm::mat4(1.0f), -centre);

  // Dragging turns it, the wheel zooms
  ImGui::InvisibleButton("##preview_view", view_size);
  if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
    const ImVec2 delta = ImGui::GetIO().MouseDelta;
    yaw_ += delta.x * 0.01f;
    pitch_ = std::clamp(pitch_ + delta.y * 0.01f, -1.5f, 1.5f);
  }
  if (ImGui::IsItemHovered() && ImGui::GetIO().MouseWheel != 0.0f)
    zoom_ = std::clamp(zoom_ * std::pow(0.9f, ImGui::GetIO().MouseWheel),
                       0.2f, 5.0f);

  PreviewPipeline& pipeline = Runtime::GetPreviewPipeline();
  const float dpi = WindowManager::GetDPIScale();
  pipeline.ResizeOutput(output_, view_size.x * dpi, view_size.y * dpi);

  PreviewRenderInstruction instruction;
  instruction.output_index = output_;
  const ImVec4 background = ImGui::ColorConvertU32ToFloat4(
      light_background_ ? IM_COL32(170, 170, 175, 255) : EditorColor::surface_1);
  instruction.background_color =
      glm::vec4(background.x, background.y, background.z, 1.0f);
  instruction.lit = lit_;
  for (const PreviewMesh& preview : meshes)
    instruction.meshes.push_back(
        {preview.mesh, space * preview.transform, preview.color});
  // The unit sphere fills the view at zoom 1
  const glm::quat turn =
      glm::angleAxis(yaw_, glm::vec3(0.0f, 1.0f, 0.0f)) *
      glm::angleAxis(pitch_, glm::vec3(1.0f, 0.0f, 0.0f));
  const float distance = zoom_ / std::sin(glm::radians(kFov * 0.5f));
  instruction.camera_transform.rotation_ = turn;
  instruction.camera_transform.position_ =
      turn * glm::vec3(0.0f, 0.0f, -distance);
  pipeline.AddRenderInstruction(instruction);
  pipeline.Render();

  view_projection_ =
      Transformation::Projection(kFov, view_size.x / view_size.y, 0.01f,
                                 1000.0f) *
      Transformation::View(instruction.camera_transform.position_, turn) *
      space;

  ImDrawList& draw_list = *ImGui::GetWindowDrawList();
  const bgfx::TextureHandle texture = pipeline.GetOutputTexture(output_);
  if (bgfx::isValid(texture))
    draw_list.AddImage((ImTextureID)(uintptr_t)texture.idx, view_min_,
                       view_max_, ImVec2(0.0f, 1.0f), ImVec2(1.0f, 0.0f));
  draw_list.PushClipRect(view_min_, view_max_, true);
  if (bounds_) {
    // The box around the meshes' spheres
    ImVec2 corners[8];
    for (int i = 0; i < 8; ++i)
      corners[i] = Project(glm::vec3(i & 1 ? high.x : low.x,
                                     i & 2 ? high.y : low.y,
                                     i & 4 ? high.z : low.z));
    for (int i = 0; i < 8; ++i)
      for (int bit = 1; bit < 8; bit <<= 1)
        if (!(i & bit))
          draw_list.AddLine(corners[i], corners[i | bit],
                            IM_COL32(120, 200, 255, 200));
  }
  item.RenderPreviewOverlay(*this);
  draw_list.PopClipRect();

  // What it draws, on the line under it
  ImGui::AlignTextToFramePadding();
  ImGui::TextDisabled("%u meshes   %u triangles",
                      static_cast<uint32_t>(meshes.size()), triangles);
  if (has_lods) {
    ImGui::SameLine(0.0f, 12.0f);
    ImGui::TextDisabled("LOD %d", lod_);
  }

  const float rows_top = ImGui::GetCursorScreenPos().y;
  item.RenderPreviewRows(*this);
  rows_height_ = ImGui::GetCursorScreenPos().y - rows_top;
}

ImVec2 AssetPreview::Project(const glm::vec3& point) const {
  const glm::vec4 clip = view_projection_ * glm::vec4(point, 1.0f);
  if (clip.w <= 0.0f)
    return view_min_;
  const glm::vec2 ndc = glm::vec2(clip) / clip.w;
  // The image shows the view's texture upside down
  return ImVec2(view_min_.x + (ndc.x + 1.0f) * 0.5f * (view_max_.x - view_min_.x),
                view_min_.y + (ndc.y + 1.0f) * 0.5f * (view_max_.y - view_min_.y));
}
