/**************************************************************************
 * FrameGraph
 * ----------
 * Backend-agnostic pass orchestration layer owned by Runtime.
 *
 * Models:
 *  - Passes through nodes (depth pre-pass, SSAO, forward lighting, post-FX,
 *etc.)
 *  - Attachments (color/depth/intermediate) as logical IDs only
 *  - Implicit edges via node.reads / node.writes
 *
 * Responsibilities:
 *  - Node registration (AddNode) and attachment registration (AddAttachment)
 *  - Simple sequencing (Bake -> current: insertion order)
 *  - Per-frame execution (Render) invoking node lambdas
 *  - Resize hook (Rebuild) updating logical sizes and re-baking
 *
 * TODO:
 *  - Topological sort from reads/writes (real dependency resolution)
 *  - Attachment validation (unwritten reads, multiple writers, cycles)
 *  - Transient resource allocation/reuse via RendererBase (populate texture/fbo
 *ids)
 *  - Attachment metadata: format, clear values, sample count, flags
 *  - Populate Context.view_id / width / height consistently (policy)
 *  - Build(GraphDesc) as a single-shot construction API?
 **************************************************************************/

#include "frame_graph.h"

#include <algorithm>
#include "engine/core/logger.h"

void FrameGraph::AddAttachment(const AttachmentDesc& desc) {
  // Add attachment to graph
  if (attachments_.find(desc.name) != attachments_.end()) {
    Logger::getInstance().Log(LogLevel::Warning,
                              "FrameGraph: Attachment '" + desc.name +
                                  "' already exists, overwriting");
  }

  AttachmentRT rt;
  rt.desc = desc;
  attachments_[desc.name] = rt;
}

void FrameGraph::AddNode(const Node& node) {
  // Add node to graph
  nodes_.push_back(node);
}

void FrameGraph::Bake() {
  // Reset execution order
  exec_order_.clear();

  // TODO: Basic sequential topo sort, enhance later
  for (auto& node : nodes_) {
    exec_order_.push_back(&node);
  }

  Logger::getInstance().Log(
      LogLevel::Info,
      "FrameGraph: Baked with " + std::to_string(attachments_.size()) +
          " attachments and " + std::to_string(nodes_.size()) + " nodes");
}

void FrameGraph::Clear() {
  // Reset graph
  attachments_.clear();
  nodes_.clear();
  exec_order_.clear();
}

void FrameGraph::Render() {
  // Execute nodes in order
  // Context is identical for every node (for now), build it once per frame
  const Node::Context context{
      .renderer = renderer_,
      .view_id = 0,  // Will be set properly in future
      .width = 0,    // Will be set properly in future
      .height = 0,   // Will be set properly in future

      // Lambda to get texture ID
      .tex = [this](const std::string& name) -> uint32_t {
        auto it = attachments_.find(name);
        return (it != attachments_.end()) ? it->second.texture_id
                                          : 0xFFFFFFFF;
      },

      // Lambda to get FBO ID
      .fbo = [this](const std::string& name) -> uint32_t {
        auto it = attachments_.find(name);
        return (it != attachments_.end()) ? it->second.fbo_id : 0xFFFFFFFF;
      },
      .globals = globals_};

  for (Node* node : exec_order_) {
    if (node && node->execute)
      node->execute(context);
  }
}

void FrameGraph::Rebuild(uint16_t width, uint16_t height) {
  // ! Sizes are only filled once: after the first Rebuild, width/height are
  // ! non-zero, so later resizes don't update window-relative attachments.
  // ! Harmless today (nothing reads desc.width/height). Fix when attachments
  // ! get allocated: store a `relative` flag in AttachmentDesc and always
  // ! overwrite those.
  // Update dimensions of attachments
  for (auto& [name, rt] : attachments_) {
    if (rt.desc.width == 0)
      rt.desc.width = width;
    if (rt.desc.height == 0)
      rt.desc.height = height;
  }

  // Re-bake graph
  Bake();

  Logger::getInstance().Log(LogLevel::Debug,
                            "FrameGraph: Rebuilt with dimensions " +
                                std::to_string(width) + "x" +
                                std::to_string(height));
}