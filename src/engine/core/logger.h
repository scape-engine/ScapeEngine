#ifndef LOGGER_H
#define LOGGER_H

#include <atomic>
#include <cstdint>
#include <deque>
#include <fstream>
#include <iostream>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "engine/core/singleton.hpp"

enum class LogLevel { Info, Debug, Warning, Error };

class Logger : public Singleton<Logger> {
public:
  Logger();
  ~Logger();

  void Log(LogLevel level, const std::string& message);

  std::deque<std::string> GetLogEntries();

  // Total errors logged since startup
  uint64_t GetErrorCount() const {
    return error_count_.load(std::memory_order_relaxed);
  }

  std::map<LogLevel, std::string> logLevelNames{{LogLevel::Info, "INFO"},
                                                {LogLevel::Debug, "DEBUG"},
                                                {LogLevel::Warning, "WARNING"},
                                                {LogLevel::Error, "ERROR"}};

  std::string GetLogLevelName(LogLevel level);

  uint64_t GetVersion();

private:
  std::deque<std::string> entries_;  // log entries
  std::ofstream logfile_;            // stream data to file
  std::mutex mutex_;  // support for multi-thread i.e. only 1 thread can access
  // at a time

  static constexpr size_t MAX_ENTRIES = 10000;

  uint64_t version_ = 0;
  std::atomic<uint64_t> error_count_{0};
};

#endif  // LOGGER_H
