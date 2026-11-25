#include "imgizmo.h"

#include <bgfx/bgfx.h>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <cmath>

#include "engine/context/engine_context.h"
#include "engine/core/logger.h"
#include "engine/math/transformation.h"
#include "engine/renderer/graphics_renderer.h"
#include "engine/renderer/model/mesh.h"

// Global resource
IMGizmo::StaticData IMGizmo::static_data_;

IMGizmo::IMGizmo()
    : color(glm::vec3(1.0f)),
      opacity(1.0f),
      foreground(false),
      icon_scale(0.5f),
      shape_render_stack_(),
      icon_render_stack_() {}

void IMGizmo::Create() {
  if (static_data_.loaded)
    return;

  ResourceManager& resource = EngineContext::resourceManager();

  // TODO: Load shape meshes for component gizmos, include icons
  // handle shaders
  // auto [boxId, box] =
  // EngineContext::resourceManager().Create<Model>("gizmo-box");
  // box->Load("resources/primitives/box.fbx");
  // static_data_.box_mesh = box->QueryMesh(0);

  // Initialize line vertex layout
  static_data_.line_layout.begin()
      .add(bgfx::Attrib::Position, 3, bgfx::AttribType::Float)
      .add(bgfx::Attrib::Color0, 4, bgfx::AttribType::Uint8, true)
      .end();

  // Load gizmo shaders
  // TODO: use 'shader_pool' instead
  static_data_.line_shader = EngineContext::Shader().LoadProgram(
      "gizmo_line", "vs_gizmo_line.bin", "fs_gizmo_line.bin");
  static_data_.icon_shader = EngineContext::Shader().LoadProgram(
      "gizmo_icon", "vs_gizmo_icon.bin", "fs_gizmo_icon.bin");

  if (!bgfx::isValid(static_data_.line_shader)) {
    Logger::getInstance().Log(LogLevel::Warning,
                              "IMGizmo: Failed to load line shader, "
                              "using fallback rendering");
  }

  if (!bgfx::isValid(static_data_.icon_shader)) {
    Logger::getInstance().Log(LogLevel::Warning,
                              "IMGizmo: Failed to load icon shader");
  }

  // Create plane mesh
  static_data_.plane_mesh = CreatePlaneMesh();

  static_data_.loaded = true;
  Logger::getInstance().Log(LogLevel::Debug, "IMGizmo: Initialized");
}

void IMGizmo::NewFrame(bgfx::ViewId view_id) {
  primitive_render_stack_.clear();
  shape_render_stack_.clear();
  icon_render_stack_.clear();
  current_view_id_ = view_id;
}

void IMGizmo::RenderAll(const glm::mat4& view_projection) {
  RenderPrimitives(view_projection);
  RenderShapes(view_projection);
  RenderIcons(view_projection);
}

// RENDER
void IMGizmo::RenderPrimitives(const glm::mat4& view_projection) {
  if (!bgfx::isValid(static_data_.line_shader))
    return;

  const uint32_t num_lines =
      static_cast<uint32_t>(primitive_render_stack_.size());
  if (num_lines == 0)
    return;

  // Each line = 2 vertices
  const uint32_t num_vertices = num_lines * 2;
  if (bgfx::getAvailTransientVertexBuffer(num_vertices,
                                          static_data_.line_layout) <
      num_vertices)
    return;

  bgfx::TransientVertexBuffer tvb;
  bgfx::allocTransientVertexBuffer(&tvb, num_vertices,
                                   static_data_.line_layout);

  // Pack vertices (Position + Color)
  struct LineVertex {
    float x, y, z;
    uint32_t abgr;
  };
  LineVertex* vertex = reinterpret_cast<LineVertex*>(tvb.data);

  // Depth-tested lines first, then foreground ones, so each group is one
  // contiguous range of the buffer
  uint32_t num_depth_tested = 0;
  for (bool foreground : {false, true}) {
    for (const auto& primitive : primitive_render_stack_) {
      if (primitive.state.foreground != foreground)
        continue;

      const glm::vec3 c = glm::clamp(primitive.state.color, 0.0f, 1.0f);
      const uint32_t r = static_cast<uint32_t>(c.r * 255.0f);
      const uint32_t g = static_cast<uint32_t>(c.g * 255.0f);
      const uint32_t b = static_cast<uint32_t>(c.b * 255.0f);
      const uint32_t a = static_cast<uint32_t>(
          glm::clamp(primitive.state.opacity, 0.0f, 1.0f) * 255.0f);
      const uint32_t abgr = (a << 24) | (b << 16) | (g << 8) | r;

      *vertex++ = {primitive.start.x, primitive.start.y, primitive.start.z,
                   abgr};
      *vertex++ = {primitive.end.x, primitive.end.y, primitive.end.z, abgr};
    }
    if (!foreground)
      num_depth_tested = static_cast<uint32_t>(
          vertex - reinterpret_cast<LineVertex*>(tvb.data));
  }

  // Lines are in world space: the vertex shader only uses the view projection
  const uint64_t base_state = BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A |
                              BGFX_STATE_BLEND_ALPHA | BGFX_STATE_PT_LINES |
                              BGFX_STATE_LINEAA;

  if (num_depth_tested > 0) {
    bgfx::setVertexBuffer(0, &tvb, 0, num_depth_tested);
    bgfx::setState(base_state | BGFX_STATE_DEPTH_TEST_LESS);
    bgfx::submit(current_view_id_, static_data_.line_shader);
  }
  if (num_vertices > num_depth_tested) {
    bgfx::setVertexBuffer(0, &tvb, num_depth_tested,
                          num_vertices - num_depth_tested);
    bgfx::setState(base_state);
    bgfx::submit(current_view_id_, static_data_.line_shader);
  }
}

void IMGizmo::RenderShapes(const glm::mat4& view_projection) {
  if (!bgfx::isValid(static_data_.line_shader))
    return;

  for (int32_t i = 0; i < shape_render_stack_.size(); i++) {
    ShapeRenderTarget gizmo = shape_render_stack_[i];
  }
}

void IMGizmo::RenderIcons(const glm::mat4& view_projection) {
  if (!bgfx::isValid(static_data_.icon_shader))
    return;

  auto& graphics = static_cast<GraphicsRenderer&>(EngineContext::Renderer());
  const Mesh* mesh = QueryMesh(Shape::PLANE);

  // Stands the XZ plane up, so -Z (v = 0) is the top of the icon
  const glm::quat upright =
      glm::angleAxis(glm::radians(90.0f), glm::vec3(1.0f, 0.0f, 0.0f));

  for (const auto& icon : icon_render_stack_) {
    glm::mat4 model =
        GetModelMatrix(icon.position, icon.camera_transform.rotation_ * upright,
                       glm::vec3(icon.scale));
    const float tint[4] = {icon.state.color.r, icon.state.color.g,
                           icon.state.color.b, icon.state.opacity};
    bgfx::TextureHandle texture = {static_cast<uint16_t>(icon.icon_texture)};

    bgfx::setUniform(graphics.GetMeshColorUniform(), tint);
    bgfx::setTexture(0, graphics.GetAlbedoSamplerUniform(), texture);
    bgfx::setTransform(glm::value_ptr(model));
    mesh->Bind();

    // Transparent: tested against depth, but never written to it
    uint64_t render_state =
        BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A | BGFX_STATE_BLEND_ALPHA;

    if (!icon.state.foreground) {
      render_state |= BGFX_STATE_DEPTH_TEST_LESS;
    }

    bgfx::setState(render_state);
    bgfx::submit(current_view_id_, static_data_.icon_shader);
  }
}

// PRIMITIVES
void IMGizmo::Line(const glm::vec3& start, const glm::vec3& end) {
  PrimitiveRenderTarget primitive(start, end, GetCurrentState());
  primitive_render_stack_.push_back(primitive);
}

// WIRE SHAPES
void IMGizmo::Circle(const glm::vec3& center, const glm::vec3& normal,
                     float radius, uint32_t segments) {
  // Two axes spanning the circle's plane
  const glm::vec3 n = glm::normalize(normal);
  const glm::vec3 helper = std::abs(n.y) < 0.99f ? glm::vec3(0.0f, 1.0f, 0.0f)
                                                 : glm::vec3(1.0f, 0.0f, 0.0f);
  const glm::vec3 u = glm::normalize(glm::cross(n, helper));
  const glm::vec3 v = glm::cross(n, u);

  glm::vec3 previous = center + u * radius;
  for (uint32_t i = 1; i <= segments; ++i) {
    const float angle = glm::two_pi<float>() * static_cast<float>(i) /
                        static_cast<float>(segments);
    const glm::vec3 point =
        center + (u * std::cos(angle) + v * std::sin(angle)) * radius;
    Line(previous, point);
    previous = point;
  }
}

void IMGizmo::WireSphere(const glm::vec3& center, float radius,
                         const glm::quat& rotation) {
  Circle(center, rotation * glm::vec3(1.0f, 0.0f, 0.0f), radius);
  Circle(center, rotation * glm::vec3(0.0f, 1.0f, 0.0f), radius);
  Circle(center, rotation * glm::vec3(0.0f, 0.0f, 1.0f), radius);
}

void IMGizmo::WireCone(const glm::vec3& apex, const glm::quat& rotation,
                       float length, float angle) {
  // +Z is forward
  const glm::vec3 forward = rotation * glm::vec3(0.0f, 0.0f, 1.0f);
  const glm::vec3 right = rotation * glm::vec3(1.0f, 0.0f, 0.0f);
  const glm::vec3 up = rotation * glm::vec3(0.0f, 1.0f, 0.0f);

  // Past 90 degrees the cone has no base in front of the apex
  const float clamped = glm::clamp(angle, 0.0f, 89.0f);
  const glm::vec3 base = apex + forward * length;
  const float radius = length * std::tan(glm::radians(clamped));

  Circle(base, forward, radius);
  for (const glm::vec3& side : {right, -right, up, -up})
    Line(apex, base + side * radius);
}

void IMGizmo::Frustum(const glm::vec3& position, const glm::quat& rotation,
                      float fov, float aspect, float near_plane,
                      float far_plane) {
  // +Z is forward, fov is vertical, in degrees
  const glm::vec3 forward = rotation * glm::vec3(0.0f, 0.0f, 1.0f);
  const glm::vec3 right = rotation * glm::vec3(1.0f, 0.0f, 0.0f);
  const glm::vec3 up = rotation * glm::vec3(0.0f, 1.0f, 0.0f);
  const float tan_half = std::tan(glm::radians(fov) * 0.5f);

  auto corners = [&](float distance, glm::vec3 out[4]) {
    const glm::vec3 center = position + forward * distance;
    const glm::vec3 h = up * (distance * tan_half);
    const glm::vec3 w = right * (distance * tan_half * aspect);
    out[0] = center - w - h;
    out[1] = center + w - h;
    out[2] = center + w + h;
    out[3] = center - w + h;
  };

  glm::vec3 near_corners[4];
  glm::vec3 far_corners[4];
  corners(near_plane, near_corners);
  corners(far_plane, far_corners);

  for (int i = 0; i < 4; ++i) {
    const int next = (i + 1) % 4;
    Line(near_corners[i], near_corners[next]);
    Line(far_corners[i], far_corners[next]);
    Line(near_corners[i], far_corners[i]);
  }
}

// SHAPES
void IMGizmo::Plane(const glm::vec3& position, const glm::vec3& scale,
                    const glm::quat& rotation) {
  /* ShapeRenderTarget gizmo(Shape::PLANE, position, rotation, scale, false,
                          GetCurrentState());*/

  // shape_render_stack_.push_back(gizmo);
}

// ICONS
void IMGizmo::Icon3D(uint32_t IconTexture, const glm::vec3& position,
                     TransformComponent& CameraTransform) {
  if (IconTexture == bgfx::kInvalidHandle)
    return;

  IconRenderTarget icon(IconTexture, position, icon_scale, CameraTransform,
                        GetCurrentState());
  icon_render_stack_.push_back(icon);
}

IMGizmo::RenderState IMGizmo::GetCurrentState() {
  RenderState state;
  state.color = color;
  state.opacity = opacity;
  state.foreground = foreground;
  return state;
}

const Mesh* IMGizmo::QueryMesh(Shape shape) {
  switch (shape) {
    case Shape::PLANE:
      return static_data_.plane_mesh;
    default:
      return nullptr;
  }
}

glm::mat4 IMGizmo::GetModelMatrix(glm::vec3 position, glm::quat rotation,
                                  glm::vec3 scale) {
  glm::mat4 model(1.0f);

  // Position
  glm::vec3 worldPos = position;
  model = glm::translate(model, worldPos);

  // Rotation
  glm::quat worldRot = rotation;
  model = model * glm::mat4_cast(glm::normalize(worldRot));

  // Scale
  model = glm::scale(model, scale);

  return model;
}

const Mesh* IMGizmo::CreatePlaneMesh() {
  Geometry::MeshData plane_data;

  plane_data.vertices = {
      {{-1.0f, 0.0f, -1.0f}, {0.0f, 1.0f, 0.0f}, {0.0f, 0.0f}, {}, {}},
      {{1.0f, 0.0f, -1.0f}, {0.0f, 1.0f, 0.0f}, {1.0f, 0.0f}, {}, {}},
      {{1.0f, 0.0f, 1.0f}, {0.0f, 1.0f, 0.0f}, {1.0f, 1.0f}, {}, {}},
      {{-1.0f, 0.0f, 1.0f}, {0.0f, 1.0f, 0.0f}, {0.0f, 1.0f}, {}, {}},
  };

  plane_data.indices = {0, 1, 2, 0, 2, 3};

  return new Mesh(plane_data);
}
