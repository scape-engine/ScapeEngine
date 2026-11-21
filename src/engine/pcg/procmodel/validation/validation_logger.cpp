#include "validation_logger.h"

#include <nlohmann/json.hpp>
#include <system_error>

#include "engine/core/logger.h"

namespace ProcModel {

// ---------------------------------------------------------------------------
// ValidationLogger
// ---------------------------------------------------------------------------
ValidationLogger::~ValidationLogger() {
  std::scoped_lock lock(mutex_);
  if (stream_.is_open()) {
    stream_.flush();
    stream_.close();
  }
}

void ValidationLogger::SetPath(const std::filesystem::path& path) {
  std::scoped_lock lock(mutex_);
  if (stream_.is_open()) {
    stream_.flush();
    stream_.close();
  }
  path_ = path;
  open_attempted_ = false;
}

std::filesystem::path ValidationLogger::DefaultLogPath() {
  // Resolves to the working directory at process start, which for xmake
  // builds is typically build/<platform>/<arch>/release/.
  // TODO: switch to executable-relative path once a platform abstraction
  // for exe-dir exists.
  return std::filesystem::current_path() / "logs" / "procmodel" /
         "generation_log.jsonl";
}

bool ValidationLogger::EnsureOpen() {
  if (stream_.is_open())
    return true;
  if (open_attempted_)
    return false;  // Already tried and failed this session

  open_attempted_ = true;

  if (path_.empty())
    path_ = DefaultLogPath();

  std::error_code ec;
  std::filesystem::create_directories(path_.parent_path(), ec);
  if (ec) {
    Logger::getInstance().Log(
        LogLevel::Error,
        "[ValidationLogger] Failed to create log directory: " + ec.message());
    return false;
  }

  stream_.open(path_, std::ios::out | std::ios::trunc);
  if (!stream_.is_open()) {
    Logger::getInstance().Log(
        LogLevel::Error,
        "[ValidationLogger] Failed to open log file: " + path_.string());
    return false;
  }

  Logger::getInstance().Log(LogLevel::Info,
                            "[ValidationLogger] Logging to " + path_.string());
  return true;
}

bool ValidationLogger::Write(const ProcModelSample& result) {
  std::scoped_lock lock(mutex_);
  if (!EnsureOpen())
    return false;

  // DEBUG
  static int s_write_count = 0;
  ++s_write_count;
  if (s_write_count % 100 == 0) {
    Logger::getInstance().Log(
        LogLevel::Debug,
        "[ValidationLogger] Records written: " + std::to_string(s_write_count));
  }

  try {
    // Not flushed per record: a sync write per spawned model slowed region
    // loading; the stream flushes on its own and on close
    stream_ << SerializeSample(result).dump() << '\n';
    return stream_.good();
  } catch (const std::exception& e) {
    Logger::getInstance().Log(
        LogLevel::Error,
        std::string("[ValidationLogger] Serialization failed: ") + e.what());
    return false;
  }
}

}  // namespace ProcModel