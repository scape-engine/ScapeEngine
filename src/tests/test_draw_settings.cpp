// Checks must run in release builds too
#undef NDEBUG
#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>

#include "engine/renderer/model/draw_settings.h"

namespace Tests {

// Draw settings round-trip through a model's metadata file, keeping its
// header and other fields; a file without them gives the defaults
void TestDrawSettings() {
  const std::filesystem::path model =
      std::filesystem::temp_directory_path() / "scape_draw_settings.glb";
  const std::filesystem::path meta = model.string() + ".meta";
  {
    std::ofstream out(meta, std::ios::binary);
    out << "guid: 1f1b642c-23db-44b4-94cf-6b65ded6f0ad\n---\n{\"other\": 3}";
  }
  assert(DrawSettings::Read(model).lods.empty());

  DrawSettings settings;
  settings.fade_start = 70.0f;
  settings.fade_end = 80.0f;
  settings.lods = {{15.0f, "models/rock_lod1.glb"}, {35.0f, "models/rock_lod2.glb"}};
  assert(DrawSettings::Write(model, settings));

  const DrawSettings read = DrawSettings::Read(model);
  assert(read.fade_start == 70.0f && read.fade_end == 80.0f);
  assert(read.lods.size() == 2 && read.lods[1].distance == 35.0f &&
         read.lods[1].source == "models/rock_lod2.glb");
  std::stringstream text;
  text << std::ifstream(meta).rdbuf();
  assert(text.str().rfind("guid: 1f1b642c-23db-44b4-94cf-6b65ded6f0ad\n---\n",
                          0) == 0);
  assert(text.str().find("\"other\": 3") != std::string::npos);

  std::filesystem::remove(meta);
  assert(DrawSettings::Read(model).fade_end == 0.0f);
  std::cout << "Draw settings: OK" << std::endl;
}

}  // namespace Tests
