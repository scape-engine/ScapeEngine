#include "graph_canvas.h"

#include <cmath>
#include <string>

#include "editor/gui/styles/editor_styles.h"

GraphCanvas::GraphCanvas() {
  ed::Config config;
  // Layouts come from the graph itself, never a stale file
  config.SettingsFile = nullptr;
  // About Unreal's range: far enough to see a big graph, never lost in it
  for (const float zoom : {0.1f, 0.15f, 0.2f, 0.25f, 0.33f, 0.5f, 0.67f,
                           0.75f, 1.0f, 1.25f, 1.5f, 2.0f})
    config.CustomZoomLevels.push_back(zoom);
  context_ = ed::CreateEditor(&config);

  ed::SetCurrentEditor(context_);
  ed::Style& style = ed::GetStyle();
  const auto colour = [](ImU32 c) { return ImGui::ColorConvertU32ToFloat4(c); };
  style.Colors[ed::StyleColor_Bg] = colour(EditorColor::surface_0);
  style.Colors[ed::StyleColor_Grid] = colour(EditorColor::grid_minor);
  style.Colors[ed::StyleColor_NodeBg] = colour(EditorColor::surface_2);
  style.Colors[ed::StyleColor_NodeBorder] = colour(EditorColor::line_strong);
  style.Colors[ed::StyleColor_HovNodeBorder] =
      colour(EditorColor::accent_hover);
  style.Colors[ed::StyleColor_SelNodeBorder] = colour(EditorColor::accent);
  style.Colors[ed::StyleColor_HovLinkBorder] =
      colour(EditorColor::accent_hover);
  style.Colors[ed::StyleColor_SelLinkBorder] = colour(EditorColor::accent);
  style.Colors[ed::StyleColor_PinRect] = colour(EditorColor::accent_border);
  style.NodeRounding = EditorSizes::control_radius;
  style.NodeBorderWidth = 1.0f;
  ed::SetCurrentEditor(nullptr);
}

GraphCanvas::~GraphCanvas() {
  ed::DestroyEditor(context_);
}

bool GraphCanvas::Begin(const char* id) {
  origin_ = ImGui::GetCursorScreenPos();
  size_ = ImGui::GetContentRegionAvail();
  if (size_.x < 10.0f || size_.y < 10.0f)
    return false;
  ed::SetCurrentEditor(context_);
  ed::Begin(id);
  return true;
}

void GraphCanvas::End() {
  if (frame_in_ > 0 && --frame_in_ == 0)
    ed::NavigateToContent(0.0f);
  const float zoom = ed::GetCurrentZoom();
  ed::End();
  ed::SetCurrentEditor(nullptr);

  // Unreal's readout: "Zoom 1:1" at rest, else the scale as a percentage
  // (the canvas zooms out as its zoom grows)
  const float scale = 1.0f / zoom;
  const std::string text =
      std::abs(scale - 1.0f) < 0.01f
          ? "Zoom 1:1"
          : "Zoom " + std::to_string(std::lround(scale * 100.0f)) + "%";
  const ImVec2 extent = ImGui::CalcTextSize(text.c_str());
  ImGui::GetWindowDrawList()->AddText(
      ImVec2(origin_.x + size_.x - extent.x - 12.0f, origin_.y + 8.0f),
      EditorColor::text_dim, text.c_str());
}

namespace GraphNode {

// The node being drawn
static ed::NodeId s_node;
static float s_width = 0.0f;
static float s_left = 0.0f;
static float s_header_bottom = 0.0f;
static ImU32 s_header = 0;

static constexpr float pin_size = 12.0f;

void Begin(ed::NodeId id, float width) {
  ed::PushStyleVar(ed::StyleVar_NodePadding, ImVec4(8.0f, 6.0f, 8.0f, 8.0f));
  ed::BeginNode(id);
  s_node = id;
  s_width = width;
  s_left = ImGui::GetCursorScreenPos().x;
  s_header = 0;
}

void EndHeader(ImU32 header) {
  s_header = header;
  s_header_bottom = ImGui::GetItemRectMax().y + 6.0f;
  ImGui::Dummy(ImVec2(0.0f, 8.0f));
}

void End() {
  ImGui::Dummy(ImVec2(s_width, 0.0f));
  ed::EndNode();
  ed::PopStyleVar();
  if (s_header == 0)
    return;
  // The title bar, under the node's content (the node's rect is its item)
  const ImVec2 min = ImGui::GetItemRectMin();
  const ImVec2 max = ImGui::GetItemRectMax();
  const float border = ed::GetStyle().NodeBorderWidth;
  ed::GetNodeBackgroundDrawList(s_node)->AddRectFilled(
      ImVec2(min.x + border, min.y + border),
      ImVec2(max.x - border, s_header_bottom), s_header,
      ed::GetStyle().NodeRounding, ImDrawFlags_RoundCornersTop);
}

static void _Pin(ed::PinId id, ed::PinKind kind, bool linked) {
  ed::BeginPin(id, kind);
  ed::PinPivotAlignment(ImVec2(0.5f, 0.5f));
  const ImVec2 at = ImGui::GetCursorScreenPos();
  const float row = ImGui::GetFrameHeight();
  ImGui::Dummy(ImVec2(pin_size, row));
  const ImVec2 centre(at.x + pin_size * 0.5f, at.y + row * 0.5f);
  ImDrawList& draw_list = *ImGui::GetWindowDrawList();
  if (linked)
    draw_list.AddCircleFilled(centre, 4.5f, EditorColor::accent_border);
  draw_list.AddCircle(centre, 4.5f,
                      linked ? EditorColor::accent_border
                             : EditorColor::text_dim,
                      12, 1.5f);
  ed::EndPin();
}

void InputPin(ed::PinId id, const char* label, bool linked) {
  _Pin(id, ed::PinKind::Input, linked);
  ImGui::SameLine();
  ImGui::AlignTextToFramePadding();
  ImGui::TextUnformatted(label);
}

void OutputPin(ed::PinId id, bool linked) {
  ImGui::SameLine();
  ImGui::SetCursorScreenPos(ImVec2(s_left + s_width - pin_size,
                                   ImGui::GetCursorScreenPos().y));
  _Pin(id, ed::PinKind::Output, linked);
}

}  // namespace GraphNode
