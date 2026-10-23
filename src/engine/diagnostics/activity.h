#ifndef ACTIVITY_H
#define ACTIVITY_H

#include <string>

//---------------------------------------------------------------------------
// ACTIVITY
// What the engine is currently busy with ("Loading texture: x.png").
// Thread-safe: may be set from loader threads.
//---------------------------------------------------------------------------
namespace Diagnostics {

void SetActivity(const std::string& text);
void ClearActivity();

// Copy of the current activity; empty when idle
std::string GetActivity();

}  // namespace Diagnostics

#endif  // ACTIVITY_H