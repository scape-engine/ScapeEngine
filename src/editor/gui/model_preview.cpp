#include "model_preview.h"
#include "editor/gui/components/im_components.h"
#include "imgui.h"

#define GLM_ENABLE_EXPERIMENTAL
#include <algorithm>
#include <glm/gtx/matrix_decompose.hpp>
#include <string>

#include <ImGuiFileDialog/ImGuiFileDialog.h>

#include "editor/gui/styles/editor_styles.h"

#include "engine/context/engine_context.h"
#include "engine/ecs/components/transform_component.h"
#include "engine/memory/resource_manager.h"
#include "engine/renderer/model/model.h"

#include "engine/pcg/procmodel/procmodel_resource.h"

ModelPreview::ModelPreview(PreviewData* data) : data_(data) {}

ModelPreview::~ModelPreview() {
  if (initialized_) {
    preview_pipeline_.Destroy();
  }
}

void ModelPreview::Init() {
  if (initialized_)
    return;

  preview_pipeline_.Create();
  // Create outputs for all instances
  for (int i = 0; i < total_instances_; ++i) {
    instance_outputs_.push_back(preview_pipeline_.CreateOutput());
  }

  initialized_ = true;
}

void ModelPreview::LoadDescriptor(const std::string& path) {
  descriptor_path_ = path;

  data_->instances.clear();
  data_->instance_model_data.clear();
  images_generated_ = 0;
  images_submitted_ = 0;

  auto& pcg = EngineContext::PCG().GetProcModel();
  data_->archetype_id = pcg.LoadArchetype(descriptor_path_);

  if (data_->archetype_id == 0) {
    Logger::getInstance().Log(
        LogLevel::Error, "[ModelPreview] Failed to load archetype: " + path);
    data_->model = nullptr;
    return;
  }

  auto& resource_mgr = EngineContext::resourceManager();
  auto resource = resource_mgr.GetResourceAs<ProcModel::ProcModelResource>(
      data_->archetype_id);
  if (resource) {
    data_->model = Model::FromMeshData(resource->GetGraph().mesh_data,
                                       resource->GetGraph().materials);
  }
}

// TODO: Move per-instance Model construction out of editor
// - Implement a 'BuildRenderModel' for it (instantiator?)
// - do not implement the TODO into the procmodel subsystem
void ModelPreview::TickGenerate() {
  if (images_generated_ >= total_instances_)
    return;

  if (data_->archetype_id == 0)
    return;

  auto& procmodel = EngineContext::PCG().GetProcModel();

  int budget = images_per_frame_;
  while (budget-- > 0 && images_generated_ < total_instances_) {
    auto output = procmodel.GenerateInstance(descriptor_path_,
                                             current_seed_ + images_generated_);

    if (output) {
      // Only build a per-instance Model if pipeline produces deformed
      // geometry; else reuse source model (no buffer allocation)
      // TODO: allocate buffers only for deformed mesh slots, not all 87
      // This TODO is tied with the implementation of the pcg pipelie
      if (!output->instance_geometry.empty()) {
        auto& resource_mgr = EngineContext::resourceManager();
        auto resource =
            resource_mgr.GetResourceAs<ProcModel::ProcModelResource>(
                data_->archetype_id);

        const auto& graph = resource->GetGraph();
        std::vector<Geometry::MeshData> per_instance_meshes = graph.mesh_data;

        for (const auto& ig : output->instance_geometry) {
          const auto& descs = output->model.descriptors;
          auto desc_it =
              std::find_if(descs.begin(), descs.end(), [&](const auto& d) {
                return d.descriptor_id == ig.descriptor_id;
              });
          if (desc_it == descs.end())
            continue;

          for (size_t slot = 0; slot < ig.mesh_data.size() &&
                                slot < desc_it->mesh_indices.size();
               ++slot) {
            int mesh_idx = desc_it->mesh_indices[slot];
            if (mesh_idx >= 0 &&
                mesh_idx < static_cast<int>(per_instance_meshes.size())) {
              per_instance_meshes[mesh_idx] = ig.mesh_data[slot];
            }
          }
        }

        auto instance_model =
            Model::FromMeshData(per_instance_meshes, graph.materials);
        data_->instance_model_data.push_back(std::move(instance_model));
      } else {
        data_->instance_model_data.push_back(nullptr);
      }
      data_->instances.push_back(std::move(output->model));
    }
    ++images_generated_;
  }
}

void ModelPreview::Render() {
  if (!initialized_)
    Init();

  TickGenerate();

  // ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(20.0f, 20.0f));
  ImGui::Begin("Procedural Preview", nullptr, EditorFlag::standard);

  RenderToolbar(ImGui::GetWindowDrawList());

  ImGui::BeginChild("GridArea", ImVec2(0, 0), false);
  // Fetch the draw list INSIDE the child window so scrolling
  // automatically clips the images
  ImDrawList* child_draw_list = ImGui::GetWindowDrawList();
  RenderGrid(child_draw_list);
  ImGui::EndChild();

  ImGui::End();

  SubmitViews();
  preview_pipeline_.Render();
}

void ModelPreview::RenderToolbar(ImDrawList* draw_list) {
  ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(12.0f, 8.0f));

  IMComponents::Headline("Procedural Viewer", ICON_FA_CUBES);

  if (ImGui::Button(ICON_FA_FOLDER_OPEN " Load")) {
    IGFD::FileDialogConfig cfg{};
    cfg.path = std::filesystem::current_path().string();
    IGFD::FileDialog::Instance()->OpenDialog("LoadDescriptorDlg",
                                             "Select Descriptor", ".json", cfg);
  }

  if (IGFD::FileDialog::Instance()->Display("LoadDescriptorDlg",
                                            ImGuiWindowFlags_NoCollapse,
                                            ImVec2(700.0f, 500.0f))) {
    if (IGFD::FileDialog::Instance()->IsOk()) {
      LoadDescriptor(IGFD::FileDialog::Instance()->GetFilePathName());
    }
    IGFD::FileDialog::Instance()->Close();
  }

  ImGui::SameLine();
  if (ImGui::Button(ICON_FA_ROTATE " Generate")) {
    current_seed_ = static_cast<uint32_t>(Time::Now() * 1000.0);
    data_->instances.clear();
    data_->instance_model_data.clear();
    images_generated_ = 0;
    images_submitted_ = 0;
  }

  ImGui::Text("Base seed: %u", current_seed_);

  ImGui::SameLine();
  float zoom_width = 100.0f;
  float current_x = ImGui::GetCursorPosX();
  float right_edge =
      ImGui::GetWindowWidth() - zoom_width - ImGui::GetStyle().WindowPadding.x;
  // Safely push to the right only if we have space, otherwise stay next in line
  if (right_edge > current_x) {
    ImGui::SetCursorPosX(right_edge);
  }

  ImGui::SetNextItemWidth(zoom_width);
  ImGui::SliderFloat("##Zoom", &thumbnail_size_, 64.0f, 120.0f, "Zoom: %.0f");

  ImGui::PopStyleVar();
  ImGui::Dummy(ImVec2(0, 5.0f));
  ImGui::Separator();
  ImGui::Dummy(ImVec2(0, 5.0f));
}

void ModelPreview::RenderGrid(ImDrawList* draw_list) {
  ImVec2 content_avail = ImGui::GetContentRegionAvail();
  float padding = 8.0f;
  int columns = std::max(
      1, static_cast<int>(content_avail.x / (thumbnail_size_ + padding)));
  float dpi = WindowManager::GetDPIScale();
  int render_count =
      std::min(total_instances_, static_cast<int>(data_->instances.size()));

  if (ImGui::BeginTable("InstanceGrid", columns)) {
    for (int i = 0; i < total_instances_; i++) {
      ImGui::TableNextColumn();

      ImVec2 cursor_pos = ImGui::GetCursorScreenPos();
      ImVec2 rect_min = cursor_pos;
      ImVec2 rect_max =
          ImVec2(rect_min.x + thumbnail_size_, rect_min.y + thumbnail_size_);

      ImGui::InvisibleButton(("##inst_" + std::to_string(i)).c_str(),
                             ImVec2(thumbnail_size_, thumbnail_size_));
      bool hovered = ImGui::IsItemHovered();
      bool selected = (data_->selected_instance == i);

      if (ImGui::IsItemClicked()) {
        data_->selected_instance = i;
      }

      ImU32 bg_color = EditorColor::element;
      if (hovered)
        bg_color = EditorColor::element_hovered;
      if (selected)
        bg_color = EditorColor::element_active;

      draw_list->AddRectFilled(rect_min, rect_max, bg_color, 4.0f);

      bool has_instance = (i < render_count) && data_->model;

      if (has_instance) {
        size_t out_idx = instance_outputs_[i];
        preview_pipeline_.ResizeOutput(out_idx, thumbnail_size_ * dpi,
                                       thumbnail_size_ * dpi);

        bgfx::TextureHandle tex = preview_pipeline_.GetOutputTexture(out_idx);
        if (bgfx::isValid(tex)) {
          float pad = 0.5f;
          ImVec2 img_min = ImVec2(rect_min.x + pad, rect_min.y + pad);
          ImVec2 img_max = ImVec2(rect_max.x - pad, rect_max.y - pad);
          draw_list->AddImage((ImTextureID)(uintptr_t)tex.idx, img_min, img_max,
                              ImVec2(0, 1), ImVec2(1, 0));
        }
      } else {
        ImGui::PushFont(EditorStyles::GetFonts().h2);
        std::string icon = ICON_FA_CUBE;
        ImVec2 text_size = ImGui::CalcTextSize(icon.c_str());
        draw_list->AddText(
            ImVec2(rect_min.x + (thumbnail_size_ - text_size.x) * 0.5f,
                   rect_min.y + (thumbnail_size_ - text_size.y) * 0.5f),
            EditorColor::text, icon.c_str());
        ImGui::PopFont();
      }

      draw_list->AddRect(rect_min, rect_max, EditorColor::border_color, 4.0f, 0,
                         1.0f);
    }
    ImGui::EndTable();
  }
}

// Procedural Color Generator based on Group ID (Replaces Hardcoded Maps)
static glm::vec3 GenerateGroupColor(const std::string& group_id) {
  if (group_id == "_BASE_")
    return glm::vec3(0.92f, 0.92f, 0.90f);

  // Hash the string to generate a deterministic RGB value
  size_t hash = std::hash<std::string>{}(group_id);
  float r = ((hash & 0xFF0000) >> 16) / 255.0f;
  float g = ((hash & 0x00FF00) >> 8) / 255.0f;
  float b = (hash & 0x0000FF) / 255.0f;

  // Mix with white to guarantee pastel colors that look good in the editor
  return glm::mix(glm::vec3(r, g, b), glm::vec3(1.0f), 0.4f);
}

void ModelPreview::SubmitViews() {
  if (!data_->model)
    return;

  float dpi = WindowManager::GetDPIScale();
  int render_count =
      std::min(total_instances_, static_cast<int>(data_->instances.size()));
  int budget = images_per_frame_;

  while (images_submitted_ < render_count && budget-- > 0) {
    int i = images_submitted_;
    size_t out_idx = instance_outputs_[i];
    preview_pipeline_.ResizeOutput(out_idx, thumbnail_size_ * dpi,
                                   thumbnail_size_ * dpi);

    bool on_first_draw = true;

    for (const auto& desc : data_->instances[i].descriptors) {

      // Generate procedural colors dynamically
      glm::vec3 color = GenerateGroupColor(desc.group_id);

      for (int idx : desc.mesh_indices) {
        uint32_t mesh_idx = static_cast<uint32_t>(idx);

        PreviewRenderInstruction inst;
        inst.output_index = out_idx;
        inst.background_color = glm::vec4(0.35f, 0.35f, 0.35f, 1.0f);

        inst.model = (i < static_cast<int>(data_->instance_model_data.size()) &&
                      data_->instance_model_data[i])
                         ? data_->instance_model_data[i].get()
                         : data_->model.get();

        inst.clear_output = on_first_draw;
        on_first_draw = false;

        inst.mesh_filter = {mesh_idx};
        inst.mesh_colors[mesh_idx] = color;

        glm::vec3 pos, scale, skew;
        glm::quat rot;
        glm::vec4 persp;
        glm::decompose(desc.local_transform, scale, rot, pos, skew, persp);

        TransformComponent transform_component;
        transform_component.position_ = pos;
        transform_component.rotation_ = rot;
        transform_component.scale_ = scale;

        // Apply pipeline deformation from resolved descriptor
        const glm::quat jitter_rot = glm::quat(desc.applied_rotation);
        transform_component.rotation_ = glm::normalize(rot * jitter_rot);
        transform_component.scale_ = scale * desc.applied_scale;

        inst.mesh_transforms[mesh_idx] = transform_component;

        inst.camera_transform.position_ = glm::vec3(0.0f, 15.0f, -60.0f);

        preview_pipeline_.AddRenderInstruction(inst);
      }
    }
    ++images_submitted_;
  }
}

/* void ModelPreview::GenerateInstances() {
  data_->instances.clear();

  if (data_->archetype_id == 0)
    return;

  auto& resource_mgr = EngineContext::resourceManager();
  auto resource = resource_mgr.GetResourceAs<ProcModel::ProcModelResource>(
      data_->archetype_id);
  if (!resource || !resource->IsResolved())
    return;

  for (int i = 0; i < total_instances_; ++i) {
    auto resolved = ProcModel::ModelGenerator::Generate(
        resource->GetGraph(), resource->GetDescriptor(), current_seed_ + i);
    if (resolved) {
      data_->instances.push_back(std::move(*resolved));
    }
  }

  regenerate_ = false;
}*/