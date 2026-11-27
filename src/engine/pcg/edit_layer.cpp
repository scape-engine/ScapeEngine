#include "edit_layer.h"

#include <algorithm>
#include <cstring>
#include <fstream>

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/type_ptr.hpp>

namespace PCG {

// Rows read as glm columns: its axes are the columns
PointTransform Decompose(const PointMatrix& matrix) {
  const glm::mat4 m = glm::make_mat4(matrix.data());
  PointTransform transform;
  transform.position = glm::vec3(m[3]);
  transform.scale =
      glm::vec3(glm::length(glm::vec3(m[0])), glm::length(glm::vec3(m[1])),
                glm::length(glm::vec3(m[2])));
  const glm::mat3 rotation(glm::vec3(m[0]) / transform.scale.x,
                           glm::vec3(m[1]) / transform.scale.y,
                           glm::vec3(m[2]) / transform.scale.z);
  transform.rotation =
      glm::degrees(glm::eulerAngles(glm::quat_cast(rotation)));
  return transform;
}

PointMatrix Compose(const PointTransform& transform) {
  const glm::mat4 m =
      glm::translate(glm::mat4(1.0f), transform.position) *
      glm::mat4_cast(glm::quat(glm::radians(transform.rotation))) *
      glm::scale(glm::mat4(1.0f), transform.scale);
  PointMatrix matrix;
  std::memcpy(matrix.data(), glm::value_ptr(m), sizeof(matrix));
  return matrix;
}

static nlohmann::json _Vec3(const glm::vec3& v) { return {v.x, v.y, v.z}; }

static glm::vec3 _Vec3(const nlohmann::json& in) {
  return {in.at(0).get<float>(), in.at(1).get<float>(), in.at(2).get<float>()};
}

const CellEdits* EditLayer::Find(const std::string& stage,
                                 uint64_t cell) const {
  const auto cells = stages_.find(stage);
  if (cells == stages_.end())
    return nullptr;
  const auto edits = cells->second.find(cell);
  return edits == cells->second.end() ? nullptr : &edits->second;
}

CellEdits& EditLayer::Cell(const std::string& stage, uint64_t cell) {
  return stages_[stage][cell];
}

InstanceEdit& EditLayer::Edit(const std::string& stage, uint64_t cell,
                              const InstanceKey& key) {
  std::vector<InstanceEdit>& edits = Cell(stage, cell).edits;
  for (InstanceEdit& edit : edits)
    if (edit.object == key.object && edit.index == key.index)
      return edit;
  return edits.emplace_back(InstanceEdit{key.object, key.index, key.anchor});
}

void EditLayer::Revert(const std::string& stage, uint64_t cell,
                       const InstanceKey& key) {
  const auto cells = stages_.find(stage);
  if (cells == stages_.end())
    return;
  const auto found = cells->second.find(cell);
  if (found == cells->second.end())
    return;
  CellEdits& edits = found->second;
  if (key.addition >= 0) {
    if (key.addition < static_cast<int>(edits.additions.size()))
      edits.additions.erase(edits.additions.begin() + key.addition);
  } else {
    std::erase_if(edits.edits, [&](const InstanceEdit& edit) {
      return edit.object == key.object && edit.index == key.index;
    });
  }
  if (edits.edits.empty() && edits.additions.empty())
    cells->second.erase(found);
  if (cells->second.empty())
    stages_.erase(cells);
}

size_t EditLayer::Count() const {
  size_t count = 0;
  for (const auto& [stage, cells] : stages_)
    for (const auto& [cell, edits] : cells)
      count += edits.edits.size() + edits.additions.size();
  return count;
}

std::vector<std::string> EditLayer::Objects(
    const std::string& stage) const {
  std::vector<std::string> objects;
  const auto add = [&](const std::string& object) {
    if (!object.empty() &&
        std::find(objects.begin(), objects.end(), object) == objects.end())
      objects.push_back(object);
  };
  const auto cells = stages_.find(stage);
  if (cells == stages_.end())
    return objects;
  for (const auto& [cell, edits] : cells->second) {
    for (const InstanceEdit& edit : edits.edits)
      add(edit.replacement);
    for (const InstanceAddition& addition : edits.additions)
      add(addition.object);
  }
  return objects;
}

nlohmann::json WriteEditLayer(const EditLayer& layer) {
  nlohmann::json stages = nlohmann::json::object();
  for (const auto& [stage, cells] : layer.stages_) {
    nlohmann::json out = nlohmann::json::object();
    for (const auto& [cell, edits] : cells) {
      nlohmann::json list = nlohmann::json::array();
      for (const InstanceEdit& edit : edits.edits) {
        nlohmann::json e = {{"object", edit.object},
                            {"index", edit.index},
                            {"anchor", _Vec3(edit.anchor)}};
        if (edit.removed)
          e["removed"] = true;
        if (edit.position)
          e["position"] = _Vec3(*edit.position);
        if (edit.rotation)
          e["rotation"] = _Vec3(*edit.rotation);
        if (edit.scale)
          e["scale"] = _Vec3(*edit.scale);
        if (!edit.replacement.empty())
          e["replacement"] = edit.replacement;
        if (edit.seed)
          e["seed"] = *edit.seed;
        list.push_back(std::move(e));
      }
      nlohmann::json additions = nlohmann::json::array();
      for (const InstanceAddition& addition : edits.additions)
        additions.push_back({{"object", addition.object},
                             {"seed", addition.seed},
                             {"matrix", addition.matrix}});
      // Keys are strings in JSON
      out[std::to_string(cell)] = {{"edits", std::move(list)},
                                   {"additions", std::move(additions)}};
    }
    stages[stage] = std::move(out);
  }
  return {{"stages", std::move(stages)}};
}

bool ReadEditLayer(const nlohmann::json& in, EditLayer& layer) {
  layer.stages_.clear();
  const auto stages = in.find("stages");
  if (!in.is_object() || stages == in.end() || !stages->is_object())
    return false;
  try {
    for (const auto& [stage, cells] : stages->items())
      for (const auto& [key, cell] : cells.items()) {
        CellEdits& edits = layer.stages_[stage][std::stoull(key)];
        for (const nlohmann::json& e : cell.value("edits",
                                                  nlohmann::json::array())) {
          InstanceEdit edit;
          edit.object = e.at("object").get<std::string>();
          edit.index = e.at("index").get<uint32_t>();
          edit.anchor = _Vec3(e.at("anchor"));
          edit.removed = e.value("removed", false);
          if (e.contains("position"))
            edit.position = _Vec3(e["position"]);
          if (e.contains("rotation"))
            edit.rotation = _Vec3(e["rotation"]);
          if (e.contains("scale"))
            edit.scale = _Vec3(e["scale"]);
          edit.replacement = e.value("replacement", std::string());
          if (e.contains("seed"))
            edit.seed = e["seed"].get<uint64_t>();
          edits.edits.push_back(std::move(edit));
        }
        for (const nlohmann::json& a :
             cell.value("additions", nlohmann::json::array()))
          edits.additions.push_back({a.at("object").get<std::string>(),
                                     a.value("seed", uint64_t{0}),
                                     a.at("matrix").get<PointMatrix>()});
      }
  } catch (const std::exception&) {
    layer.stages_.clear();
    return false;
  }
  return true;
}

bool LoadEditLayer(const std::filesystem::path& path, EditLayer& layer) {
  layer.stages_.clear();
  std::ifstream file(path);
  if (!file)
    return true;
  const nlohmann::json in = nlohmann::json::parse(file, nullptr, false);
  return !in.is_discarded() && ReadEditLayer(in, layer);
}

bool SaveEditLayer(const std::filesystem::path& path, const EditLayer& layer) {
  std::error_code ec;
  std::filesystem::create_directories(path.parent_path(), ec);
  std::ofstream file(path);
  if (!file)
    return false;
  file << WriteEditLayer(layer).dump(2);
  return static_cast<bool>(file);
}

std::vector<size_t> ApplyEdits(const CellEdits& edits,
                               std::vector<InstancePoint>& points) {
  // Each point's object as placed and its place among that object's
  std::vector<std::string> placed(points.size());
  std::vector<uint32_t> index(points.size());
  std::map<std::string, uint32_t> counts;
  for (size_t i = 0; i < points.size(); ++i) {
    placed[i] = points[i].object;
    index[i] = counts[placed[i]]++;
    points[i].key = {placed[i], index[i],
                     glm::vec3(points[i].matrix[12], points[i].matrix[13],
                               points[i].matrix[14])};
  }

  std::vector<size_t> orphans;
  std::vector<bool> removed(points.size(), false);
  for (size_t e = 0; e < edits.edits.size(); ++e) {
    const InstanceEdit& edit = edits.edits[e];
    size_t i = 0;
    while (i < points.size() &&
           (placed[i] != edit.object || index[i] != edit.index))
      ++i;
    const auto point = points.begin() + i;
    if (point == points.end() ||
        glm::distance(glm::vec3(point->matrix[12], point->matrix[13],
                                point->matrix[14]),
                      edit.anchor) > kEditTolerance) {
      orphans.push_back(e);
      continue;
    }
    if (edit.removed)
      removed[i] = true;
    if (edit.position || edit.rotation || edit.scale) {
      PointTransform transform = Decompose(point->matrix);
      if (edit.position)
        transform.position = *edit.position;
      if (edit.rotation)
        transform.rotation = *edit.rotation;
      if (edit.scale)
        transform.scale = *edit.scale;
      point->matrix = Compose(transform);
    }
    if (!edit.replacement.empty() && edit.replacement != point->object) {
      point->object = edit.replacement;
      point->kind = -1;
    }
    if (edit.seed)
      point->seed = *edit.seed;
  }
  // Points keep their seeds, so the others don't change as one goes
  size_t kept = 0;
  for (size_t i = 0; i < points.size(); ++i)
    if (!removed[i])
      points[kept++] = std::move(points[i]);
  points.resize(kept);
  for (size_t a = 0; a < edits.additions.size(); ++a) {
    const InstanceAddition& addition = edits.additions[a];
    points.push_back({addition.object, addition.matrix, addition.seed, -1,
                      {addition.object, 0, {}, static_cast<int>(a)}});
  }
  return orphans;
}

}  // namespace PCG
