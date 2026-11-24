#ifndef DRAW_SETTINGS_H
#define DRAW_SETTINGS_H

#include <filesystem>
#include <string>
#include <vector>

// How far and how finely a model draws, wherever it is used (entities,
// instanced sets, procedural content). Kept in the model's metadata file
// (<model>.meta, under "draw") and edited in the model asset's inspector
struct DrawSettings {
  // From `distance` metres from the viewer on, the model's meshes draw as
  // `source`'s (a project-relative model; mesh i stands in for mesh i), or
  // without a source as simplified copies of its own keeping about `detail`
  // (0..1) of their triangles (LodGeneration)
  struct Lod {
    float distance = 0.0f;
    std::string source;
    float detail = 0.5f;
  };
  // It dithers out from fade_start to fade_end metres from the viewer and
  // doesn't draw past it (fade_end <= fade_start: no limit)
  float fade_start = 0.0f;
  float fade_end = 0.0f;
  std::vector<Lod> lods;  // nearest first
  // Each LOD blends in (dithered) over this share of its distance before it,
  // as the one before blends out (0: they switch at once)
  float lod_fade = 0.1f;
  // From `distance` metres from the viewer on (0: never), instanced copies
  // draw as an imposter: cards baked from `views` x `views` directions over
  // the upper hemisphere, `resolution` pixels a side for each variant (a
  // procedural model bakes `variants` of its instances). It blends in over
  // lod_fade of the distance as the meshes blend out
  struct Imposter {
    float distance = 0.0f;
    int resolution = 1024;
    int views = 8;
    int variants = 4;
  };
  Imposter imposter;

  // The model's settings; defaults when its metadata has none
  static DrawSettings Read(const std::filesystem::path& model);
  // Writes them into the model's metadata, keeping its header and other
  // fields
  static bool Write(const std::filesystem::path& model,
                    const DrawSettings& settings);
};

#endif  // DRAW_SETTINGS_H
