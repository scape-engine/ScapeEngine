#include "engine/core/logger.h"
#include <filesystem>

/**
 * CORE LOGGER
 * Issues:
 * ! - file on disk is unbounded
 * ! - copy is O(n) in entries, but only happens when version_ changes
 * TODO: store struct LogEntry { LogLevel level; std::string text; } here to
 * change GetLogEntries()'s type and its callers
 */

Logger::Logger() : logfile_() {

  namespace fs = std::filesystem;

  fs::path log_dir = fs::current_path() / "logs" / "engine";
  std::error_code ec;
  fs::create_directories(log_dir, ec);
  if (ec) {
    std::cerr << "Logger: failed to create log directory: " << ec.message()
              << '\n';
    return;
  }

  // Define two log paths (old and new)
  const fs::path current_log = log_dir / "enginelog.txt";
  const fs::path old_log = log_dir / "oldlog.txt";

  // Discard oldest, promote current log to old, create new file for current log
  if (fs::exists(old_log))
    fs::remove(old_log);
  if (fs::exists(current_log))
    fs::rename(current_log, old_log);

  logfile_.open(current_log, std::ios::trunc);
  if (!logfile_)
    std::cerr << "Log File failed to open at " << current_log.string() << '\n';
  else
    std::cerr << "Log File successfully opened at " << current_log.string()
              << '\n';
}

Logger::~Logger() {  // destructor to close logger class if it is still open
                     // after execution
  if (logfile_) {
    logfile_.close();
  }
}
std::string Logger::GetLogLevelName(LogLevel level) {
  return logLevelNames[level];  // Lookup the mapping for levels
}

uint64_t Logger::GetVersion() {
  std::lock_guard<std::mutex> guard(mutex_);
  return version_;
}

std::deque<std::string> Logger::GetLogEntries() {
  std::lock_guard<std::mutex> guard(mutex_);
  return entries_;  // copied while the lock is held
}

void Logger::Log(LogLevel level, const std::string& message) {
  std::lock_guard<std::mutex> guard(mutex_);  // Prevent overwriting; mutex
                                              // gives multi-threading support
  std::string formatted = "[" + GetLogLevelName(level) + "] " + message;

  entries_.push_back(formatted);
  ++version_;

  if (entries_.size() > MAX_ENTRIES)
    entries_.pop_front();

  logfile_ << formatted << std::endl;
  logfile_.flush();  // Ensure the message is written to the file
}
