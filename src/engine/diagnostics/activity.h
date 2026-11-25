#ifndef ACTIVITY_H
#define ACTIVITY_H

#include <cstdint>
#include <string>

//---------------------------------------------------------------------------
// ACTIVITY
// Background work the engine is busy with ("Loading Sponza.gltf"), shown in
// the status bar. Thread-safe: tasks may begin and end on loader threads.
//---------------------------------------------------------------------------
namespace Diagnostics {

using TaskId = uint64_t;

TaskId BeginTask(const std::string& name);
void EndTask(TaskId id);

// Oldest running task, with "(+N)" when more are running; empty when idle
std::string GetActivity();

}  // namespace Diagnostics

#endif  // ACTIVITY_H
