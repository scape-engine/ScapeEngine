#pragma once

#include <SDL.h>
#include <bgfx/bgfx.h>
#include <glm/glm.hpp>

namespace Platform {

/**
 * Returns the pixel size of the drawable area (HiDPI aware) */
void GetDrawableSize(SDL_Window* window, int* out_w, int* out_h);

/**
 * Returns the DPI scaling factor of the window.
 * Default is 1.0f on non-HiDPI or unsupported platforms. */
float GetDPIScale(SDL_Window* window);

/**
 * macOS: Creates and attaches a CAMetalLayer to a Cocoa window.
 * Other platforms: returns nullptr */
void* CreateMetalLayer(void* native_window);

void SetupBGFXPlatformData(bgfx::Init& init, SDL_Window* window);

void InitSDLForImGui(SDL_Window* window);

/* Handling of fullscreen mode */
void ToggleBorderlessFullscreen(SDL_Window* win, bool enable);
bool IsBorderlessFullscreen(SDL_Window* win);
void RefreshFramebufferSize(SDL_Window* win);  // bgfx+ImGui sync
bool InFullscreenSpace(SDL_Window* w);

void LockMinSize(SDL_Window* w, int minW, int minH);
void RestoreMinSize(SDL_Window* w);

// Transparent titlebar, incl traffic lights and content under it
void StyleTitleBar(SDL_Window* window, float r, float g, float b);

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
