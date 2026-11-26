// Checks must run in release builds too
#undef NDEBUG
#include <cassert>

#include <cmath>
#include <filesystem>
#include <iostream>

#include "engine/pcg/edit_layer.h"

namespace Tests {

// A point of `object` at x
static PCG::InstancePoint Point(const std::string& object, float x,
                                uint64_t seed, int kind) {
  PCG::InstancePoint point{object, {}, seed, kind};
  point.matrix[0] = point.matrix[5] = point.matrix[10] = point.matrix[15] = 1;
  point.matrix[12] = x;
  return point;
}

// Edits find their points by object and place among that object's, check
// they are still where they were placed, and keep the other points' seeds
void TestEditLayer() {
  // rock 0 at 0, tree 0 at 10, rock 1 at 20, rock 2 at 30
  const std::vector<PCG::InstancePoint> placed = {
      Point("rock", 0, 1, 0), Point("tree", 10, 2, 1), Point("rock", 20, 3, 0),
      Point("rock", 30, 4, 0)};

  PCG::CellEdits edits;
  // Rock 1 goes; the rocks after it keep their seeds
  edits.edits.push_back({"rock", 1, {20, 0, 0}, true});
  // The tree moves and takes another seed; only its position changes
  PCG::InstanceEdit move{"tree", 0, {10, 0, 0}};
  move.position = glm::vec3(12, 0, 0);
  move.seed = 99;
  edits.edits.push_back(move);
  // Rock 2 becomes a bush, a little off where it was placed still counts
  PCG::InstanceEdit swap{"rock", 2, {30.3f, 0, 0}};
  swap.replacement = "bush.object";
  edits.edits.push_back(swap);
  // Moved by the rules (rock 0 isn't at 5) and gone (no rock 3): orphans
  edits.edits.push_back({"rock", 0, {5, 0, 0}, true});
  edits.edits.push_back({"rock", 3, {40, 0, 0}, true});
  edits.additions.push_back({"bush.object", 7, Point("", 50, 0, 0).matrix});

  std::vector<PCG::InstancePoint> points = placed;
  const std::vector<size_t> orphans = PCG::ApplyEdits(edits, points);
  assert((orphans == std::vector<size_t>{3, 4}));
  assert(points.size() == 4);
  assert(points[0].object == "rock" && points[0].seed == 1);  // orphan kept
  assert(points[1].object == "tree" && points[1].seed == 99 &&
         points[1].matrix[12] == 12 && points[1].matrix[0] == 1 &&
         points[1].matrix[5] == 1 && points[1].kind == 1);

  // A scaled and turned matrix comes apart and back the same
  PCG::PointTransform transform{{1, 2, 3}, {10, 20, 30}, {2, 2, 2}};
  const PCG::PointMatrix turned = PCG::Compose(transform);
  const PCG::PointMatrix again = PCG::Compose(PCG::Decompose(turned));
  for (int i = 0; i < 16; ++i)
    assert(std::abs(turned[i] - again[i]) < 1e-4f);
  assert(points[2].object == "bush.object" && points[2].seed == 4 &&
         points[2].kind == -1 && points[2].matrix[12] == 30);
  assert(points[3].object == "bush.object" && points[3].seed == 7 &&
         points[3].kind == -1 && points[3].matrix[12] == 50);
  // Each knows which it is: placed ones as placed, additions by place
  assert(points[2].key.object == "rock" && points[2].key.index == 2 &&
         points[2].key.anchor.x == 30 && points[2].key.addition == -1);
  assert(points[3].key.addition == 0);

  // Edits made by key: one per point, reverted to none
  PCG::EditLayer edited;
  edited.Edit("objects", 5, points[2].key).seed = 3;
  edited.Edit("objects", 5, points[2].key).removed = true;
  assert(edited.Count() == 1 &&
         edited.Find("objects", 5)->edits[0].seed == 3u);
  edited.Revert("objects", 5, points[2].key);
  assert(edited.Empty());

  // Saved and read back the same; a missing file is an empty layer
  PCG::EditLayer layer;
  layer.Cell("objects", (uint64_t{3} << 60) | 17) = edits;
  assert((layer.Objects() == std::vector<std::string>{"bush.object"}));
  const auto folder =
      std::filesystem::temp_directory_path() / "scape_edit_layer";
  assert(PCG::SaveEditLayer(folder / "planet.edits", layer));
  PCG::EditLayer read;
  assert(PCG::LoadEditLayer(folder / "planet.edits", read));
  assert(PCG::WriteEditLayer(read) == PCG::WriteEditLayer(layer));
  const PCG::CellEdits* cell = read.Find("objects", (uint64_t{3} << 60) | 17);
  assert(cell && cell->edits.size() == 5 && cell->edits[1].seed == 99u &&
         cell->edits[2].replacement == "bush.object");
  assert(!read.Find("objects", 17) && !read.Find("herds", 17));
  assert(PCG::LoadEditLayer(folder / "missing.edits", read) && read.Empty());
  std::filesystem::remove_all(folder);

  std::cout << "Edit layer tests passed" << std::endl;
}

}  // namespace Tests
