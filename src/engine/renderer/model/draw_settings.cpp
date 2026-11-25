#include "draw_settings.h"

#include <fstream>
#include <nlohmann/json.hpp>
#include <sstream>

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(DrawSettings::Lod, distance,
                                                source, detail)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(DrawSettings, fade_start,
                                                fade_end, lods, lod_fade)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(ImposterSettings, distance,
                                                resolution, views, variants)

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

// The asset's metadata field; defaults when it has none
template <class T>
static T _Read(const std::filesystem::path& asset, const char* key) {
  std::string header;
  nlohmann::json body;
  _Split(asset.string() + ".meta", header, body);
  try {
    return body.value(key, T());
  } catch (const nlohmann::json::exception&) {
    return {};
  }
}

template <class T>
static bool _Write(const std::filesystem::path& asset, const char* key,
                   const T& value) {
  const std::filesystem::path meta = asset.string() + ".meta";
  std::string header;
  nlohmann::json body;
  _Split(meta, header, body);
  body[key] = value;
  // Text mode, like the metadata's other writers (ProjectAssets)
  std::ofstream out(meta);
  out << header << body.dump(4);
  return static_cast<bool>(out);
}

DrawSettings DrawSettings::Read(const std::filesystem::path& model) {
  return _Read<DrawSettings>(model, "draw");
}

bool DrawSettings::Write(const std::filesystem::path& model,
                         const DrawSettings& settings) {
  return _Write(model, "draw", settings);
}

ImposterSettings ImposterSettings::Read(
    const std::filesystem::path& descriptor) {
  return _Read<ImposterSettings>(descriptor, "imposter");
}

bool ImposterSettings::Write(const std::filesystem::path& descriptor,
                             const ImposterSettings& settings) {
  return _Write(descriptor, "imposter", settings);
}
