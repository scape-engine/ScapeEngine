// Checks must run in release builds too
#undef NDEBUG
#include <cassert>

#include <filesystem>
#include <fstream>
#include <iostream>

#include "engine/pcg/procmodel/descriptor/descriptor_edits.h"
#include "engine/pcg/procmodel/descriptor/model_descriptor_parser.h"

namespace Tests {

namespace Edits = ProcModel::DescriptorEdits;

static bool _Loads(const Edits::Document& doc) {
  ProcModel::ModelDescriptor parsed;
  return ProcModel::ModelDescriptorParser::FromJson(
      nlohmann::json::parse(doc.dump()), parsed);
}

// Edits keep every reference whole, refuse what would break the
// descriptor, and leave what they don't touch (unknown keys, their order)
void TestDescriptorEdits() {
  Edits::Document doc = Edits::Document::parse(R"({
    "model_id": "m", "path": "m.glb", "custom": {"kept": true},
    "selection_groups": [
      {"group_id": "trunk", "parts": [{"id": "T_A"}, {"id": "T_B"}]},
      {"group_id": "branches", "parent": "T_A", "list": 1,
       "parts": [{"id": "B_A", "weight": 2}]}],
    "transform_ranges": [{"part_id": "T_A", "scale_min": [1, 1, 1]}],
    "constraints": [{"id": "c", "type": "excludes", "part_a": "T_A",
                     "part_b": "B_A"}],
    "locator_operations": [{"kind": "k", "target_group_id": "branches"},
                           {"kind": "all"}],
    "part_deformation_ranges": [{"group_id": "branches"}]})");
  assert(_Loads(doc));

  // The only T_A: what named it follows
  assert(Edits::SetPart(doc, 0, 0, "TRUNK"));
  assert(doc["selection_groups"][1]["parent"] == Edits::Document({"TRUNK"}));
  assert(doc["transform_ranges"][0]["part_id"] == "TRUNK");
  assert(doc["constraints"][0]["part_a"] == "TRUNK");
  assert(!Edits::SetPart(doc, 0, 1, "TRUNK"));  // the group offers it
  assert(!Edits::SetPart(doc, 0, 1, ""));
  // A part offered elsewhere too: what names it stays
  assert(Edits::AddPart(doc, 1, "TRUNK"));
  assert(Edits::SetPart(doc, 1, 1, "B_B"));
  assert(doc["selection_groups"][1]["parent"] == Edits::Document({"TRUNK"}));
  assert(Edits::RemovePart(doc, 1, 1));

  assert(Edits::RenameGroup(doc, 1, "twigs"));
  assert(doc["locator_operations"][0]["target_group_id"] == "twigs");
  assert(!doc["locator_operations"][1].contains("target_group_id"));
  assert(doc["part_deformation_ranges"][0]["group_id"] == "twigs");

  // Links: no duplicates, a group's own part recurses; the last unlink makes
  // a root
  assert(Edits::Link(doc, 0, "T_B") && Edits::Unlink(doc, 0, "T_B"));
  assert(!doc["selection_groups"][0].contains("parent"));
  assert(!Edits::Link(doc, 1, "TRUNK"));
  assert(!Edits::Link(doc, 1, "NONE"));
  assert(Edits::Link(doc, 1, "T_B"));
  assert(Edits::Unlink(doc, 1, "TRUNK") && Edits::Unlink(doc, 1, "T_B"));
  assert(!doc["selection_groups"][1].contains("parent"));
  assert(Edits::Link(doc, 1, "TRUNK"));

  // A group's last part stays; removing one drops what names it
  assert(!Edits::RemovePart(doc, 1, 0));
  assert(Edits::RemovePart(doc, 0, 0));
  assert(doc["transform_ranges"].empty() && doc["constraints"].empty());
  assert(!doc["selection_groups"][1].contains("parent"));

  assert(Edits::AddGroup(doc, Edits::UniqueGroupId(doc, "twigs"), "L_A"));
  assert(doc["selection_groups"][2]["group_id"] == "twigs_2");
  assert(!Edits::AddGroup(doc, "twigs", "L_B"));  // group id taken
  assert(Edits::Link(doc, 2, "B_A"));
  assert(Edits::RemoveGroup(doc, 1));
  assert(doc["selection_groups"].size() == 2);
  assert(!doc["selection_groups"][1].contains("parent"));
  assert(doc["locator_operations"].size() == 1);
  assert(doc["part_deformation_ranges"].empty());

  assert(_Loads(doc));
  assert(doc["custom"]["kept"] == true &&
         doc["selection_groups"][0].contains("list") == false &&
         doc.begin().key() == "model_id");

  // The example project's models (parts shared by groups) still load after
  // each part is changed
  const std::filesystem::path models =
      std::filesystem::path(__FILE__).parent_path() /
      "../../examples/example-project/models";
  for (const char* name : {"tree.json", "rock.json", "creature.json"}) {
    std::ifstream file(models / name);
    if (!file)
      continue;
    Edits::Document model = Edits::Document::parse(file);
    assert(_Loads(model));
    auto& groups = model["selection_groups"];
    for (size_t group = 0; group < groups.size(); ++group)
      for (size_t part = 0; part < groups[group]["parts"].size(); ++part) {
        const std::string id = groups[group]["parts"][part]["id"];
        assert(Edits::SetPart(model, group, part, id + "_other"));
      }
    assert(_Loads(model));
  }

  std::cout << "Descriptor edit tests passed\n";
}

}  // namespace Tests
