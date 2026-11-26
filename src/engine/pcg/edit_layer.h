#ifndef PCG_EDIT_LAYER_H
#define PCG_EDIT_LAYER_H

#include <array>
#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include <glm/glm.hpp>
#include <nlohmann/json.hpp>

namespace PCG {

// Hand edits on what a generator's stages placed, kept apart from the rules
// and settings that placed it: an asset (JSON, .edits) the generator's
// volume points to. A stage's points are found by their cell (the stage's own
// key for it) and, within the cell, by their object and their place among
// that object's points; each edit also keeps where its point was placed, so
// an edit whose point the rules moved or dropped changes nothing

// Matrices are rows of x, y, z axes, then the translation (worldgen's)
using PointMatrix = std::array<float, 16>;

// A change to one placed point
struct InstanceEdit {
  std::string object;  // the point's object, as placed
  uint32_t index = 0;  // its place among the cell's points of that object
  glm::vec3 anchor{0.0f};  // its translation as placed
  bool removed = false;
  // Moved: each set part replaces the placed one's (rotation in degrees,
  // as Euler angles)
  std::optional<glm::vec3> position;
  std::optional<glm::vec3> rotation;
  std::optional<glm::vec3> scale;
  std::string replacement;       // another object; empty: its own
  std::optional<uint64_t> seed;  // another seed

  bool operator==(const InstanceEdit&) const = default;
};

// A point's matrix as position, rotation (degrees, Euler angles) and scale,
// and back
struct PointTransform {
  glm::vec3 position{0.0f};
  glm::vec3 rotation{0.0f};
  glm::vec3 scale{1.0f};
};
PointTransform Decompose(const PointMatrix& matrix);
PointMatrix Compose(const PointTransform& transform);

// A point placed by hand
struct InstanceAddition {
  std::string object;
  uint64_t seed = 0;
  PointMatrix matrix{};

  bool operator==(const InstanceAddition&) const = default;
};

struct CellEdits {
  std::vector<InstanceEdit> edits;
  std::vector<InstanceAddition> additions;

  bool operator==(const CellEdits&) const = default;
};

// Which point of a cell an edit is on: a placed one (as InstanceEdit finds
// it) or one of the cell's additions
struct InstanceKey {
  std::string object;
  uint32_t index = 0;
  glm::vec3 anchor{0.0f};
  int addition = -1;  // into the cell's additions; -1: a placed point
};

// A point a stage placed in a cell
struct InstancePoint {
  std::string object;
  PointMatrix matrix{};
  uint64_t seed = 0;
  int kind = -1;  // the stage's own index for its object; -1 once it changes
  InstanceKey key;  // which it is, set by ApplyEdits
};

class EditLayer {
public:
  // The cell's edits in the stage; null: none
  const CellEdits* Find(const std::string& stage, uint64_t cell) const;
  CellEdits& Cell(const std::string& stage, uint64_t cell);
  // The edit on a placed point, made when it has none
  InstanceEdit& Edit(const std::string& stage, uint64_t cell,
                     const InstanceKey& key);
  // The point's edit, or the addition, goes
  void Revert(const std::string& stage, uint64_t cell, const InstanceKey& key);
  // Its edits and additions
  size_t Count() const;
  // The objects its replacements and additions name
  std::vector<std::string> Objects() const;
  bool Empty() const { return stages_.empty(); }

  // By stage, then cell
  std::map<std::string, std::map<uint64_t, CellEdits>> stages_;
};

nlohmann::json WriteEditLayer(const EditLayer& layer);
// False, leaving the layer empty, when `in` isn't an edit layer
bool ReadEditLayer(const nlohmann::json& in, EditLayer& layer);
// A missing file reads as an empty layer
bool LoadEditLayer(const std::filesystem::path& path, EditLayer& layer);
bool SaveEditLayer(const std::filesystem::path& path, const EditLayer& layer);

// An edit's point must still be this close to where it was placed, in metres
constexpr float kEditTolerance = 0.5f;

// The cell's edits on its points, in their placed order: removed points go,
// edited ones take their transform's parts, object and seed, the additions
// join them.
// Each point gets its key. Returns the edits whose point isn't there any
// more (by `object`, `index` and `anchor`), which change nothing
std::vector<size_t> ApplyEdits(const CellEdits& edits,
                               std::vector<InstancePoint>& points);

}  // namespace PCG

#endif  // PCG_EDIT_LAYER_H
