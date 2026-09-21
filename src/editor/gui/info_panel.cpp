#include "info_panel.h"

#include <bgfx/bgfx.h>

#include "editor/gui/components/im_components.h"
#include "editor/gui/styles/editor_styles.h"
#include "editor/gui/utils/gui_utils.h"
#include "editor/runtime/runtime.h"
#include "engine/time/time.h"

void InfoPanel::Render() {
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(pad_x_, pad_y_));
  ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.0f, line_gap_));
  ImGui::Begin("Project Info", nullptr, EditorFlag::standard);
  {
    RenderMetadata();
    RenderAssets();
    RenderConfiguration();
    RenderLog();
  }
  ImGui::End();
  ImGui::PopStyleVar(2);
}

void InfoPanel::RenderMetadata() {
  auto& project = Runtime::Project();
  const auto& cfg = project.Config();

  IMComponents::SectionTitle("Metadata");
  IMComponents::KeyValue("Project Name", cfg.name);
  IMComponents::KeyValue("Author", cfg.author.empty() ? "Unknown" : cfg.author);
  IMComponents::KeyValue("Tags", cfg.tags.empty() ? "None" : cfg.tags);
  IMComponents::KeyValue("Size", GUIUtils::FormatBytes(project.SizeOnDisk()));
}

void InfoPanel::RenderAssets() {
  // TODO: source from ProjectAssets once it exposes missing/packed counts
  IMComponents::SectionTitle("Asset");
  IMComponents::KeyValue("Textures", "3 Missing", EditorColor::error);
  IMComponents::KeyValue("HDRI", "Not Packed");
  IMComponents::KeyValue("Simulation Caches", "Empty");
}

void InfoPanel::RenderConfiguration() {
  auto& pipeline = Runtime::GetSceneViewPipeline();

  IMComponents::SectionTitle("Configuration");
  IMComponents::KeyValue("Frame Rate",
      std::to_string(static_cast<int>(ImGui::GetIO().Framerate + 0.5f)) + " FPS");
  IMComponents::KeyValue("Renderer", bgfx::getRendererName(bgfx::getRendererType()));
  IMComponents::KeyValue("MSAA", std::to_string(pipeline.msaa_samples_) + "x");
  IMComponents::KeyValue("Shadows", pipeline.render_shadows_ ? "On" : "Off");
}

void InfoPanel::RenderLog() {
  IMComponents::SectionTitle("Log");
  IMComponents::KeyValue("Session Time",
      std::to_string(static_cast<int>(Time::Nowf() / 60.0f)) + " mins");
}