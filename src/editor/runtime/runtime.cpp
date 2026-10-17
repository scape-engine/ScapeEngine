/**************************************************************************
 * Runtime
 * -------
 * Central orchestrator for engine-wide systems
 * Initializes entire engine and runs the main loop
 *
 * Owns / lifetime-managed:
 *  - EditorBase (g_editor)
 *  - GameManager (g_game_manager)
 *  - ProjectManager (g_project_manager)
 *  - Rendering pipelines (g_scene_view_pipeline)
 *  - Global resources (skybox, scene gizmos, shadowMap, etc)
 *
 * Initializes / coordinates:
 *  - EngineContext subsystems (renderer, shaders, input)
 *  - Asset and component registries
 *  - Game execution thread
 *  - Signal connections between subsystems
 *
 * Lifecycle:
 *  - START_LOOP(): Creates context, loads assets, launches editor and game
 *  - _NextFrame(): Renders scene, processes UI events (incomplete)
 *  - TERMINATE(): Shuts down game thread, editor, pipelines, and context
 *
 * Accessors:
 *  - Editor(), Renderer(), Game(): Access to major subsystems
 *  - InputDevice(), Window(), Shader(): Convenience wrappers to EngineContext
 *  - Project(): Access to project management
 *  - sceneViewPipeline(): Access to editor view rendering
 *
 * TODO:
 *  - Implement scene gizmos
 *  - Create default assets and settings
 *  - Complete _NextFrame() and main loop implementation
 *  - Proper audio context (currently using SoundManager directly)
 *  - Remove lighting from runtime after materials is implemented
 **************************************************************************/

#include "runtime.h"

#include "editor/events.h"
#include "editor/registry/asset_registry.h"
#include "editor/registry/component_registry.h"

#include "engine/audio/sound_manager.h"  // TODO: remove later
#include "engine/context/engine_context.h"
#include "engine/core/engine_globals.h"
#include "engine/core/logger.h"
#include "engine/ecs/ecs_collection.h"
#include "engine/renderer/icons/icon_loader.h"
#include "engine/renderer/lighting/light.h"
#include "engine/renderer/shadows/shadow_map.h"
#include "engine/renderer/skybox/skybox.h"
#include "engine/scene/scene_manager.h"
#include "engine/scene/scene_template.h"
#include "engine/time/time.h"

#include "platform/paths.h"

#include "game/game_test.h"

// ------------------ single-instance state -----------------
namespace Runtime {

// ---------- globals ----------
static std::thread g_game_thread;

std::unique_ptr<EditorBase> g_editor;
std::unique_ptr<GameManager> g_game_manager;

std::unique_ptr<FrameGraph> g_frame_graph;

RendererBase* g_renderer = nullptr;
ShaderManager* g_shader_manager = nullptr;
Input* g_input = nullptr;
WindowManager* g_window_manager = nullptr;

// Project manager
ProjectManager g_project_manager;

// Pipelines
SceneViewPipeline g_scene_view_pipeline;
PreviewPipeline g_preview_pipeline;
// GameViewPipeline g_game_view_pipeline;

// Scene gizmos
IMGizmo g_scene_gizmos;

// default assets
Skybox g_default_skybox;

// shadow
ShadowMap g_main_shadow_map;

// scene manager
SceneManager g_scene_manager;

EditorState g_editor_state;

// TODO: default settings

// TODO: global game state?

static void _LoadDependencies() {
  // TODO: resource manager
  ResourceManager& resource = EngineContext::resourceManager();

  // TODO: load shaders
  // ShaderPool::loadAllSync("./shaders/materials");
  // ShaderPool::loadAllSync("./shaders/postprocessing");
  // ShaderPool::loadAllSync("./shaders/gizmo");
  // ShaderPool::loadAllSync("./shaders/passes");

  // TODO: create default textures

  // load icons
  IconLoader::CreatePlaceholderIcon(
      Platform::ResolvePath("resources/icons/fallback/fallback.png"));
  IconLoader::LoadIcons(Platform::ResolvePath("resources/icons/shared"));
  IconLoader::LoadIconsAsync(Platform::ResolvePath("resources/icons/assets"));
  IconLoader::LoadIconsAsync(
      Platform::ResolvePath("resources/icons/components"));
  IconLoader::LoadIconsAsync(Platform::ResolvePath("resources/icons/scene"));

  // Create default skybox
  g_default_skybox.Create(g_shader_manager);

  // Create default texture
}

static void _CreateResources() {

  // TODO: create pipelines here
  g_scene_view_pipeline.Create();
  // g_game_view_pipeline.Create();
  g_preview_pipeline.Create();

  // Initialize FrameGraph with current viewport dimensions
  g_frame_graph->Rebuild(canvasViewportW, canvasViewportH);

  // setup scene gizmos
  g_scene_gizmos.Create();

  // Init lighting
  // Uniform creation should be moved to GraphicsRenderer or ShaderManager!
  Light::Create();  // ! remove once material system is done

  // Setup shadows
  g_main_shadow_map.Create();

  // TODO: Create main shadow disk and main shadow map here ?
  // TODO: load global shadows here?
}

static void _CreateEngineContext() {
  if (!EngineContext::Init()) {
    Logger::getInstance().Log(LogLevel::Error, "EngineContext init failed");
    std::exit(1);
  }

  // cache subsystem references
  g_window_manager = &EngineContext::Window();
  g_renderer = &EngineContext::Renderer();
  g_shader_manager = &EngineContext::Shader();
  g_input = &EngineContext::InputDevice();

  g_frame_graph = std::make_unique<FrameGraph>(*g_renderer);

  // Register for window resize notifications
  WindowManager::RegisterResizeCallback([](int width, int height) {
    // Rebuild frame graph when window size changes
    g_frame_graph->Rebuild(width, height);
  });

  // Set audio
  // TODO: remove once we have a proper audio context
  // SoundManager::Instance().setAmbientVolume(0.7f);
  // SoundManager::Instance().startAmbient();
}

static void _LaunchEditor() {

  // TODO: add welcome console message here

  // Create Asset registry
  AssetRegistry::Create();

  // Create component registry
  ComponentRegistry::Create();

  // initialize editor layer
  // TODO: should be EditorUI::Setup();
  g_editor = std::make_unique<EditorUI>(g_renderer);
  Logger::getInstance().Log(LogLevel::Info, "Editor initialized");

  // TODO: Show editor window here?
  // WindowContext::SetResizeable(true);
  // WindowContext::MaximizeWindow();
  // WindowContext::SetVisible(true);

  // Load example project
  std::filesystem::path project_path =
      std::filesystem::current_path() / "examples" / "example-project";
  std::filesystem::create_directories(project_path);
  g_project_manager.Load(project_path);

  // Load default scene
  g_scene_manager.LoadScene(std::make_unique<SceneTemplate>());

  // TODO: show welcome panel here
}

static void _NextFrame() {

  // Start engine context frame
  EngineContext::NextFrame();

  // Update resources
  UpdateGlobalResources();

  // Set global resources in frame graph
  g_frame_graph->SetGlobalResources(BuildGlobalResources());

  // Execute all passes from all pipelines
  g_frame_graph->Render();

  // Render next frame (not needed anymore)
  // g_scene_view_pipeline.Render();
  // g_preview_pipeline.Render();
  // g_game_view_pipeline.Render();

  // Render editor
  g_project_manager.PollEvents();
  // TODO: start profiler for ui
  // TODO: stop profiler for ui

  // TODO: Stop EngineContext::EndFrame(); ???
}

// TODO: create a dedicated eventbus for gameplay events?
static void _ConnectSignals() {

  // connect editor event handles
  EditorEvents::game_start_pressed.connect(
      [&] { g_game_manager->StartGame(); });
  EditorEvents::game_end_pressed.connect([&] { g_game_manager->EndGame(); });
  EditorEvents::editor_exit_pressed.connect(
      [&] { g_game_manager->Terminate(); });

  EditorEvents::game_inputed.connect([&](InputEvent event) {
    g_input->ForwardInputEvent(event, g_game_manager->GetFrameCount());
  });

  g_renderer->redrawn.connect([&] { g_editor->RequestUpdate(); });
}

void UpdateGlobalResources() {

  // Get delta time from time
  float dt = Time::Deltaf();

  g_default_skybox.Update(dt);
  // TODO: g_terrain.Update(dt); g_water.Update(dt);
}

GlobalResources BuildGlobalResources() {
  GlobalResources r;
  r.skybox = &g_default_skybox;
  // TODO: r.terrain = &g_terrain; r.water = &g_water;
  return r;
}

int START_LOOP() {
  Logger::getInstance().Log(LogLevel::Info, "Runtime::StartLoop");
  _CreateEngineContext();
  _LoadDependencies();
  _CreateResources();
  _LaunchEditor();

  // Setup game (manager)
  g_game_manager = std::make_unique<GameManager>(std::make_unique<GameTest>());
  Logger::getInstance().Log(LogLevel::Info, "GameManager initialized");

  _ConnectSignals();

  // Setup game
  g_game_thread = std::thread([] {
    if (g_game_manager)
      g_game_manager->Run();
  });
  Logger::getInstance().Log(LogLevel::Info,
                            "Game initialized in its own thread");

  // Run the editor
  g_editor->Run();  // TODO: this blocks global resources, needs fix!
  Logger::getInstance().Log(LogLevel::Info, "Editor initialized and running");

  // TODO: remove once _NextFrame() is used
  // g_scene_view_pipeline.Render();

  // Main loop
  // TODO: EditorUI::Run() needs refactoring
  while (EngineContext::Running())
    _NextFrame();

  // TODO: remove once _NextFrame() is used
  // g_project_manager.PollEvents();

  return TERMINATE();
}

int TERMINATE() {

  // Terminate game thread
  if (g_game_manager) {
    g_game_manager->Terminate();
  }

  // join game thread
  if (g_game_thread.joinable())
    g_game_thread.join();

  // Stop game logic
  if (g_game_manager) {
    g_game_manager->Destroy();
    g_game_manager.reset();
  }

  // Destroy editor subsystems
  if (g_editor) {
    g_editor->Destroy();
    g_editor.reset();
  }

  // Stop pipelines
  g_scene_view_pipeline.Destroy();
  g_preview_pipeline.Destroy();

  g_frame_graph.reset();

  g_main_shadow_map.Destroy();

  g_default_skybox.Destroy();

  Light::Destroy();

  EngineContext::Destroy();

  Logger::getInstance().Log(LogLevel::Info, "Runtime::Terminate");

  // Exit application
  std::exit(0);
  return 0;
}

EditorBase* Editor() {
  return g_editor.get();
}
RendererBase* Renderer() {
  return g_renderer;
}
GameManager* Game() {
  return g_game_manager.get();
}
Input* InputDevice() {
  return g_input;
}
WindowManager* Window() {
  return g_window_manager;
}
ShaderManager* Shader() {
  return g_shader_manager;
}
ProjectManager& Project() {
  return g_project_manager;
}

IMGizmo& SceneGizmos() {
  return g_scene_gizmos;
}

ShadowMap& MainShadowMap() {
  return g_main_shadow_map;
}

SceneViewPipeline& GetSceneViewPipeline() {
  return g_scene_view_pipeline;
}

PreviewPipeline& GetPreviewPipeline() {
  return g_preview_pipeline;
}

FrameGraph& GetFrameGraph() {
  return *g_frame_graph;
}

SceneManager& Scene() {
  return g_scene_manager;
}

EditorState& State() {
  return g_editor_state;
}

}  // namespace Runtime