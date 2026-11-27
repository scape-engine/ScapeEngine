#ifndef EDITOR_EVENTS_H
#define EDITOR_EVENTS_H

#include <boost/signals2/signal.hpp>
#include <string>
#include <entt/entt.hpp>
#include <nlohmann/json.hpp>

#include "editor/systems/asset_meta.h"
#include "engine/memory/resource.h"
#include "platform/input/input_event.h"

using Entity = entt::entity;

namespace EditorEvents {

inline boost::signals2::signal<void()> game_start_pressed;
inline boost::signals2::signal<void()> game_end_pressed;
inline boost::signals2::signal<void()> editor_exit_pressed;
inline boost::signals2::signal<void(InputEvent)> game_inputed;

// A PCG graph asset (project-relative) to edit
inline boost::signals2::signal<void(const std::string&)> open_pcg_graph;
// A procedural model's descriptor (project-relative) to show as a graph
inline boost::signals2::signal<void(const std::string&)> open_asset_graph;

inline boost::signals2::signal<void(Entity)> entity_selected;
inline boost::signals2::signal<void()> selection_cleared;
inline boost::signals2::signal<void(AssetSID)> asset_selected;

inline boost::signals2::signal<void(int)> item_selected;

inline boost::signals2::signal<void(Entity)> focus_entity;

// A scene was opened or created, previous entities no longer exist
inline boost::signals2::signal<void()> scene_opened;

// A scene file is being written: add editor state to its JSON
inline boost::signals2::signal<void(nlohmann::json&)> scene_saving;

// A scene file was read, before scene_opened: restore editor state
inline boost::signals2::signal<void(const nlohmann::json&)> scene_loaded;

}  // namespace EditorEvents

#endif  // EDITOR_EVENTS_H