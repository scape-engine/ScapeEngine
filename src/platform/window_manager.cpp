#include <SDL_hints.h>
#include <SDL_syswm.h>

#include <bgfx/bgfx.h>

#include "engine/core/logger.h"  // ! remove engine dependency
#include "imgui.h"

#include "platform/platform_utils.h"
#include "window_manager.h"

// Track window state
bool WindowManager::fullscreen = false;
SDL_Rect WindowManager::windowed_bounds = {};
bool WindowManager::minimized = false;
bool WindowManager::quit_requested = false;

bool WindowManager::Initialize(const char* title, int width, int height) {
  WindowManager& instance = getInstance();
  instance.width_ = width;
  instance.height_ = height;

  Logger::getInstance().Log(LogLevel::Debug,
                            "Calling SDL_Init");  // debug - remove later

  if (SDL_Init(SDL_INIT_VIDEO) < 0) {
    Logger::getInstance().Log(
        LogLevel::Error, std::string("SDL_Init failed: ") + SDL_GetError());
    return false;
  }

  instance.window_ = SDL_CreateWindow(
      title, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, width, height,
      SDL_WINDOW_SHOWN | SDL_WINDOW_ALLOW_HIGHDPI | SDL_WINDOW_RESIZABLE);
  if (!instance.window_) {
    Logger::getInstance().Log(
        LogLevel::Error,
        std::string("SDL_CreateWindow failed: ") + SDL_GetError());
    SDL_Quit();
    return false;
  }

  SDL_RaiseWindow(instance.window_);
  Platform::LockMinSize(instance.window_, width, height);

  // ------------------------------------------------
  //  Get display scale factor for Retina support
  // ------------------------------------------------
  instance.dpi_scale_ = Platform::GetDPIScale(instance.window_);

  Platform::DisableTextInput();

  Logger::getInstance().Log(
      LogLevel::Info, "WindowManager initialized with size: " +
                          std::to_string(width) + "x" + std::to_string(height));
  return true;
}

// TODO: check if it ever gets called somewhere
void WindowManager::Shutdown() {
  WindowManager& inst = getInstance();

  if (inst.window_) {  // destroy window exactly once
    SDL_DestroyWindow(inst.window_);
    inst.window_ = nullptr;
  }

  SDL_QuitSubSystem(SDL_INIT_VIDEO);
  SDL_Quit();
}

WindowManager::~WindowManager() {
  Shutdown();
}

int WindowManager::GetWidth() {
  return getInstance().width_;
}

int WindowManager::GetHeight() {
  return getInstance().height_;
}

SDL_Window* WindowManager::GetWindow() {
  return getInstance().window_;
}

float WindowManager::GetDPIScale() {
  return getInstance().dpi_scale_;
}

void WindowManager::RegisterResizeCallback(
    std::function<void(int, int)> callback) {
  getInstance().resize_callbacks_.push_back(callback);
}

void WindowManager::OnWindowResize(int width, int height) {
  WindowManager& instance = getInstance();
  instance.width_ = width;
  instance.height_ = height;

  if (fullscreen || minimized)  // ignore resize on fullscreen or minimized
    return;

  if (!Platform::InFullscreenSpace(instance.window_))
    Platform::LockMinSize(instance.window_, instance.width_, instance.height_);
  else
    Platform::RestoreMinSize(instance.window_);

  Platform::RefreshFramebufferSize(instance.window_);  // rebuild swap-chain

  Logger::getInstance().Log(
      LogLevel::Debug, "Window resized to: " + std::to_string(width) + "x" +
                           std::to_string(height));

  // Notify all registered callbacks
  for (auto& callback : instance.resize_callbacks_) {
    callback(width, height);
  }
}

bool WindowManager::SetFullscreen(bool enable) {
  Uint32 target = enable ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0;
  if (SDL_SetWindowFullscreen(window_, target) != 0)  // ⚠ check error
  {
    Logger::getInstance().Log(LogLevel::Error, SDL_GetError());
    return false;
  }
  return true;
}

bool WindowManager::SetBorderlessFullscreen(bool enable) {
  WindowManager& instance = getInstance();

  if (enable == Platform::IsBorderlessFullscreen(instance.window_))
    return true;

  Platform::ToggleBorderlessFullscreen(instance.window_, enable);
  fullscreen = enable;
  return true;
}

void WindowManager::ToggleFullscreen() {
  WindowManager& inst = getInstance();
  fullscreen = !fullscreen;

  if (fullscreen) {  // ---> FULLSCREEN
    SDL_GetWindowPosition(inst.window_, &windowed_bounds.x, &windowed_bounds.y);
    SDL_GetWindowSize(inst.window_, &windowed_bounds.w, &windowed_bounds.h);
    SDL_SetWindowFullscreen(inst.window_, SDL_WINDOW_FULLSCREEN_DESKTOP);
  } else {  // ---> WINDOWED
    SDL_SetWindowFullscreen(inst.window_, 0);
    SDL_SetWindowBordered(inst.window_, SDL_TRUE);
    SDL_SetWindowPosition(inst.window_, windowed_bounds.x, windowed_bounds.y);
    SDL_SetWindowSize(inst.window_, windowed_bounds.w, windowed_bounds.h);
  }

  // ---- NEW: refresh cached size, propagate, reset swap-chain -----------
  int logicalW, logicalH;
  SDL_GetWindowSize(inst.window_, &logicalW, &logicalH);
  inst.width_ = logicalW;
  inst.height_ = logicalH;

  // Graph­icsRenderer’s resize callback calls SetSize() → framebuffer rebuild
  for (auto& cb : inst.resize_callbacks_)
    cb(logicalW, logicalH);

  Platform::RefreshFramebufferSize(inst.window_);
}

bool WindowManager::IsFullscreen() {
  return fullscreen;
}

void WindowManager::Quit() {
  quit_requested = true;
}

bool WindowManager::ShouldQuit() {
  return quit_requested;
}

void WindowManager::InitBGFXPlatformData(bgfx::Init& init) {
  Platform::SetupBGFXPlatformData(init, getInstance().window_);
}
