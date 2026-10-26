#pragma once

#include <SDL.h>
#include <bgfx/bgfx.h>
#include <glm/glm.hpp>
#include <vector>

namespace Platform {

/**
 * Returns the pixel size of the drawable area (HiDPI aware) */
void GetDrawableSize(SDL_Window* window, int* out_w, int* out_h);

// Physical pixels per window unit: 2 on Retina, 1 on Windows/Linux.
float GetFramebufferScale(SDL_Window* window);

// The user's display scaling, to apply to UI sizes: 1 on macOS, 1.25 / 1.5 /
// etc on Windows
float GetContentScale(SDL_Window* window);

// Call before SDL_Init. Windows: opt into per-monitor DPI awareness so the
// OS doesn't bitmap-stretch the window. No-op elsewhere.
void EnableHighDpi();

/**
 * Returns the DPI scaling factor of the window.
 * Default is 1.0f on non-HiDPI or unsupported platforms. */
// Deprecated: same as GetFramebufferScale. TODO: migrate callers, then remove
float GetDPIScale(SDL_Window* window);

void SetupBGFXPlatformData(bgfx::Init& init, SDL_Window* window);

void InitSDLForImGui(SDL_Window* window);

/* Handling of fullscreen mode */
void ToggleBorderlessFullscreen(SDL_Window* win, bool enable);
bool IsBorderlessFullscreen(SDL_Window* win);
void RefreshFramebufferSize(SDL_Window* win);  // bgfx+ImGui sync
bool InFullscreenSpace(SDL_Window* w);

void LockMinSize(SDL_Window* w, int minW, int minH);
void RestoreMinSize(SDL_Window* w);

// Style the native title bar to match the app per platform:
//   macOS:      exact color, transparent bar (content extends under it)
//   Windows 11: exact color, dark text and buttons
//   Windows 10: dark text and buttons, system dark color (no custom color)
//   Linux:      unchanged (the window manager owns the title bar)
// Returns true if the bar now shows exactly this color.
bool StyleTitleBar(SDL_Window* window, float r, float g, float b);

// App-drawn title bar. Where the platform has no native bar to keep (Windows,
// Linux), the app removes it and draws its own row with window buttons; macOS
// keeps its native (transparent) bar
bool DrawsWindowButtons();
void EnableCustomTitleBar(SDL_Window* window);  // call once after creation

// In window units (same as ImGui coordinates)
struct WindowRect {
  float x, y, w, h;
};

// The top `bottom` units of the window drag it (and double-click / snap,
// where the OS supports it), except over `interactive` rects. Set every frame
// by the title bar; bottom = 0 disables it
void SetTitleBarRegion(float bottom, const std::vector<WindowRect>& interactive);
bool InTitleBarDragArea(float x, float y);

void MinimizeWindow(SDL_Window* window);
bool IsWindowMaximized(SDL_Window* window);
void ToggleMaximizeWindow(SDL_Window* window);
void RequestWindowClose();  // same path as the OS close button (SDL_QUIT)

// Text input control
void DisableTextInput();
void EnableTextInput();
bool IsTextInputActive();

// Cursor utilities
// TODO: move to /input/cursor
glm::vec2 GetCursorPosition();
void SetCursorPosition(SDL_Window* window, const glm::vec2& pos);
glm::vec2 GetGlobalCursorPosition();
void SetGlobalCursorPosition(const glm::vec2& pos);

}  // namespace Platform
