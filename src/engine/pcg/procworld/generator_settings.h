#ifndef GENERATOR_SETTINGS_H
#define GENERATOR_SETTINGS_H

#include <memory>
#include <nlohmann/json.hpp>

#include "engine/pcg/generator_base.h"
#include "engine/pcg/points/point_source.h"

// Planet, craft, region, spawner and graph generator settings as JSON: what
// scenes and generator profiles store
namespace ProcWorld {

// The generator's settings with its "type"; null for other generators
nlohmann::json WriteSettings(const PCG::Generator& generator);

// Sets the generator's settings from JSON; missing values stay as they are
void ReadSettings(PCG::Generator& generator, const nlohmann::json& in);

// A new generator of the JSON's "type" with its settings; null if unknown
std::unique_ptr<PCG::Generator> CreateGenerator(const nlohmann::json& in);

// A new point source of the JSON's "type" with its settings; null if
// unknown
std::unique_ptr<PCG::PointSource> CreatePointSource(const nlohmann::json& in);

}  // namespace ProcWorld

#endif  // GENERATOR_SETTINGS_H
