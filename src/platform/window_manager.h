#ifndef SDL_PLATFORM_H
#define SDL_PLATFORM_H

#include <SDL.h>

#include <functional>
#include <vector>
#include "engine/core/singleton.hpp"  // ! remove engine dependency?

class WindowManager : public Singleton<WindowManager> {
private:
  SDL_Window* window_;
  int width_;
  int height_;
  float dpi_scale_;  // retina scale factor
  std::vector<std::function<void(int, int)>> resize_callbacks_;

  bool SetFullscreen(bool enable);

public:
  WindowManager()
      : window_(nullptr), width_(1280), height_(720), dpi_scale_(1.0f) {}
  ~WindowManager();

  // Prevent copying
  WindowManager(const WindowManager&) = delete;
  WindowManager& operator=(const WindowManager&) = delete;

  // Initialize with window management
  static bool Initialize(const char* title = "Scape Engine", int width = 1280,
                         int height = 720);
  static void Shutdown();

  // Window properties
  static int GetWidth();
  static int GetHeight();
  static SDL_Window* GetWindow();

  // stored scale factor
  static float GetDPIScale();

  // Register for resize notifications
  static void RegisterResizeCallback(std::function<void(int, int)> callback);

  // Handle window resize
  static void OnWindowResize(int width, int height);

  // BGFX platform data
  static void InitBGFXPlatformData(bgfx::Init& init);

  // Handle fullscreen
  static void ToggleFullscreen();
  static bool IsFullscreen();
  static bool SetBorderlessFullscreen(bool enable);

  // Track window state
  static SDL_Rect windowed_bounds;
  static bool minimized;
  static bool fullscreen;
  static bool quit_requested;

  static void Quit();
  static bool ShouldQuit();
};

#endif  // SDL_PLATFORM_H