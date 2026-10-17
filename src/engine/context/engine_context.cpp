/**************************************************************************
 * EngineContext
 * -------------
 * Central access point for low-level engine systems and services.
 *
 * Owns / lifetime-managed:
 *  - GraphicsRenderer (graphics_renderer_instance_)
 *  - ShaderManager (shader_manager_instance_)
 *  - Input (input_device_instance_)
 *  - ResourceManager (resource_manager_)
 *  - SubsystemList hot-reload registry (dynamic_registry)
 *
 * Initializes / coordinates (not owned):
 *  - WindowManager (Initialize, ShouldQuit)
 *  - ECS (entt::locator<ECS>::emplace)
 *  - SoundManager (Instance().init / Instance().terminate)
 *
 * Lifecycle:
 *  - Init(): Creates subsystems, initializes WindowManager, BGFX, ECS, Audio
 *  - Running(): Updates resources, ticks dynamic registry, polls window state
 *  - Destroy(): Shuts down subsystems in reverse dependency order
 *
 * Accessors:
 *  - Window(): Access to window and display operations
 *  - Renderer(): Access to rendering pipeline
 *  - InputDevice(): Access to input handling
 *  - Shader(): Access to shader management
 *  - resourceManager(): Access to resource
 **************************************************************************/

#include "engine_context.h"
#include "engine/audio/sound_manager.h"
#include "engine/context/subsystem_list.h"
#include "engine/core/logger.h"
#include "engine/diagnostics/frame_stats.h"
#include "engine/ecs/world.h"
#include "engine/pcg/pcg_engine.h"
#include "engine/renderer/graphics_renderer.h"
#include "engine/renderer/shaders/shader_manager.h"
#include "platform/input/input.h"
#include "platform/window_manager.h"

namespace EngineContext {

// ------------------------------------------------------------------
//  Process-wide singletons
// ------------------------------------------------------------------
static std::unique_ptr<GraphicsRenderer> graphics_renderer_instance_;
static std::unique_ptr<ShaderManager> shader_manager_instance_;
static std::unique_ptr<Input> input_device_instance_;
static std::unique_ptr<ResourceManager> resource_manager_;
static std::unique_ptr<PCGEngine> pcg_engine_;

// ------------------------------------------------------------------
//  Hot-reload registry (not used yet)
// ------------------------------------------------------------------
static SubsystemList dynamic_registry;

// ------------------------------------------------------------------
//  Init / Tick / Shutdown
// ------------------------------------------------------------------
bool Init() {
  if (!WindowManager::Initialize())
    return false;

  graphics_renderer_instance_ = std::make_unique<GraphicsRenderer>();
  if (!graphics_renderer_instance_->InitBgfx())
    return false;

  shader_manager_instance_ = std::make_unique<ShaderManager>();
  shader_manager_instance_->Init();

  input_device_instance_ = std::make_unique<Input>();

  resource_manager_ = std::make_unique<ResourceManager>();

  pcg_engine_ = std::make_unique<PCGEngine>();
  pcg_engine_->Create();

  // Initialize ECS singleton
  entt::locator<ECS>::emplace();

  // Initialize audio
  // TODO: rename to AudioContext
  if (!SoundManager::Instance().init()) {
    Logger::getInstance().Log(LogLevel::Error,
                              "Failed to initialize SoundManager");
    return false;
  }

  // TODO: for future subsystems only (hot-reload, live editing, etc)
  return dynamic_registry.InitAll();
}

bool Running() {
  return !WindowManager::ShouldQuit();
}

void NextFrame() {

  // Calculate delta time
  // TODO: move time stuff to profiler, we just keep the call
  using clock = std::chrono::steady_clock;
  static auto previous = clock::now();
  auto now = clock::now();
  double delta_time = std::chrono::duration<double>(now - previous).count();
  previous = now;

  Time::Step(delta_time);

  if (resource_manager_) {
    resource_manager_->UpdateContext();
  }

  if (pcg_engine_) {
    pcg_engine_->ProcessQueued();
  }

  dynamic_registry.TickAll(delta_time);

  Diagnostics::UpdateStats();
}

void Destroy() {
  dynamic_registry.ShutdownAll();

  pcg_engine_.reset();
  resource_manager_.reset();
  input_device_instance_.reset();
  shader_manager_instance_.reset();

  // Release renderer GPU handles
  // ! No bgfx::shutdown() yet: static objects holding GPU handles (e.g. the
  // ! asset_cache in game_canvas.h, icon textures) are destroyed during
  // ! std::exit, after this, and would call bgfx::destroy on a dead context.
  // ! Move those into owned objects first, then add bgfx::shutdown() here.
  if (graphics_renderer_instance_)
    graphics_renderer_instance_->Destroy();
  graphics_renderer_instance_.reset();

  SoundManager::Instance().terminate();
}

// ------------------------------------------------------------------
//  Accessors
// ------------------------------------------------------------------
WindowManager& Window() {
  return WindowManager::getInstance();
}

RendererBase& Renderer() {
  return *graphics_renderer_instance_;
}

Input& InputDevice() {
  return *input_device_instance_;
}

ShaderManager& Shader() {
  return *shader_manager_instance_;
}

// TODO: fix name
ResourceManager& resourceManager() {
  return *resource_manager_;
}

PCGEngine& PCG() {
  return *pcg_engine_;
}

}  // namespace EngineContext