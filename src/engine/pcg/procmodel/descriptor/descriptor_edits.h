#ifndef DESCRIPTOR_EDITS_H
#define DESCRIPTOR_EDITS_H

#include <cstddef>
#include <string>

#include <nlohmann/json.hpp>

// Edits on a model descriptor's JSON document, in place: only what an edit
// changes is written, so fields an editor doesn't know (and their order)
// are kept. Groups and parts are by index in "selection_groups" and their
// "parts". A part's id names a part of the model: several groups may offer
// the same part, and a group's "parent", transform ranges, constraints and
// bindings name it wherever it is placed (a group activated by one of its
// own parts recurses). A group's id is what locator operations and
// deformation ranges target. Edits keep those references whole and return
// false, changing nothing, when they would make the descriptor invalid
namespace ProcModel::DescriptorEdits {

using Document = nlohmann::ordered_json;

// An id no group (or part) has yet: `base`, else `base` and a number
std::string UniqueGroupId(const Document& doc, const std::string& base);
std::string UniquePartId(const Document& doc, const std::string& base);

// A root group with one part (a group needs one)
bool AddGroup(Document& doc, const std::string& group_id,
              const std::string& part_id);
// Also drops what targets it, and what names parts no group offers then
bool RemoveGroup(Document& doc, size_t group);
bool RenameGroup(Document& doc, size_t group, const std::string& group_id);
bool SetRequired(Document& doc, size_t group, bool required);

// A part the group doesn't offer yet
bool AddPart(Document& doc, size_t group, const std::string& part_id);
// Not a group's last part; what names it is dropped when no group offers
// it any more
bool RemovePart(Document& doc, size_t group, size_t part);
// The group offers another part there; what named the old one names the
// new one when no group offers the old one any more
bool SetPart(Document& doc, size_t group, size_t part,
             const std::string& part_id);
bool SetWeight(Document& doc, size_t group, size_t part, float weight);

// The group is activated by the part, wherever it is placed
bool Link(Document& doc, size_t group, const std::string& part_id);
bool Unlink(Document& doc, size_t group, const std::string& part_id);

}  // namespace ProcModel::DescriptorEdits

#endif  // DESCRIPTOR_EDITS_H
