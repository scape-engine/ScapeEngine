#include "descriptor_edits.h"

#include <algorithm>
#include <functional>

namespace ProcModel::DescriptorEdits {

namespace {

// The entries naming a part, by list and key
struct PartReference {
  const char* list;
  const char* key;
};
constexpr PartReference part_references[] = {
    {"transform_ranges", "part_id"},
    {"constraints", "part_a"},
    {"constraints", "part_b"},
    {"parameter_bindings", "source_part"},
    {"parameter_bindings", "target_part"}};
// And those targeting a group
constexpr PartReference group_references[] = {
    {"locator_operations", "target_group_id"},
    {"part_deformation_ranges", "group_id"}};

Document* _Group(Document& doc, size_t group) {
  auto groups = doc.find("selection_groups");
  if (groups == doc.end() || !groups->is_array() || group >= groups->size())
    return nullptr;
  return &(*groups)[group];
}

Document* _Part(Document& doc, size_t group, size_t part) {
  Document* found = _Group(doc, group);
  if (!found || !found->contains("parts") || part >= (*found)["parts"].size())
    return nullptr;
  return &(*found)["parts"][part];
}

bool _HasGroup(const Document& doc, const std::string& id) {
  for (const auto& group : doc.value("selection_groups", Document::array()))
    if (group.value("group_id", "") == id)
      return true;
  return false;
}

bool _Offers(const Document& group, const std::string& id) {
  for (const auto& part : group.value("parts", Document::array()))
    if (part.value("id", "") == id)
      return true;
  return false;
}

bool _HasPart(const Document& doc, const std::string& id) {
  for (const auto& group : doc.value("selection_groups", Document::array()))
    if (_Offers(group, id))
      return true;
  return false;
}

// A group's parents as an array (the parser takes a lone string too)
Document& _Parents(Document& group) {
  Document& parent = group["parent"];
  if (parent.is_string())
    parent = Document::array({parent});
  else if (!parent.is_array())
    parent = Document::array();
  return parent;
}

void _DropParent(Document& group, const std::string& part_id) {
  if (!group.contains("parent"))
    return;
  Document& parents = _Parents(group);
  parents.erase(std::remove(parents.begin(), parents.end(), part_id),
                parents.end());
  // No parent left: a root group, as one never linked
  if (parents.empty())
    group.erase("parent");
}

// Entries in `references`' lists whose key is `id`: renamed, or dropped
// when `rename` is empty
template <size_t N>
void _Refer(Document& doc, const PartReference (&references)[N],
            const std::string& id, const std::string& rename) {
  for (const PartReference& reference : references) {
    auto list = doc.find(reference.list);
    if (list == doc.end() || !list->is_array())
      continue;
    for (auto it = list->begin(); it != list->end();) {
      if (it->is_object() && it->value(reference.key, "") == id) {
        if (rename.empty()) {
          it = list->erase(it);
          continue;
        }
        (*it)[reference.key] = rename;
      }
      ++it;
    }
  }
}

// What names a part no group offers any more: dropped, or naming
// `rename` instead
void _Orphaned(Document& doc, const std::string& id,
               const std::string& rename) {
  if (_HasPart(doc, id))
    return;
  for (auto& group : doc["selection_groups"]) {
    if (!group.contains("parent"))
      continue;
    if (rename.empty()) {
      _DropParent(group, id);
      continue;
    }
    for (auto& parent : _Parents(group))
      if (parent == id)
        parent = rename;
  }
  _Refer(doc, part_references, id, rename);
}

std::string _Unique(const std::string& base,
                    const std::function<bool(const std::string&)>& taken) {
  if (!taken(base))
    return base;
  for (int i = 2;; ++i)
    if (const std::string id = base + "_" + std::to_string(i); !taken(id))
      return id;
}

}  // namespace

std::string UniqueGroupId(const Document& doc, const std::string& base) {
  return _Unique(base, [&](const std::string& id) {
    return _HasGroup(doc, id);
  });
}

std::string UniquePartId(const Document& doc, const std::string& base) {
  return _Unique(base, [&](const std::string& id) {
    return _HasPart(doc, id);
  });
}

bool AddGroup(Document& doc, const std::string& group_id,
              const std::string& part_id) {
  if (group_id.empty() || part_id.empty() || _HasGroup(doc, group_id))
    return false;
  Document& groups = doc["selection_groups"];
  if (!groups.is_array())
    groups = Document::array();
  groups.push_back(
      {{"group_id", group_id},
       {"parts", Document::array({{{"id", part_id}, {"weight", 1.0}}})}});
  return true;
}

bool RemoveGroup(Document& doc, size_t group) {
  Document* found = _Group(doc, group);
  if (!found)
    return false;
  const std::string id = found->value("group_id", "");
  const Document parts = found->value("parts", Document::array());
  doc["selection_groups"].erase(group);
  for (const auto& part : parts)
    _Orphaned(doc, part.value("id", ""), "");
  _Refer(doc, group_references, id, "");
  return true;
}

bool RenameGroup(Document& doc, size_t group, const std::string& group_id) {
  Document* found = _Group(doc, group);
  if (!found || group_id.empty() || _HasGroup(doc, group_id))
    return false;
  const std::string old = found->value("group_id", "");
  (*found)["group_id"] = group_id;
  _Refer(doc, group_references, old, group_id);
  return true;
}

bool SetRequired(Document& doc, size_t group, bool required) {
  Document* found = _Group(doc, group);
  if (!found)
    return false;
  (*found)["required"] = required;
  return true;
}

bool AddPart(Document& doc, size_t group, const std::string& part_id) {
  Document* found = _Group(doc, group);
  if (!found || part_id.empty() || _Offers(*found, part_id))
    return false;
  Document& parts = (*found)["parts"];
  if (!parts.is_array())
    parts = Document::array();
  parts.push_back({{"id", part_id}, {"weight", 1.0}});
  return true;
}

bool RemovePart(Document& doc, size_t group, size_t part) {
  Document* found = _Part(doc, group, part);
  if (!found || (*_Group(doc, group))["parts"].size() < 2)
    return false;
  const std::string id = found->value("id", "");
  (*_Group(doc, group))["parts"].erase(part);
  _Orphaned(doc, id, "");
  return true;
}

bool SetPart(Document& doc, size_t group, size_t part,
             const std::string& part_id) {
  Document* found = _Part(doc, group, part);
  if (!found || part_id.empty() || _Offers(*_Group(doc, group), part_id))
    return false;
  const std::string old = found->value("id", "");
  (*found)["id"] = part_id;
  _Orphaned(doc, old, part_id);
  return true;
}

bool SetWeight(Document& doc, size_t group, size_t part, float weight) {
  Document* found = _Part(doc, group, part);
  if (!found || weight < 0.0f)
    return false;
  (*found)["weight"] = weight;
  return true;
}

bool Link(Document& doc, size_t group, const std::string& part_id) {
  Document* found = _Group(doc, group);
  if (!found || !_HasPart(doc, part_id))
    return false;
  Document& parents = _Parents(*found);
  if (std::find(parents.begin(), parents.end(), part_id) != parents.end())
    return false;
  parents.push_back(part_id);
  return true;
}

bool Unlink(Document& doc, size_t group, const std::string& part_id) {
  Document* found = _Group(doc, group);
  if (!found || !found->contains("parent"))
    return false;
  _DropParent(*found, part_id);
  return true;
}

}  // namespace ProcModel::DescriptorEdits
