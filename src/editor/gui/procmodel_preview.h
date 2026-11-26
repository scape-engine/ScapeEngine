#ifndef PROCMODEL_PREVIEW_H
#define PROCMODEL_PREVIEW_H

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "editor/pipelines/preview_pipeline.h"
#include "engine/pcg/procmodel/procmodel.h"

class AssetPreview;

// What a procedural model adds to its AssetPreview: any seed of its
// descriptor, its parts (each alone, hidden or in its own colour) and its
// origin. Placed instances and .object assets preview through it
class ProcModelPreview {
public:
  // `descriptor`: project relative; `seed` the one it starts and resets to
  ProcModelPreview(std::string descriptor, uint64_t seed);

  // The parts shown; none when the model didn't generate (or animates)
  const std::vector<PreviewMesh>& Meshes() const { return meshes_; }

  void RenderButtons(AssetPreview& preview);
  void RenderOverlay(AssetPreview& preview);
  void RenderRows();

private:
  // A part of the descriptor: its meshes in the model's space
  struct Part {
    std::string id;
    std::vector<PreviewMesh> meshes;
  };

  // The parts of `seed_`
  void Generate();
  // `meshes_` from the parts and what is shown
  void Show();

  std::string descriptor_;
  uint64_t start_seed_;
  uint64_t seed_;

  // What keeps the meshes alive
  std::shared_ptr<const ProcModel::ProcModelResource> archetype_;
  std::shared_ptr<const ProcModel::Subsystem::DeformedMeshes> deformed_;

  std::vector<Part> parts_;
  std::vector<std::string> hidden_;  // part ids
  std::string solo_;                 // the part shown alone; empty: none
  bool colours_ = false;             // each part in its own colour
  bool origin_ = false;
  float extent_ = 1.0f;  // about how far the model reaches from its origin
  std::vector<PreviewMesh> meshes_;
};

#endif  // PROCMODEL_PREVIEW_H
