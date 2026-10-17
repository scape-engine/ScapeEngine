// ---------------------------------------------------------------------------
// game_manager.cpp
// Implements the GameManager, which handles the main thread loop,
// game state transitions, and delegates scene updates/rendering to
// SceneManager.
// ---------------------------------------------------------------------------

#include "game_manager.h"

#include <chrono>
#include <thread>

#include "engine/core/logger.h"
#include "game_base.h"

#include "engine/scene/scene_manager.h"

GameManager::GameManager(std::unique_ptr<GameBase>&& core)
    : gamestate_(GameState::ENDED),
      core_(std::move(core)),
      scene_manager_(std::make_unique<SceneManager>()),
      frame_count_(0) {}

void GameManager::StartGame() {
  Logger::getInstance().Log(LogLevel::Info, "Game manager start game");

  if (core_) {
    core_->Init();
    Logger::getInstance().Log(LogLevel::Info, "Game initialized");
  }

  {
    std::lock_guard<std::mutex> lock(state_mutex_);
    gamestate_ = GameState::STARTING;
  }
  condition_.notify_one();
}

void GameManager::LoadScene(std::unique_ptr<Scene> scene) {
  if (scene_manager_) {
    scene_manager_->LoadScene(std::move(scene));
    Logger::getInstance().Log(LogLevel::Info, "Scene loaded");
  }
}

void GameManager::EndGame() {
  Logger::getInstance().Log(LogLevel::Info, "Game manager end game");
  std::lock_guard<std::mutex> lock(state_mutex_);
  gamestate_ = GameState::ENDING;
}

void GameManager::Terminate() {
  Logger::getInstance().Log(LogLevel::Info, "Game manager terminating");
  {
    std::lock_guard<std::mutex> lock(state_mutex_);
    is_terminating_ = true;
  }
  condition_.notify_one();
}

void GameManager::Run() {
  Logger::getInstance().Log(LogLevel::Info, "Game thread started");

  while (true) {
    if (gamestate_ != RUNNING && !is_terminating_) {
      std::unique_lock<std::mutex> lock(state_mutex_);

      if (gamestate_ == GameState::STARTING) {
        // core_->start();
        gamestate_ = GameState::RUNNING;
      } else if (gamestate_ == GameState::ENDING) {
        // core_->end();
        Logger::getInstance().Log(LogLevel::Debug, "Game thread ending to end");
        gamestate_ = ENDED;
      } else if (gamestate_ == GameState::PAUSING) {
        gamestate_ = PAUSED;
      }
      Logger::getInstance().Log(LogLevel::Debug,
                                "Game thread entering sleep state...");
      if (!is_terminating_ && (gamestate_ == GameState::PAUSED ||
                               gamestate_ == GameState::ENDED)) {
        Logger::getInstance().Log(LogLevel::Debug, "Game thread Sleep");
        condition_.wait(lock, [this] {
          return is_terminating_ || (gamestate_ != GameState::ENDED &&
                                     gamestate_ != GameState::PAUSED);
        });
      }
    }

    if (is_terminating_) {
      Logger::getInstance().Log(LogLevel::Info,
                                "Game thread return on terminating");
      return;
    }

    if (core_) {
      core_->Update();
    }
    if (scene_manager_) {
      scene_manager_->Update(1.0f / 60.0f);  // fixed timestep
    }

    frame_count_++;
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
  }
}

uint64_t GameManager::GetFrameCount() {
  return frame_count_;
}

void GameManager::Render() {
  //   if (core_ && gamestate_ == GameState::RUNNING) {
  if (core_) {
    // Logger::getInstance().Log(LogLevel::Debug,
    // "[GameManager] Calling game->Render()");
    core_->Render();
  } else if (scene_manager_) {
    // Logger::getInstance().Log(LogLevel::Debug,
    // "[GameManager] Calling scene->Render()");
    // scene_manager_->Render();
  }
}

void GameManager::Destroy() {
  Logger::getInstance().Log(LogLevel::Info, "Game manager destroy initiated");

  if (core_) {
    Logger::getInstance().Log(LogLevel::Info, "Calling core_->Destroy()");
    core_->Destroy();
  }
}
