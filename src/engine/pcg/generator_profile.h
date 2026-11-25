#ifndef GENERATOR_PROFILE_H
#define GENERATOR_PROFILE_H

#include <filesystem>
#include <nlohmann/json.hpp>

// A generator profile is an asset (JSON, .generator) holding a generator's
// settings, shared by any number of generators. Each generator keeps what it
// sets differently, its overrides, as UE's graph instances keep parameter
// overrides: its settings are the profile with its overrides merged in
namespace PCG {

// The profile's settings; null when it can't be read
nlohmann::json LoadProfile(const std::filesystem::path& path);

bool SaveProfile(const std::filesystem::path& path,
                 const nlohmann::json& settings);

// What settings change from the profile, as a JSON merge patch (RFC 7386):
// profile.merge_patch(overrides) gives settings back. Arrays are whole values
nlohmann::json ProfileOverrides(const nlohmann::json& profile,
                                const nlohmann::json& settings);

}  // namespace PCG

#endif  // GENERATOR_PROFILE_H
