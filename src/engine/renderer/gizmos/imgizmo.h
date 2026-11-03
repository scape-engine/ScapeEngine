#ifndef IMGIZMO_H
#define IMGIZMO_H

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <vector>

#include "engine/ecs/components/camera_component.h"
#include "engine/ecs/components/transform_component.h"

#include "engine/memory/resource_manager.h"
#include "engine/renderer/texture/texture2d.h"

class IMGizmo {
public:
  IMGizmo();

  // Initialize static resources
  void Create();

  // Clear render queue for new frame
  void NewFrame(bgfx::ViewId view_id);

  // Render all queued gizmos
  void RenderAll(const glm::mat4& ViewProjection);

  void RenderIcons(const glm::mat4& viewProjection);

  void RenderShapes(const glm::mat4& viewProjection);

  void RenderPrimitives(const glm::mat4& viewProjection);

  // Render state settings
  glm::vec3 color;
  float opacity;
  bool foreground;
  float icon_scale;

  // Primitives & shapes
  void Line(const glm::vec3& start, const glm::vec3& end);
  void Box(const glm::vec3& position, const glm::vec3& scale = glm::vec3(1.0f),
           const glm::quat& rotation = glm::identity<glm::quat>());
  void Plane(const glm::vec3& position,
             const glm::vec3& scale = glm::vec3(1.0f, 1.0f, 1.0f),
             const glm::quat& rotation = glm::identity<glm::quat>());

  void BoxWire(const glm::vec3& position,
               const glm::vec3& scale = glm::vec3(1.0f),
               const glm::quat& rotation = glm::identity<glm::quat>());

  // Icons
  void Icon3D(uint32_t IconTexture, const glm::vec3& position,
              TransformComponent& CameraTransform);

private:
  struct StaticData {
    bool loaded = false;
    bgfx::ProgramHandle line_shader = BGFX_INVALID_HANDLE;
    bgfx::ProgramHandle fill_shader = BGFX_INVALID_HANDLE;
    bgfx::ProgramHandle icon_shader = BGFX_INVALID_HANDLE;
    bgfx::VertexLayout line_layout;

    const Mesh* plane_mesh = nullptr;
    const Mesh* sphere_mesh = nullptr;
    const Mesh* box_mesh = nullptr;
  };

  struct RenderState {
    glm::vec3 color;
    float opacity;
    bool foreground;
  };

  enum class Shape { PLANE, BOX, SPHERE };

  // TODO: stub!
  struct ShapeRenderTarget {
    Shape shape;
    glm::vec3 position;
    glm::vec3 scale;
    glm::quat rotation;
    RenderState state;
    bool wireframe;

    ShapeRenderTarget(Shape shape, const glm::vec3& position,
                      const glm::vec3& scale, const glm::quat& rotation,
                      RenderState state, bool wireframe)
        : shape(shape),
          position(position),
          scale(scale),
          rotation(rotation),
          state(state),
          wireframe(wireframe) {}
  };

  struct PrimitiveRenderTarget {
    glm::vec3 start;
    glm::vec3 end;
    RenderState state;

    PrimitiveRenderTarget(const glm::vec3& start, const glm::vec3& end,
                          RenderState state)
        : start(start), end(end), state(state) {}
  };

  struct IconRenderTarget {
    uint32_t icon_texture;
    glm::vec3 position;
    float scale;
    TransformComponent& camera_transform;
    RenderState state;

    IconRenderTarget(uint32_t texture, glm::vec3 position, float scale,
                     TransformComponent& camera_transform, RenderState state)
        : icon_texture(texture),
          position(position),
          scale(scale),
          camera_transform(camera_transform),
          state(state) {};
  };

  std::vector<ShapeRenderTarget> shape_render_stack_;
  std::vector<PrimitiveRenderTarget> primitive_render_stack_;
  std::vector<IconRenderTarget> icon_render_stack_;

  bgfx::ViewId current_view_id_;
  static StaticData static_data_;

  RenderState GetCurrentState();
  const Mesh* QueryMesh(Shape shape);
  glm::mat4 GetModelMatrix(glm::vec3 position, glm::quat rotation, glm::vec3 scale);
  glm::mat4 GetModelMatrix(glm::vec3 position, glm::vec3 rotation, glm::vec3 scale);

  const Mesh* CreatePlaneMesh(); 
};

#endif  // IMGIZMO_H