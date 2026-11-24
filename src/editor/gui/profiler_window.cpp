#include "profiler_window.h"

#include <algorithm>
#include <cfloat>
#include <cstdio>

#include "engine/diagnostics/frame_stats.h"

namespace {

constexpr float kBudgetMs = 1000.0f / 60.0f;

// A graph of the last frames up to their slowest, at least `floor` high;
// gives its top
float Graph(const char* id, const std::vector<float>& history, float floor,
           const char* overlay, float height) {
  const float top = std::max(
      floor, history.empty()
                 ? 0.0f
                 : *std::max_element(history.begin(), history.end()));
  ImGui::PlotLines(id, history.data(), static_cast<int>(history.size()), 0,
                   overlay, 0.0f, top, ImVec2(-FLT_MIN, height));
  return top;
}

}  // namespace

void ProfilerWindow::Render() {
  GUIUtils::HideDockTabBar();
  ImGui::Begin("Profiler", nullptr);
  const Diagnostics::FrameStatsSnapshot& stats = Diagnostics::GetFrameStats();

  // The frames, with the 60 FPS budget as a line
  char overlay[64];
  std::snprintf(overlay, sizeof(overlay), "Frame %.2f ms, slowest %.2f ms",
                stats.frame_ms_avg, stats.frame_ms_max);
  const auto& frames = stats.frame_history;
  const float top = Graph("##frames", frames, 2.0f * kBudgetMs, overlay, 80.0f);
  const ImVec2 low = ImGui::GetItemRectMin();
  const ImVec2 high = ImGui::GetItemRectMax();
  const float y = high.y - (high.y - low.y) * kBudgetMs / top;
  ImGui::GetWindowDrawList()->AddLine(ImVec2(low.x, y), ImVec2(high.x, y),
                                      EditorColor::text_dim);

  if (stats.sections.empty()) {
    ImGui::TextDisabled("No system timed its work in the last frames");
    ImGui::End();
    return;
  }

  // What the top sections leave of each frame
  std::vector<float> untimed = frames;
  float untimed_ms = stats.frame_ms_avg;
  for (const auto& section : stats.sections)
    if (section.depth == 0) {
      untimed_ms -= section.ms;
      for (size_t i = 0; i < untimed.size() && i < section.history.size(); ++i)
        untimed[i] -= section.history[i];
    }
  const float untimed_max =
      untimed.empty() ? 0.0f : *std::max_element(untimed.begin(), untimed.end());

  if (ImGui::BeginTable("##sections", 4,
                        ImGuiTableFlags_RowBg |
                            ImGuiTableFlags_BordersInnerV |
                            ImGuiTableFlags_ScrollY)) {
    ImGui::TableSetupScrollFreeze(0, 1);
    ImGui::TableSetupColumn("Section", ImGuiTableColumnFlags_WidthStretch, 2.0f);
    ImGui::TableSetupColumn("ms", ImGuiTableColumnFlags_WidthFixed, 64.0f);
    ImGui::TableSetupColumn("Slowest", ImGuiTableColumnFlags_WidthFixed, 64.0f);
    ImGui::TableSetupColumn("Last frames", ImGuiTableColumnFlags_WidthStretch,
                            3.0f);
    ImGui::TableHeadersRow();
    const auto row = [](const char* name, int depth, float ms, float max_ms,
                        const std::vector<float>& history) {
      ImGui::TableNextRow();
      ImGui::TableNextColumn();
      ImGui::SetCursorPosX(ImGui::GetCursorPosX() +
                           depth * ImGui::GetStyle().IndentSpacing);
      ImGui::TextUnformatted(name);
      ImGui::TableNextColumn();
      ImGui::Text("%.2f", ms);
      ImGui::TableNextColumn();
      ImGui::Text("%.2f", max_ms);
      ImGui::TableNextColumn();
      ImGui::PushID(name);
      ImGui::PushID(depth);
      Graph("##history", history, 1.0f, nullptr, ImGui::GetTextLineHeight());
      ImGui::PopID();
      ImGui::PopID();
    };
    for (const auto& section : stats.sections)
      row(section.name, section.depth, section.ms, section.max_ms,
          section.history);
    row("Untimed", 0, untimed_ms, untimed_max, untimed);
    // What bgfx::frame (Submit) waits on
    if (!stats.render_thread.empty()) {
      ImGui::TableNextRow();
      ImGui::TableNextColumn();
      ImGui::TextDisabled("bgfx");
    }
    for (const auto& section : stats.render_thread)
      row(section.name, 1, section.ms, section.max_ms, section.history);
    ImGui::EndTable();
  }
  ImGui::End();
}
