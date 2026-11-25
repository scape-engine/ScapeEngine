// Checks must run in release builds too
#undef NDEBUG
#include <cassert>

#include <filesystem>
#include <iostream>

#include "engine/pcg/generator_profile.h"
#include "engine/pcg/procworld/generator_settings.h"
#include "engine/pcg/procworld/planet_generator.h"

namespace Tests {

// A generator keeps only what it sets over its profile, and merging that back
// into the profile gives its settings
void TestGeneratorProfile() {
  ProcWorld::PlanetGenerator planet;
  planet.seed = 7;
  planet.objects = {"objects/rock.object"};
  const nlohmann::json profile = ProcWorld::WriteSettings(planet);

  planet.seed = 9;
  planet.look.roughness = 0.5f;
  planet.creatures.animation.sample_distance = 10.0f;
  const nlohmann::json settings = ProcWorld::WriteSettings(planet);

  const nlohmann::json overrides = PCG::ProfileOverrides(profile, settings);
  assert(overrides.size() == 3);
  assert(overrides.at("seed") == 9);
  assert(overrides.at("look").size() == 1);
  assert(overrides.at("creatures").at("animation").size() == 1);
  assert(PCG::ProfileOverrides(profile, profile).empty());

  nlohmann::json merged = profile;
  merged.merge_patch(overrides);
  assert(merged == settings);
  const auto copy = ProcWorld::CreateGenerator(merged);
  assert(copy && ProcWorld::WriteSettings(*copy) == settings);

  // Without a profile every setting is an override
  assert(PCG::ProfileOverrides(nullptr, settings) == settings);

  const auto folder =
      std::filesystem::temp_directory_path() / "scape_generator_profile";
  assert(PCG::SaveProfile(folder / "planet.generator", profile));
  assert(PCG::LoadProfile(folder / "planet.generator") == profile);
  assert(PCG::LoadProfile(folder / "missing.generator").is_null());
  std::filesystem::remove_all(folder);

  std::cout << "Generator profile tests passed" << std::endl;
}

}  // namespace Tests
