#include "draw_settings.h"

#include <fstream>
#include <nlohmann/json.hpp>
#include <sstream>

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(DrawSettings::Lod, distance,
                                                source, detail)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(DrawSettings, fade_start,
                                                fade_end, lods, lod_fade)

// A metadata file is a header ("guid: ...", then "---") and a JSON body
static void _Split(const std::filesystem::path& meta, std::string& header,
                   nlohmann::json& body) {
  std::ifstream in(meta);
  std::stringstream text;
  text << in.rdbuf();
  const std::string source = text.str();
  // No header: all of it is the body
  const size_t separator = source.find("---");
  const size_t end = separator == std::string::npos
                         ? std::string::npos
                         : source.find('\n', separator);
  header = end == std::string::npos ? std::string() : source.substr(0, end + 1);
  body = nlohmann::json::parse(
      end == std::string::npos ? source : source.substr(end + 1), nullptr,
      false);
  if (!body.is_object())
    body = nlohmann::json::object();
}

DrawSettings DrawSettings::Read(const std::filesystem::path& model) {
  std::string header;
  nlohmann::json body;
  _Split(model.string() + ".meta", header, body);
  try {
    return body.value("draw", DrawSettings());
  } catch (const nlohmann::json::exception&) {
    return {};
  }
}

bool DrawSettings::Write(const std::filesystem::path& model,
                         const DrawSettings& settings) {
  const std::filesystem::path meta = model.string() + ".meta";
  std::string header;
  nlohmann::json body;
  _Split(meta, header, body);
  body["draw"] = settings;
  // Text mode, like the metadata's other writers (ProjectAssets)
  std::ofstream out(meta);
  out << header << body.dump(4);
  return static_cast<bool>(out);
}
