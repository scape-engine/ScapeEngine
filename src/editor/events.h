#ifndef EDITOR_EVENTS_H
#define EDITOR_EVENTS_H

#include <boost/signals2/signal.hpp>
#include <entt/entt.hpp>

#include "engine/memory/resource.h"

using Entity = entt::entity;

namespace EditorEvents {

inline boost::signals2::signal<void()> game_start_pressed;
inline boost::signals2::signal<void()> game_end_pressed;
inline boost::signals2::signal<void()> editor_exit_pressed;
inline boost::signals2::signal<void(InputEvent)> game_inputed;

inline boost::signals2::signal<void(ResourceID)> open_graph_editor;
inline boost::signals2::signal<void()> close_graph_editor;

inline boost::signals2::signal<void(Entity)> entity_selected;
inline boost::signals2::signal<void()> selection_cleared;

inline boost::signals2::signal<void(int)> item_selected;

inline boost::signals2::signal<void(Entity)> focus_entity;

}  // namespace EditorEvents

#endif  // EDITOR_EVENTS_H