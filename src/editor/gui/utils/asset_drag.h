#ifndef ASSET_DRAG_H
#define ASSET_DRAG_H

#include <imgui.h>

#include <string>
#include <vector>

// Project files dragged in the editor (from the asset browser): one payload
// type for every file, its data the file's project-relative path. A drop
// target takes the files whose extensions it handles
namespace AssetDrag {

// Starts dragging the file (project-relative) from the last item
void Source(const std::string& path);

// Within BeginDragDropTarget: the dragged file's payload when its extension
// is one of `extensions` (".glb"; any case), else null; nothing accepted
const ImGuiPayload* Peek(const std::vector<std::string>& extensions);

// Within BeginDragDropTarget: AcceptDragDropPayload for the files with one
// of `extensions`; null for others
const ImGuiPayload* Accept(const std::vector<std::string>& extensions,
                           ImGuiDragDropFlags flags = 0);

// The payload's file, project-relative
std::string Path(const ImGuiPayload& payload);

}  // namespace AssetDrag

#endif  // ASSET_DRAG_H
