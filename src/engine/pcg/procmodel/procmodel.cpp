#include "procmodel.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <sstream>

#include "engine/core/logger.h"

#include "engine/content/cache/image_cache.h"
#include "engine/content/io/json.h"
#include "engine/content/loaders/mesh_loader.h"
#include "engine/context/engine_context.h"

#include "engine/pcg/pipeline/linear_pipeline.h"
#include "engine/pcg/pipeline/pipeline_descriptor_parser.h"

#include "engine/pcg/procmodel/descriptor/model_descriptor_parser.h"
#include "engine/pcg/procmodel/generator/descriptor_resolver.h"
#include "engine/pcg/procmodel/generator/model_generator.h"
#include "engine/pcg/procmodel/model_graph/model_graph_builder.h"
#include "engine/pcg/procmodel/procmodel_resource.h"
#include "engine/pcg/procmodel/validation/procmodel_evaluation.h"

#include "content_json.h"

namespace ProcModel {

Subsystem::Subsystem() = default;

void Subsystem::Init() {
  RegisterModelOperations(operation_registry_);

  Logger::getInstance().Log(
      LogLevel::Info,
      "[ProcModel::Subsystem] Init complete, registered " +
          std::to_string(
              operation_registry_.IsRegistered("vertex_deform") +
              operation_registry_.IsRegistered("duplicate_instances")) +
          " operations");
}

ResourceID Subsystem::LoadArchetype(const std::string& descriptor_path) {
  auto cache_it = procmodel_cache_.find(descriptor_path);
  if (cache_it == procmodel_cache_.end() &&
      AddArchetype(descriptor_path, ReadArchetype(descriptor_path)))
    cache_it = procmodel_cache_.find(descriptor_path);
  return cache_it != procmodel_cache_.end() ? cache_it->second : 0;
}

std::shared_ptr<ProcModelResource> Subsystem::ReadArchetype(
    const std::string& descriptor_path) const {
  auto resource = std::make_shared<ProcModelResource>();

  auto json_opt = Content::ReadJsonFile(descriptor_path);
  if (!json_opt)
    return nullptr;

  if (!ProcModel::ModelDescriptorParser::FromJson(*json_opt,
                                                  resource->GetDescriptor())) {
    Logger::getInstance().Log(
        LogLevel::Error, "[ProcModel::Subsystem] Failed to parse descriptor: " +
                             descriptor_path);
    return nullptr;
  }

  // A relative model path is beside the descriptor (else as given)
  {
    std::string& model_path = resource->GetDescriptor().path;
    const std::filesystem::path beside =
        std::filesystem::path(descriptor_path).parent_path() / model_path;
    if (std::filesystem::path(model_path).is_relative() &&
        std::filesystem::exists(beside))
      model_path = beside.string();
    // Texture descriptions are always beside it
    for (auto& [material, path] : resource->GetDescriptor().textures)
      path = (std::filesystem::path(descriptor_path).parent_path() / path)
                 .string();
  }

  PCG::ParsePipelineDescriptor(*json_opt, resource->GetPipelineDescriptor());

  const auto& descriptor = resource->GetDescriptor();
  Content::SceneData scene = Content::MeshLoader::LoadScene(descriptor.path);
  if (scene.mesh_data.empty()) {
    Logger::getInstance().Log(
        LogLevel::Error,
        "[ProcModel::Subsystem] Failed to load scene: " + descriptor.path);
    return nullptr;
  }

  resource->GetGraph() =
      ProcModel::ModelGraphBuilder::Build(scene, descriptor.path);

  auto resolve_result = ProcModel::DescriptorResolver::Resolve(
      resource->GetGraph(), resource->GetDescriptor());
  if (!resolve_result.success) {
    Logger::getInstance().Log(
        LogLevel::Error, "[ProcModel::Subsystem] Descriptor resolution failed");
    return nullptr;
  }

  {
    std::vector<std::string> errors;
    auto compiled = PCG::LinearPipeline::Compile(
        resource->GetPipelineDescriptor(), operation_registry_, errors);
    Logger::getInstance().Log(
        LogLevel::Info,
        "[ProcModel::Subsystem] Pipeline compiled with " +
            std::to_string(resource->GetPipelineDescriptor().entries.size()) +
            " entries");
    if (!compiled) {
      for (const auto& err : errors) {
        Logger::getInstance().Log(
            LogLevel::Error,
            "[ProcModel::Subsystem] Pipeline compilation: " + err);
      }
      return nullptr;
    }
    resource->GetPipeline() = std::move(*compiled);
  }
  return resource;
}

std::shared_ptr<const ProcModelResource> Subsystem::AddArchetype(
    const std::string& descriptor_path,
    std::shared_ptr<ProcModelResource> read) {
  if (auto loaded = LoadedArchetype(descriptor_path))
    return loaded;
  if (!read)
    return nullptr;
  if (!read->GetGraph().UploadMeshes()) {
    Logger::getInstance().Log(LogLevel::Error,
                              "[ProcModel::Subsystem] Failed to upload meshes");
    return nullptr;
  }
  read->SetResolved(true);
  procmodel_cache_[descriptor_path] =
      EngineContext::resourceManager().Add(descriptor_path, read);
  return read;
}

std::shared_ptr<const ProcModelResource> Subsystem::LoadedArchetype(
    const std::string& descriptor_path) const {
  const auto it = procmodel_cache_.find(descriptor_path);
  if (it == procmodel_cache_.end())
    return nullptr;
  return EngineContext::resourceManager()
      .GetResourceAs<ProcModel::ProcModelResource>(it->second);
}

std::optional<ProcModel::InstanceData> Subsystem::GenerateInstance(
    const std::string& descriptor_path, uint64_t seed) {
  ResourceID resource_id = LoadArchetype(descriptor_path);
  if (resource_id == 0)
    return std::nullopt;

  auto& resource_mgr = EngineContext::resourceManager();
  auto resource =
      resource_mgr.GetResourceAs<ProcModel::ProcModelResource>(resource_id);
  if (!resource || !resource->IsResolved()) {
    Logger::getInstance().Log(LogLevel::Error,
                              "[ProcModel::Subsystem] Resource not ready");
    return std::nullopt;
  }

  std::vector<ProcModelSample> attempt_samples;
  auto output = ProcModel::ModelGenerator::Generate(
      resource->GetGraph(), resource->GetDescriptor(), resource->GetPipeline(),
      operation_registry_, seed, 10, &attempt_samples);

  // Route samples to log + ERA buffer
  for (const auto& s : attempt_samples) {
    ValidationLog().Write(s);
  }
  samples_.insert(samples_.end(),
                  std::make_move_iterator(attempt_samples.begin()),
                  std::make_move_iterator(attempt_samples.end()));

  if (!output) {
    Logger::getInstance().Log(LogLevel::Warning,
                              "[ProcModel::Subsystem] Generation failed after "
                              "max retries for seed: " +
                                  std::to_string(seed));
    return std::nullopt;
  }

  return output;
}

// TODO: move load/build/resolve steps into a background ResourcePipe task,
// and the GPU upload into a UseRenderThread task. This will be needed
// so the pipeline doesn't block the frame
// TODO: Instantiate() currently ignores InstanceData::instance_geometry,
// so ECS-instantiated entities render undeformed source meshes.
// We need to wire instance_geometry through Instantiate when scene placement
// UX is needed. No need to refactor Editor preview path
ProcModel::ModelInstantiator::InstantiateResult Subsystem::SpawnInstance(
    const std::string& descriptor_path, uint64_t seed, Entity parent,
    const Worldgen::TextureInput* planet) {

  ResourceID resource_id = LoadArchetype(descriptor_path);
  if (resource_id == 0)
    return {};

  auto& resource_mgr = EngineContext::resourceManager();
  auto resource =
      resource_mgr.GetResourceAs<ProcModel::ProcModelResource>(resource_id);
  if (!resource || !resource->IsResolved()) {
    Logger::getInstance().Log(LogLevel::Error,
                              "[ProcModel::Subsystem] Resource not ready");
    return {};
  }

  // Generate instance
  std::vector<ProcModelSample> attempt_samples;
  auto resolved = ProcModel::ModelGenerator::Generate(
      resource->GetGraph(), resource->GetDescriptor(), resource->GetPipeline(),
      operation_registry_, seed, 10, &attempt_samples);

  // Log every sample produced by this request, accumulate for ERA
  for (const auto& s : attempt_samples) {
    ValidationLog().Write(s);
  }
  samples_.insert(samples_.end(),
                  std::make_move_iterator(attempt_samples.begin()),
                  std::make_move_iterator(attempt_samples.end()));

  if (!resolved) {
    Logger::getInstance().Log(LogLevel::Warning,
                              "[ProcModel::Subsystem] Generation failed after "
                              "max retries for seed: " +
                                  std::to_string(seed));
    return {};
  }

  return Instantiate(descriptor_path, *resource, *resolved, parent, planet);
}

std::shared_ptr<const ProcModelResource> Subsystem::Archetype(
    const std::string& descriptor_path) {
  const ResourceID id = LoadArchetype(descriptor_path);
  if (id == 0)
    return nullptr;
  auto resource = EngineContext::resourceManager()
                      .GetResourceAs<ProcModel::ProcModelResource>(id);
  if (!resource || !resource->IsResolved())
    return nullptr;
  return resource;
}

std::optional<ProcModel::InstanceData> Subsystem::Generate(
    const ProcModelResource& archetype, uint64_t seed) const {
  return ProcModel::ModelGenerator::Generate(
      archetype.GetGraph(), archetype.GetDescriptor(), archetype.GetPipeline(),
      operation_registry_, seed, 10);
}

ModelInstantiator::InstantiateResult Subsystem::Instantiate(
    const std::string& descriptor_path, const ProcModelResource& archetype,
    const ProcModel::InstanceData& instance, Entity parent,
    const Worldgen::TextureInput* planet) {
  const std::vector<MaterialHandle>* materials =
      planet ? TexturedMaterials(descriptor_path, archetype.GetDescriptor(),
                                 archetype.GetGraph(), *planet)
             : nullptr;
  return ProcModel::ModelInstantiator::Instantiate(
      instance.model, archetype.GetGraph(), parent, materials);
}

static uint64_t _Hash(uint64_t hash, const void* data, size_t size) {
  const auto* bytes = static_cast<const uint8_t*>(data);
  for (size_t i = 0; i < size; ++i)
    hash = (hash ^ bytes[i]) * 0x100000001b3ull;
  return hash;
}

// A blended normal map's z from its x and y, which also takes two-channel
// maps (z 0)
static Worldgen::TextureImage _RebuildNormal(Worldgen::TextureImage image) {
  for (size_t i = 0; i + 3 < image.rgba.size(); i += 4) {
    float x = image.rgba[i] / 127.5f - 1.0f, y = image.rgba[i + 1] / 127.5f - 1.0f;
    const float length = std::sqrt(x * x + y * y);
    if (length > 1.0f)
      x /= length, y /= length;
    const float z = std::sqrt(std::max(0.0f, 1.0f - x * x - y * y));
    image.rgba[i] = static_cast<uint8_t>(std::lround((x + 1.0f) * 127.5f));
    image.rgba[i + 1] = static_cast<uint8_t>(std::lround((y + 1.0f) * 127.5f));
    image.rgba[i + 2] = static_cast<uint8_t>(std::lround((z + 1.0f) * 127.5f));
  }
  return image;
}

const std::vector<MaterialHandle>* Subsystem::TexturedMaterials(
    const std::string& descriptor_path, const ModelDescriptor& descriptor,
    const ModelGraph& graph, const Worldgen::TextureInput& planet) {
  if (descriptor.textures.empty() || planet.palettes.empty())
    return nullptr;

  // Made once per kind of model and planet, so they all share the textures
  uint64_t planet_key =
      _Hash(0xcbf29ce484222325ull, &planet.seed, sizeof(planet.seed));
  planet_key = _Hash(planet_key, planet.palettes.data(),
                     planet.palettes.size() * sizeof(Worldgen::PlanetPalette));
  auto [it, added] = model_materials_.try_emplace(
      descriptor_path + "|" + std::to_string(planet_key));
  std::vector<MaterialHandle>& materials = it->second;
  if (!added)
    return &materials;
  materials = graph.material_handles;

  Worldgen::TextureInput input = planet;
  input.seed = _Hash(planet.seed, descriptor.model_id.data(),
                     descriptor.model_id.size());
  auto& images = Content::ImageCache::Instance();
  for (size_t i = 0; i < graph.materials.size() && i < materials.size(); ++i) {
    const Content::MaterialData& authored = graph.materials[i];
    const auto path = descriptor.textures.find(authored.name);
    if (path == descriptor.textures.end())
      continue;
    const Worldgen::TextureContent* content = LoadTextureContent(path->second);
    if (!content)
      continue;

    // Equal choices give equal names: one image for all the planets they fit
    const Worldgen::ProceduralTexture texture =
        Worldgen::BuildProceduralTexture(*content, input);
    const std::string key = "proctexture:" + path->second + "#" + texture.name;
    const std::filesystem::path folder =
        std::filesystem::path(path->second).parent_path();
    const auto load = [&](const std::string& file) {
      return LoadLayerImage((folder / file).string());
    };
    // The image under `key`, made once; false when it has nothing to show
    const auto put = [&](const std::string& image_key, auto make) {
      if (images.Contains(image_key))
        return images.Get(image_key).Valid();
      Worldgen::TextureImage image = make();
      Content::ImageData data;
      if (image.width > 0 && image.width <= 0xffff && image.height <= 0xffff) {
        data.width = static_cast<uint16_t>(image.width);
        data.height = static_cast<uint16_t>(image.height);
        data.pixels = std::move(image.rgba);
      }
      const bool valid = data.Valid();
      images.Put(image_key, std::move(data));
      return valid;
    };
    if (!put(key, [&] { return Worldgen::CompositeTexture(texture, load); }))
      continue;  // nothing loaded: keep the authored material

    auto [material, made] = textured_materials_.try_emplace(
        descriptor_path + "|" + authored.name + "|" + key);
    if (made) {
      Content::MaterialData textured = authored;
      textured.albedo_map = key;
      textured.base_color = glm::vec4(1.0f);  // the texture holds the colour
      // The maps the layers have; masks in the shader's metallic-roughness
      // layout (g roughness, b metallic)
      if (put(key + "#normal", [&] {
            return _RebuildNormal(Worldgen::CompositeMap(
                texture, Worldgen::TextureMap::Normal, load));
          }))
        textured.normal_map = key + "#normal";
      if (put(key + "#masks", [&] {
            return Worldgen::CompositeMap(texture, Worldgen::TextureMap::Masks,
                                          load);
          }))
        textured.metallic_roughness_map = key + "#masks";
      if (put(key + "#occlusion", [&] {
            return Worldgen::CompositeMap(
                texture, Worldgen::TextureMap::Occlusion, load);
          }))
        textured.ao_map = key + "#occlusion";
      material->second = MaterialRegistry::Instance().Register(textured);
      Logger::getInstance().Log(
          LogLevel::Info,
          "[ProcModel::Subsystem] " + descriptor.model_id + " " +
              authored.name + ": texture " + texture.name +
              (textured.normal_map.empty() ? "" : ", normal") +
              (textured.metallic_roughness_map.empty() ? "" : ", masks") +
              (textured.ao_map.empty() ? "" : ", occlusion"));
    }
    materials[i] = material->second;
  }
  return &materials;
}

const Worldgen::TextureContent* Subsystem::LoadTextureContent(
    const std::string& path) {
  auto [it, added] = texture_contents_.try_emplace(path);
  if (added) {
    std::ifstream file(path);
    std::stringstream text;
    text << file.rdbuf();
    Worldgen::TextureContent content;
    std::string error = "can't open it";
    if (file && Worldgen::Json::LoadTextureContent(text.str(), content, &error))
      it->second = std::move(content);
    else
      Logger::getInstance().Log(
          LogLevel::Error, "[ProcModel::Subsystem] " + path + ": " + error);
  }
  return it->second ? &*it->second : nullptr;
}

const Worldgen::TextureImage* Subsystem::LoadLayerImage(
    const std::string& path) {
  auto [it, added] = layer_images_.try_emplace(path);
  if (added) {
    // Kept here as worldgen reads it, not in the image cache too
    auto& images = Content::ImageCache::Instance();
    const Content::ImageData& data = images.Get(path);
    if (data.Valid() && data.format == Content::ImageFormat::RGBA8)
      it->second = {data.width, data.height, data.pixels};
    images.Evict(path);
  }
  return it->second.width > 0 ? &it->second : nullptr;
}

// Validation logger (one per session)
ValidationLogger& Subsystem::ValidationLog() {
  if (!validation_logger_)
    validation_logger_ = std::make_unique<ValidationLogger>();
  return *validation_logger_;
}

void Subsystem::ResetSamples() {
  samples_.clear();
  if (validation_logger_) {
    validation_logger_.reset();
  }
}

void Subsystem::Shutdown() {

  // Generate ERA metrics at shutdown
  // Only captures metrics for the current session
  // ! Should call one function instead of exposing the log logic here
  if (!validation_logger_)
    return;

  const auto& samples = samples_;
  if (samples.empty())
    return;

  ProcModelERA era = ProcModelEval::Compute(samples);

  std::string report_path = (std::filesystem::current_path() / "logs" /
                             "procmodel" / "era_metric.json")
                                .string();

  if (!ProcModelEval::WriteReport(era, report_path)) {
    Logger::getInstance().Log(
        LogLevel::Error, "[ProcModel::Subsystem] Failed to write ERA report");
  } else {
    Logger::getInstance().Log(
        LogLevel::Info,
        "[ProcModel::Subsystem] ERA report written to " + report_path);
  }
}

}  // namespace ProcModel