#include "graphics_renderer.h"

#include <SDL.h>
#include <SDL2/SDL_ttf.h>

#include <backends/imgui_impl_sdl2.h>
#include <imgui.h>

#include <bx/math.h>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <sstream>
#include <string>

#include "engine/core/engine_globals.h"
#include "engine/core/logger.h"
#include "engine/core/view_ids.h"

#include "engine/renderer/shaders/shader_manager.h"
#include "engine/resources/shader_utils.h"

#include "platform/platform_utils.h"

GraphicsRenderer::GraphicsRenderer() {
  // Initialize SDL_ttf (SDL itself is initialized by window manager)
  if (TTF_Init() == -1) {
    Logger::getInstance().Log(
        LogLevel::Error, "TTF_Init failed: " + std::string(TTF_GetError()));
    std::exit(1);
  }

  // Get SDL window from WindowManager instead of creating one
  window_ = WindowManager::GetWindow();
  if (!window_) {
    Logger::getInstance().Log(LogLevel::Error,
                              "Failed to get window from WindowManager");
    std::exit(1);
  }

  // Set initial size from WindowManager
  // SetSize(WindowManager::GetWidth(), WindowManager::GetHeight());

  // Register for window resize notifications
  WindowManager::RegisterResizeCallback(
      [this](int width, int height) { OnResize(width, height); });

  Logger::getInstance().Log(
      LogLevel::Info, "GraphicsRenderer initialized successfully with BGFX.");
}

GraphicsRenderer::~GraphicsRenderer() {
  TTF_Quit();
}

// Initialize BGFX with platform data
bool GraphicsRenderer::InitBgfx() {
  bgfx::Init init;
  Platform::SetupBGFXPlatformData(init, window_);

  init.type = bgfx::RendererType::Count;  // Auto-select backend
  init.debug = true;
  init.profile = true;

  if (!bgfx::init(init)) {
    Logger::getInstance().Log(LogLevel::Error, "Failed to initialize BGFX!");
    return false;
  }

  // bgfx::setDebug(BGFX_DEBUG_TEXT | BGFX_DEBUG_STATS); // debug

  // Create uniforms
  u_viewPos = bgfx::createUniform("u_viewPos", bgfx::UniformType::Vec4);
  u_meshColor = bgfx::createUniform("u_meshColor", bgfx::UniformType::Vec4);

  ConfigureViews();

  auto backend = bgfx::getRendererType();
  Logger::getInstance().Log(
      LogLevel::Info, "BGFX initialized successfully! Selected backend: " +
                          std::to_string(static_cast<int>(backend)) + " (" +
                          bgfx::getRendererName(backend) + ")");
  return true;
}

std::string GraphicsRenderer::GetCurrentGameContent() {
  std::lock_guard<std::mutex> lock(canvas_mutex_);
  return current_game_content_;
}

// Handle view setup and framebuffer creation
void GraphicsRenderer::ConfigureViews() {
  // 1. Called once on init and in WindowManager::OnWindowResize()
  int fbw, fbh;
  Platform::GetDrawableSize(window_, &fbw, &fbh);

  // Create/resize off-screen FBO only
  if (fbw != last_framebuffer_width_ || fbh != last_framebuffer_height_)
    CreateFramebuffers(fbw, fbh);

  // UI background view lives in back-buffer, full window size
  bgfx::setViewClear(ViewID::UI_BACKGROUND, BGFX_CLEAR_COLOR, 0x1e1e1eff, 1.0f,
                     0);
  bgfx::setViewRect(ViewID::UI_BACKGROUND, 0, 0, fbw, fbh);
}

void GraphicsRenderer::OnResize(uint16_t w, uint16_t h) {
  last_framebuffer_width_ = 0;  // force rebuild on next PrepareFrame()
  last_framebuffer_height_ = 0;
  ConfigureViews();  // rebuild views for new window
}

// Set up per-frame and clear operations
void GraphicsRenderer::PrepareFrame() {
  const uint16_t fbw = canvasViewportW ? canvasViewportW : 1;
  const uint16_t fbh = canvasViewportH ? canvasViewportH : 1;

  // Grow-only allocation, rounded up, so drag-resizing rarely reallocates.
  // Views render into the top-left fbw x fbh sub-rect.
  constexpr int kFramebufferGranularity = 256;
  auto round_up = [](int v) {
    return ((v + kFramebufferGranularity - 1) / kFramebufferGranularity) *
           kFramebufferGranularity;
  };
  const bool too_small =
      fbw > last_framebuffer_width_ || fbh > last_framebuffer_height_;
  const bool too_big =
      fbw * 2 < last_framebuffer_width_ && fbh * 2 < last_framebuffer_height_;
  if (fbw > 1 && fbh > 1 && (too_small || too_big))
    CreateFramebuffers(static_cast<uint16_t>(round_up(fbw)),
                       static_cast<uint16_t>(round_up(fbh)));

  // Common clear for the scene_framebuffer_ (done by the first view using it)
  // GraphicsRenderer::PrepareFrame()
  bgfx::setViewClear(ViewID::SCENE_SKYBOX, BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH,
                     0x303030ff, 1.0f, 0);

  for (uint8_t vid : ViewID::kSceneViews) {
    bgfx::setViewRect(vid, 0, 0, fbw, fbh);
    bgfx::setViewFrameBuffer(vid, scene_framebuffer_);
  }

  // UI and backbuffer views must use drawable size
  // TODO: avoid global inline state and keep viewport sizes
  // in a single owner (e.g. GraphicsRenderer or WindowManager) with accessors
  int win_w, win_h;
  Platform::GetDrawableSize(window_, &win_w, &win_h);

  bgfx::setViewClear(ViewID::UI_BACKGROUND, BGFX_CLEAR_COLOR, 0x1e1e1eff, 1.0f,
                     0);
  bgfx::setViewRect(ViewID::UI_BACKGROUND, 0, 0, static_cast<uint16_t>(win_w),
                    static_cast<uint16_t>(win_h));
  bgfx::setViewFrameBuffer(ViewID::UI_BACKGROUND, BGFX_INVALID_HANDLE);

  // Separate reflection pass framebuffer
  if (bgfx::isValid(reflection_fb_)) {
    bgfx::setViewRect(ViewID::REFLECTION_PASS, 0, 0, fbw, fbh);
    bgfx::setViewFrameBuffer(ViewID::REFLECTION_PASS, reflection_fb_);

    // Optional: clear reflection buffer
    bgfx::setViewClear(ViewID::REFLECTION_PASS,
                       BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH, 0x000000ff, 1.0f,
                       0);
  }

  if (!bgfx::isValid(scene_framebuffer_)) {
    Logger::getInstance().Log(LogLevel::Error,
                              "Scene framebuffer invalid in PrepareFrame!");
  }
}

// Framebuffer initialization and handling
// Logic includes double buffering
void GraphicsRenderer::CreateFramebuffers(uint16_t w, uint16_t h) {
  if (w == 0 || h == 0) {
    Logger::getInstance().Log(
        LogLevel::Error,
        "CreateFramebuffers called with zero dimensions, skipping.");
    return;
  }

  char log_buffer[512];  // Shared buffer for log messages

  snprintf(log_buffer, sizeof(log_buffer),
           "CreateFramebuffers: Attempting with w=%u, h=%u",
           static_cast<unsigned int>(w), static_cast<unsigned int>(h));
  Logger::getInstance().Log(LogLevel::Debug, log_buffer);

  // Create new textures
  bgfx::TextureHandle new_color_th = bgfx::createTexture2D(
      w, h, false, 1, bgfx::TextureFormat::BGRA8,
      BGFX_TEXTURE_RT | BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP);
  snprintf(
      log_buffer, sizeof(log_buffer),
      "CreateFramebuffers: new_color_th created, handle: %s",
      (bgfx::isValid(new_color_th) ? std::to_string(new_color_th.idx).c_str()
                                   : "INVALID"));
  Logger::getInstance().Log(LogLevel::Debug, log_buffer);

  bgfx::TextureHandle new_depth_th =
      bgfx::createTexture2D(w, h, false, 1, bgfx::TextureFormat::D24S8,
                            BGFX_TEXTURE_RT | BGFX_TEXTURE_RT_WRITE_ONLY);
  snprintf(
      log_buffer, sizeof(log_buffer),
      "CreateFramebuffers: new_depth_th created, handle: %s",
      (bgfx::isValid(new_depth_th) ? std::to_string(new_depth_th.idx).c_str()
                                   : "INVALID"));
  Logger::getInstance().Log(LogLevel::Debug, log_buffer);

  // Reflection framebuffer
  if (bgfx::isValid(reflection_fb_)) {
    bgfx::destroy(reflection_fb_);
    reflection_fb_ = BGFX_INVALID_HANDLE;
  }
  reflection_color_tex_ = BGFX_INVALID_HANDLE;

  // Create a color-only reflection texture
  reflection_color_tex_ = bgfx::createTexture2D(
      w, h, false, 1, bgfx::TextureFormat::BGRA8,
      BGFX_TEXTURE_RT | BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP);

  reflection_fb_ = bgfx::createFrameBuffer(1, &reflection_color_tex_, true);

  if (!bgfx::isValid(new_color_th) || !bgfx::isValid(new_depth_th)) {
    snprintf(log_buffer, sizeof(log_buffer),
             "CreateFramebuffers: Texture creation failed. new_color valid: "
             "%s, new_depth valid: %s",
             bgfx::isValid(new_color_th) ? "true" : "false",
             bgfx::isValid(new_depth_th) ? "true" : "false");
    Logger::getInstance().Log(LogLevel::Error, log_buffer);

    if (bgfx::isValid(new_color_th))
      bgfx::destroy(new_color_th);
    if (bgfx::isValid(new_depth_th))
      bgfx::destroy(new_depth_th);
    return;
  }

  const bgfx::TextureHandle attachments[2] = {new_color_th, new_depth_th};
  // Create FBO, and tell it to destroy these new textures if the FBO itself is
  // destroyed.
  bgfx::FrameBufferHandle new_fbh =
      bgfx::createFrameBuffer(BX_COUNTOF(attachments), attachments,
                              true);  // true = destroy attachments with FBO

  if (bgfx::isValid(new_fbh) &&
      new_fbh.idx !=
          0) {  // Explicitly check for non-zero handle for off-screen FBO
    snprintf(log_buffer, sizeof(log_buffer),
             "SUCCESS: Created new_framebuffer. Handle: %u, ColorTex: %u, "
             "DepthTex: %u",
             static_cast<unsigned int>(new_fbh.idx),
             static_cast<unsigned int>(new_color_th.idx),
             static_cast<unsigned int>(new_depth_th.idx));
    Logger::getInstance().Log(LogLevel::Info, log_buffer);

    if (bgfx::isValid(scene_framebuffer_)) {
      snprintf(
          log_buffer, sizeof(log_buffer),
          "Destroying old scene_framebuffer_ (handle %u) and its textures.",
          scene_framebuffer_.idx == bgfx::kInvalidHandle
              ? 9999
              : static_cast<unsigned int>(scene_framebuffer_.idx));
      Logger::getInstance().Log(LogLevel::Debug, log_buffer);
      bgfx::destroy(scene_framebuffer_);  // This will also destroy its
                                          // attachments if created with 'true'
    }

    scene_framebuffer_ = new_fbh;
    scene_color_texture_ = new_color_th;
    scene_depth_texture_ = new_depth_th;
    scene_tex_id_ = (ImTextureID)(uintptr_t)scene_color_texture_.idx;

    last_framebuffer_width_ = w;
    last_framebuffer_height_ = h;
  } else {
    snprintf(log_buffer, sizeof(log_buffer),
             "FAILURE: bgfx::createFrameBuffer FAILED or returned handle 0. "
             "New FBH idx: %u",
             new_fbh.idx == bgfx::kInvalidHandle
                 ? 9999
                 : static_cast<unsigned int>(new_fbh.idx));
    Logger::getInstance().Log(LogLevel::Error, log_buffer);

    // If FBO creation failed, destroy the textures we just made for it, as they
    // are not attached to a valid new FBO.
    if (bgfx::isValid(new_color_th))
      bgfx::destroy(new_color_th);
    if (bgfx::isValid(new_depth_th))
      bgfx::destroy(new_depth_th);
  }
}

void GraphicsRenderer::Render() {

  bgfx::touch(ViewID::UI_BACKGROUND);  // dummy background view
  // bgfx::touch(ViewID::UI);             // ImGui rendering into UI view
  // bgfx::touch(ViewID::SCENE);

  redrawn();
  frame_count_++;
}

void GraphicsRenderer::ProcessEvents(bool& quit) {
  SDL_Event event;
  while (SDL_PollEvent(&event)) {
    if (event.type == SDL_QUIT) {
      quit = true;
    }
  }

  // window size check
  int w, h;
  SDL_GetWindowSize(WindowManager::GetWindow(), &w, &h);
  if (w != WindowManager::GetWidth() || h != WindowManager::GetHeight()) {
    WindowManager::OnWindowResize(w, h);  // Will trigger the BGFX resize logic
  }
}

void GraphicsRenderer::Destroy() {
  Logger::getInstance().Log(LogLevel::Info, "Shutting down GraphicsRenderer");

  // Destroy the framebuffer
  if (bgfx::isValid(scene_framebuffer_)) {
    Logger::getInstance().Log(LogLevel::Debug,
                              "Destroying old scene_framebuffer_ (handle " +
                                  std::to_string(scene_framebuffer_.idx) +
                                  ") and its textures.");
    bgfx::destroy(scene_framebuffer_);
    Logger::getInstance().Log(LogLevel::Debug,
                              "Old framebuffer destroyed successfully.");

    scene_framebuffer_ = BGFX_INVALID_HANDLE;
  }
  scene_color_texture_ = BGFX_INVALID_HANDLE;
  scene_depth_texture_ = BGFX_INVALID_HANDLE;

  // Destroy reflection framebuffer and its textures
  if (bgfx::isValid(reflection_fb_)) {
    bgfx::destroy(reflection_fb_);
    reflection_fb_ = BGFX_INVALID_HANDLE;
  }
  reflection_color_tex_ = BGFX_INVALID_HANDLE;

  // Destroy uniforms
  if (bgfx::isValid(u_viewPos))
    bgfx::destroy(u_viewPos);

  if (bgfx::isValid(u_meshColor))
    bgfx::destroy(u_meshColor);

  // 3. Clear ImGui texture ID before shutdown
  scene_tex_id_ = 0;

  // 4. Final frame flush (optional but safe)
  bgfx::frame();

  // 5. Shutdown BGFX
  // bgfx::shutdown();

  Logger::getInstance().Log(LogLevel::Info,
                            "GraphicsRenderer shutddown complete");
}

void GraphicsRenderer::BeginImGuiFrame() {}

void GraphicsRenderer::EndImGuiFrame() {}

// function to initialize shaders
void GraphicsRenderer::CreateShaders() {

  // Fullscreen quad vertices
  struct FSVertex {
    float x, y, z;
    float u, v;
  };

  // use triangle strip topology
  static FSVertex quad_vertices[] = {
      // Triangle strip order: TL -> TR -> BL -> BR
      {-1.0f, 1.0f, 0.0f, 0.0f, 1.0f},   // Top-left
      {1.0f, 1.0f, 0.0f, 1.0f, 1.0f},    // Top-right
      {-1.0f, -1.0f, 0.0f, 0.0f, 0.0f},  // Bottom-left
      {1.0f, -1.0f, 0.0f, 1.0f, 0.0f},   // Bottom-right
  };

  // Submit as triangle strip
  bgfx::setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A | BGFX_STATE_MSAA |
                 BGFX_STATE_PT_TRISTRIP);
}

// Getters for the SDL window and renderer.
SDL_Window* GraphicsRenderer::GetWindow() const {
  return window_;
}

void GraphicsRenderer::UpdateCanvasSize(uint16_t w, uint16_t h) {
  // Only update if significantly different or enough frames have passed since
  // last resize
  bool significant_change = (abs((int)w - (int)canvasViewportW) > 5 ||
                             abs((int)h - (int)canvasViewportH) > 5);

  if (significant_change ||
      (frame_count_ - last_resize_frame_ > resize_throttle_)) {
    canvasViewportW = w;
    canvasViewportH = h;
    last_resize_frame_ = frame_count_;
  }
}

// UNIFORM METHODS
bgfx::UniformHandle GraphicsRenderer::CreateUniform(
    const std::string& name, bgfx::UniformType::Enum type) {

  // Check if already exists
  auto it = uniform_registry_.find(name);
  if (it != uniform_registry_.end()) {
    Logger::getInstance().Log(
        LogLevel::Warning,
        "Uniform '" + name + "' already exists, returning existing handle");
    return it->second;
  }

  // Create uniform via BGFX
  bgfx::UniformHandle handle = bgfx::createUniform(name.c_str(), type);

  // Track uniform
  if (bgfx::isValid(handle)) {
    uniform_registry_[name] = handle;
    Logger::getInstance().Log(
        LogLevel::Debug, "Created uniform: " + name +
                             " (handle: " + std::to_string(handle.idx) + ")");
  } else {
    Logger::getInstance().Log(LogLevel::Error,
                              "Failed to create uniform: " + name);
  }

  return handle;
}

void GraphicsRenderer::DestroyUniform(bgfx::UniformHandle handle) {
  if (!bgfx::isValid(handle)) {
    Logger::getInstance().Log(LogLevel::Warning,
                              "Attempted to destroy invalid uniform handle");
    return;
  }

  // Remove from registry
  for (auto it = uniform_registry_.begin(); it != uniform_registry_.end();
       ++it) {
    if (it->second.idx == handle.idx) {
      Logger::getInstance().Log(LogLevel::Debug,
                                "Destroying uniform: " + it->first);
      uniform_registry_.erase(it);
      break;
    }
  }

  // Destroy via BGFX
  bgfx::destroy(handle);
}

void GraphicsRenderer::ValidateUniforms() {
  Logger::getInstance().Log(LogLevel::Info, "-- Uniform Validation --");
  Logger::getInstance().Log(
      LogLevel::Info,
      "Total tracked uniforms: " + std::to_string(uniform_registry_.size()));

  // Check for invalid handles
  int invalid_count = 0;
  for (const auto& [name, handle] : uniform_registry_) {
    if (!bgfx::isValid(handle)) {
      Logger::getInstance().Log(LogLevel::Warning, "Invalid uniform: " + name);
      invalid_count++;
    }
  }

  if (invalid_count > 0) {
    Logger::getInstance().Log(
        LogLevel::Warning,
        "Found " + std::to_string(invalid_count) + " invalid uniforms!");
  } else {
    Logger::getInstance().Log(LogLevel::Info, "All uniforms valid");
  }

  Logger::getInstance().Log(LogLevel::Info, "-------------------------");
}
