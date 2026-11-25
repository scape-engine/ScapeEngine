#ifndef MODEL_PREVIEW_H
#define MODEL_PREVIEW_H

#include <imgui.h>
#include <string>
#include <vector>

#include "editor/gui/window_base.h"
#include "editor/pipelines/preview_pipeline.h"
#include "engine/memory/resource.h"
#include "engine/pcg/procmodel/generator/resolved_model.h"
#include "platform/window_manager.h"

class Model;  // Forward declare model

class ModelPreview : public WindowBase {
public:
  explicit ModelPreview(PreviewData* data);
  ~ModelPreview() override;

  void Render() override;

private:
  void Init();

  void LoadDescriptor(const std::string& path);
  void TickGenerate();
  void GenerateInstances();

  void RenderToolbar(ImDrawList* draw_list);
  void RenderGrid(ImDrawList* draw_list);

  void SubmitViews();

  PreviewPipeline preview_pipeline_;
  std::vector<size_t> instance_outputs_;
  PreviewData* data_;

  // Procmodel state
  std::string descriptor_path_;
  bool clear_output = false;

  // UI States
  uint32_t current_seed_;  // seed is only defined in procmodel, remove this
  float thumbnail_size_ = 120.0f;
  int total_instances_ = 50;
  bool initialized_ = false;
  // bool regenerate_ = false;

  // Render state
  int images_per_frame_ = 1;
  int images_generated_ = 0;
  int images_submitted_ = 0;

  // Archetype the previews were generated from
  ResourceID shown_archetype_ = 0;

  // Status bar task while previews generate (0 when idle)
  uint64_t generate_task_ = 0;
};

#endif  // MODEL_PREVIEW_H
