// Checks must run in release builds too
#undef NDEBUG
#include <cassert>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>

#include <bgfx/bgfx.h>

#include "engine/renderer/model/draw_settings.h"
#include "engine/renderer/model/lod_generation.h"
#include "engine/renderer/model/mesh.h"

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

  // Generated LODs keep about the asked share of triangles, also for a mesh
  // whose triangles share no vertices (hard edges everywhere)
  Geometry::MeshData grid, split;
  constexpr int side = 21;  // vertices a side, a bumpy sheet
  for (int y = 0; y < side; ++y)
    for (int x = 0; x < side; ++x) {
      Geometry::VertexData vertex;
      vertex.position = {x, 0.3f * std::sin(x * 0.7f + y * 0.4f), y};
      grid.vertices.push_back(vertex);
    }
  for (int y = 0; y + 1 < side; ++y)
    for (int x = 0; x + 1 < side; ++x) {
      const uint32_t at = y * side + x;
      for (uint32_t i : {at, at + side, at + 1, at + 1, at + side,
                         at + side + 1})
        grid.indices.push_back(i);
    }
  for (uint32_t i : grid.indices) {
    split.indices.push_back(static_cast<uint32_t>(split.vertices.size()));
    split.vertices.push_back(grid.vertices[i]);
  }
  for (const Geometry::MeshData* mesh : {&grid, &split}) {
    const size_t full = mesh->TriangleCount();
    const size_t half = LodGeneration::Simplify(*mesh, 0.5f).TriangleCount();
    assert(half > 0 && half <= full * 5 / 8);
    assert(LodGeneration::Simplify(*mesh, 1.0f).indices == mesh->indices);
  }

  // A mesh draws as its LODs, two at once over the blend before each: the
  // next fading in over the same distances the one before fades out
  bgfx::Init init;
  init.type = bgfx::RendererType::Noop;
  assert(bgfx::init(init));
  {
    Mesh base(grid), lod1(split), lod2(split);
    base.SetDraw(0.0f, 0.0f, 0.1f, {{100.0f, &lod1}, {50.0f, nullptr},
                                    {200.0f, &lod2}});
    Mesh::Drawn drawn[2];
    assert(base.ForDistance(50.0f, drawn) == 1 && drawn[0].mesh == &base &&
           drawn[0].lod == glm::vec4(0.0f, 0.0f, 90.0f, 100.0f));
    assert(base.ForDistance(95.0f, drawn) == 2 && drawn[0].mesh == &base &&
           drawn[1].mesh == &lod1 &&
           drawn[1].lod == glm::vec4(90.0f, 100.0f, 180.0f, 200.0f));
    assert(base.ForDistance(150.0f, drawn) == 1 && drawn[0].mesh == &lod1);
    assert(base.ForDistance(190.0f, drawn) == 2 && drawn[1].mesh == &lod2 &&
           drawn[1].lod == glm::vec4(180.0f, 200.0f, 0.0f, 0.0f));
    assert(base.ForDistance(500.0f, drawn) == 1 && drawn[0].mesh == &lod2);
    base.SetDraw(0.0f, 0.0f, 0.0f, {{100.0f, &lod1}});
    assert(base.ForDistance(99.0f, drawn) == 1 && drawn[0].mesh == &base);
  }
  bgfx::shutdown();
  std::cout << "Draw settings: OK" << std::endl;
}

}  // namespace Tests
