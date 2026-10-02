#ifndef LOGGER_H
#define LOGGER_H

#include <deque>
#include <fstream>  // file modification lib
#include <iostream>
#include <map>     // for log level mapping
#include <memory>  // smart pointer
#include <mutex>   // thread sync
#include <string>
#include <vector>

#include "engine/core/singleton.hpp"
// Enumeration for log levels
enum class LogLevel { Info, Debug, Warning, Error };

class Logger : public Singleton<Logger> {
public:
  Logger();
  ~Logger();

  void Log(LogLevel level, const std::string& message);  // Log message

  std::deque<std::string> GetLogEntries();

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
};

#endif  // LOGGER_H
