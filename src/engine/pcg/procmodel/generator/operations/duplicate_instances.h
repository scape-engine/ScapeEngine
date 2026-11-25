#ifndef DUPLICATE_INSTANCES_H
#define DUPLICATE_INSTANCES_H

#include <cstdint>
#include <string>

#include <glm/vec3.hpp>

#include "engine/pcg/pipeline/operation_data.h"

namespace PCG {
class OperationRegistry;
}

namespace ProcModel {

// Copies of a group's parts (min_count to max_count, the original included),
// each moved and turned in the part's own frame by a uniform draw within
// [-offset, offset] and [-rotation, rotation] (pitch, yaw, roll) so they don't
// sit on the original. Copies are leaves: groups the original activates are
// not copied with it
struct DuplicateInstancesData : public PCG::OperationData {
  std::string target_group_id;
  std::uint32_t min_count = 1;
  std::uint32_t max_count = 1;
  glm::vec3 offset = glm::vec3(0.0f);
  glm::vec3 rotation = glm::vec3(0.0f);  // radians; degrees in JSON
};

void RegisterDuplicateInstancesOperation(PCG::OperationRegistry& registry);

}  // namespace ProcModel

#endif  // DUPLICATE_INSTANCES_H