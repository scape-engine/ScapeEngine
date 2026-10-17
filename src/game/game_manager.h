// ---------------------------------------------------------------------------
// game_manager.h
// Oversees the main game loop and game state transitions.
// Owns core game logic and delegates scene control to SceneManager.
// TODO: Change name to GameLoopManager or something more appropriate.
// ---------------------------------------------------------------------------

#ifndef GAME_MANAGER_H
#define GAME_MANAGER_H

#include <atomic>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <thread>

#include "game_base.h"

#include "engine/scene/scene_manager.h"

class GameManager {
  enum GameState { ENDED, STARTING, RUNNING, ENDING, PAUSING, PAUSED };

private:
  std::unique_ptr<GameBase> core_;
  std::unique_ptr<SceneManager> scene_manager_;

  // Shared between the editor thread and the game thread
  std::atomic<GameState> gamestate_;
  std::thread game_thread_;
  std::atomic<uint64_t> frame_count_;
  std::atomic<bool> is_terminating_ = false;

  std::mutex state_mutex_;
  std::condition_variable condition_;

  GameManager() = delete;
  void LoadScene(std::unique_ptr<Scene> scene);

  bool is_running_ = false;

public:
  void Render();
  void StartGame();
  void EndGame();
  void Run();
  void Terminate();
  void Destroy();
  uint64_t GetFrameCount();
  GameBase* GetGame() const { return core_.get(); }
  GameManager(std::unique_ptr<GameBase>&& core);
};

#endif  // GAME_MANAGER_H
