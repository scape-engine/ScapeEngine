#include "json.h"

#include <filesystem>
#include <fstream>

#include "engine/core/file_system_utils.h"
#include "engine/core/logger.h"

namespace Content {

std::optional<nlohmann::json> ReadJsonFile(const std::string& path) {
  if (!FileSystem::Exists(path)) {
    Logger::getInstance().Log(
        LogLevel::Error, "[Content::ReadJsonFile] File not found: " + path);
    return std::nullopt;
  }

  std::string contents = FileSystem::ReadFile(path);
  if (contents.empty()) {
    // Log open failure if it occurred.
    // Empty files are also invalid JSON; parser will reject it.
    Logger::getInstance().Log(
        LogLevel::Error,
        "[Content::ReadJsonFile] Empty or unreadable: " + path);
    return std::nullopt;
  }

  try {
    return nlohmann::json::parse(contents);
  } catch (const nlohmann::json::parse_error& e) {
    Logger::getInstance().Log(
        LogLevel::Error,
        std::string("[Content::ReadJsonFile] Parse error in ") + path + ": " +
            e.what());
    return std::nullopt;
  }
}

bool WriteJsonFile(const std::string& path, const nlohmann::json& j,
                   int indent) {
  // Write next to the target, then swap: a crash mid-write never leaves a
  // truncated file behind
  const std::string tmp_path = path + ".tmp";
  try {
    {
      std::ofstream file(tmp_path);
      if (!file.is_open()) {
        Logger::getInstance().Log(
            LogLevel::Error,
            "[Content::WriteJsonFile] Cannot open for writing: " + tmp_path);
        return false;
      }
      file << (indent < 0 ? j.dump() : j.dump(indent));
      if (!file.good()) {
        Logger::getInstance().Log(
            LogLevel::Error,
            "[Content::WriteJsonFile] Write failed: " + tmp_path);
        return false;
      }
    }  // closed before the rename

    std::error_code ec;
    std::filesystem::rename(tmp_path, path, ec);
    if (ec) {
      Logger::getInstance().Log(LogLevel::Error,
                                "[Content::WriteJsonFile] Cannot replace " +
                                    path + ": " + ec.message());
      return false;
    }
    return true;
  } catch (const std::exception& e) {
    Logger::getInstance().Log(
        LogLevel::Error,
        std::string("[Content::WriteJsonFile] Serialization error in ") + path +
            ": " + e.what());
    return false;
  }
}

}  // namespace Content