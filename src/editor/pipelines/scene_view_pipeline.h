#ifndef SCENE_VIEW_PIPELINE_H
#define SCENE_VIEW_PIPELINE_H

#include <bgfx/bgfx.h>
#include <glm/glm.hpp>

#include "engine/ecs/ecs_collection.h"
#include "engine/renderer/draw_list.h"
#include "engine/renderer/frame_graph.h"
#include "engine/renderer/passes/pre_pass.h"
#include "engine/renderer/passes/ssao_pass.h"
#include "engine/transform/transform_system.h"

// Reference grid appearance (world units)
struct GridParams {
  float cell_size = 1.0f;
  int major_every = 10;
  float fade_start = 200.0f;
  float fade_end = 500.0f;
  float opacity = 1.0f;
  bool axis_lines = true;
  glm::vec3 line_color = glm::vec3(0.3f);
  glm::vec3 axis_x_color = glm::vec3(1.0f, 0.3f, 0.3f);
  glm::vec3 axis_z_color = glm::vec3(0.3f, 0.3f, 1.0f);
};

// Scene view icon appearance (world units)
struct BillboardParams {
  float opacity = 0.75f;
  float size = 0.5f;
  bool depth_aware = false;
};

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
  void SetGridParams(const GridParams& params) { grid_params_ = params; }
  void SetBillboardParams(const BillboardParams& params) {
    billboard_params_ = params;
  }

  // Pipeline settings
  void SetWireframe(bool enabled) { wireframe_ = enabled; }
  void SetShowSkybox(bool enabled) { show_skybox_ = enabled; }
  void SetShowGizmos(bool enabled) { show_gizmos_ = enabled; }
  void SetShowGrid(bool enabled) { show_grid_ = enabled; }
  void SetShowSelectionOutline(bool enabled) {
    show_selection_outline_ = enabled;
  }
  void SetUnlit(bool enabled) { unlit_ = enabled; }

  // TODO: move to a post-process volume/profile (UE: Post Process Volume)
  AmbientOcclusionSettings& GetAmbientOcclusionSettings() {
    return ambient_occlusion_;
  }

  // Settings
  bool wireframe_;
  bool show_skybox_;
  bool show_terrain_;
  bool show_water_;
  bool show_gizmos_;
  bool render_shadows_;
  bool show_grid_;
  bool show_selection_outline_;
  bool unlit_ = false;  // matcap placeholder shading instead of lighting

  uint32_t msaa_samples_;

private:
  // Creates all passes
  void CreatePasses();

  // Destroys all passes
  void DestroyPasses();

  // Forward pass for scene view
  // SceneViewForwardPass scene_view_forward_pass_;

  // Register nodes with FrameGraph
  void RegisterNodes();

  void RenderReflectionNode(const Node::Context& context);
  void RenderPrePassNode(const Node::Context& context);
  void RenderSSAONode(const Node::Context& context);
  void RenderForwardNode(const Node::Context& context);
  void RenderSelectionOutlineNode(const Node::Context& context);
  void RenderGizmosNode(const Node::Context& context);
  void RenderShadowNode(const Node::Context& context);
  void RenderGridNode(const Node::Context& context);

  // Mesh helpers
  void RenderMesh(TransformComponent& transform,
                  MeshRendererComponent& renderer);
  // One lit draw: of `model`, or of the instances bound beforehand
  // `pose`: a skinned mesh's, of `pose_count` nodes (DrawList::SetBones)
  void SubmitLit(const Mesh& mesh, MaterialHandle material,
                 const glm::mat4* model, const glm::mat4* pose = nullptr,
                 uint32_t pose_count = 0);
  void RenderSelectedEntity(EntityContainer* entity);
  void RenderSelectedEntityOutline(EntityContainer* entity);

  void BuildReferenceGrid(const glm::mat4& view_projection);

  // The shadow map's light view and projection, from the directional light
  void UpdateLightMatrices();

  // Camera
  TransformComponent god_camera_transform_;
  CameraComponent god_camera_root_;
  Camera god_camera_;

  // Transform system
  TransformSystem transform_system_;

  // Matrix cache
  glm::mat4 view_;
  glm::mat4 projection_;
  glm::mat4 light_view_;
  glm::mat4 light_projection_;

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
  bgfx::ProgramHandle shadow_instanced_program_;
  bgfx::ProgramHandle shadow_skinned_program_ = BGFX_INVALID_HANDLE;

  // The frame's meshes, grouped for instanced draws: what the camera sees,
  // and what the shadow map sees
  DrawList draws_;
  DrawList shadow_draws_;

  // Grid resources
  const Mesh* grid_mesh_;
  Geometry::MeshData grid_data_;
  GridParams grid_params_;

  // Scene icon settings
  BillboardParams billboard_params_;

  // Screen space ambient occlusion of the sky light
  PrePass pre_pass_;
  SSAOPass ssao_pass_;
  AmbientOcclusionSettings ambient_occlusion_;
};

#endif  // SCENE_VIEW_PIPELINE_H