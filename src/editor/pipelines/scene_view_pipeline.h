#ifndef SCENE_VIEW_PIPELINE_H
#define SCENE_VIEW_PIPELINE_H

#include <bgfx/bgfx.h>

#include "engine/ecs/ecs_collection.h"
#include "engine/renderer/frame_graph.h"
#include "engine/transform/transform_system.h"

class SceneViewPipeline {
public:
  SceneViewPipeline();

  void Create();
  void Destroy();
  void Render();

  // void RealRender();  // PLACEHOLDER: Remove this once pipeline is done

  Camera& GetGodCamera();

  // Get latest view matrix used
  const glm::mat4& GetView() const;

  // Get latest projection matrix used
  const glm::mat4& GetProjection() const;

  // TODO: void CreateFrameGraph();

  // Entity selection in game canvas
  // Children are selected with their parent, so selecting a model's root
  // outlines all of its meshes
  void SetSelectedEntities(const std::vector<Entity>& entities);

  // Pipeline settings
  void SetWireframe(bool enabled) { wireframe_ = enabled; }
  void SetShowSkybox(bool enabled) { show_skybox_ = enabled; }
  void SetShowGizmos(bool enabled) { show_gizmos_ = enabled; }
  void SetShowGrid(bool enabled) { show_grid_ = enabled; }

  // Settings
  bool wireframe_;
  bool show_skybox_;
  bool show_terrain_;
  bool show_water_;
  bool show_gizmos_;
  bool render_shadows_;
  bool show_grid_;
  uint32_t msaa_samples_;

private:
  // Creates all passes
  /* void CreatePasses();

  // Destroys all passes
  void DestroyPasses();*/

  // Forward pass for scene view
  // SceneViewForwardPass scene_view_forward_pass_;

  // Register nodes with FrameGraph
  void RegisterNodes();

  void RenderReflectionNode(const Node::Context& context);
  void RenderForwardNode(const Node::Context& context);
  void RenderSelectionOutlineNode(const Node::Context& context);
  void RenderGizmosNode(const Node::Context& context);
  void RenderShadowNode(const Node::Context& context);

  // Mesh helpers
  void RenderMesh(TransformComponent& transform,
                  MeshRendererComponent& renderer);
  void RenderSelectedEntity(EntityContainer* entity);
  void RenderSelectedEntityOutline(EntityContainer* entity);

  void BuildReferenceGrid(const glm::mat4& view_projection);

  // Camera
  TransformComponent god_camera_transform_;
  CameraComponent god_camera_root_;
  Camera god_camera_;

  // Transform system
  TransformSystem transform_system_;

  // Matrix cache
  glm::mat4 view_;
  glm::mat4 projection_;

  // Entity selection
  std::vector<Entity> selected_entities_;

  // TODO: replace with actual implementation of selection material
  // bgfx::ProgramHandle selection_material_ BGFX_INVALID_HANDLE;  //
  // placeholder

  // bool frame_initialized_;

  // Shader handles (loaded once)
  bgfx::ProgramHandle default_program_;
  bgfx::ProgramHandle selection_program_;
  bgfx::ProgramHandle debug_program_;
  bgfx::ProgramHandle shadow_program_;

  // Grid resources
  const Mesh* grid_mesh_;
  Geometry::MeshData grid_data_;
};

#endif  // SCENE_VIEW_PIPELINE_H