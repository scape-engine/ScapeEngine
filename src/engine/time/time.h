//
//
//
// -----------------------------------------------------------
// This is a modified version of nuro's time
// https://github.com/jonkwl/nuro/blob/main/nuro-core/time
// -----------------------------------------------------------
//
//
//

#ifndef TIME_H
#define TIME_H

namespace Time {
// Calculate times for current frame (handled by engine context)
void Step(double delta_time);

// Returns the current application time
double Now();

// Returns the current application time
float Nowf();

// Returns the application time of last frame
double Last();

// Returns the application time of last frame
float Lastf();

// Returns the current delta time not affected by the time scale
double UnscaledDelta();

// Returns the current delta time not affected by the time scale
double UnscaledDeltaf();

// Returns the current delta time
double Delta();

// Returns the current delta time
float Deltaf();

// Returns the current time scale
double GetTimeScale();

// Sets the time scale
void SetTimeScale(double time_scale);

// The world clock, in seconds: advances by the scaled delta time. It starts at
// the Unix time of launch, so machines with set clocks agree until it is
// scaled or set
// TODO: saved with the scene, and shared by a world's peers
double World();

// Sets the world clock
void SetWorld(double seconds);
};  // namespace Time

#endif  // TIME_H
