#ifndef POPUP_MENU_H
#define POPUP_MENU_H

#include <string>

namespace PopupMenu {

// Begin new popup
bool Begin();

// End current popup
void End();

// Pop menu variations
void Pop();

// Create new item with icon
bool Item(const char* icon, std::string title);

// Create new item without icon
bool ItemLight(std::string title);

// Create new menu within popup
bool Menu(const char* icon, std::string title);

// End current menu
void EndMenu();

// Draw separator
void Separator();

// Request the popup to open
void Open();

}  // namespace PopupMenu

#endif  // POPUP_MENU_H