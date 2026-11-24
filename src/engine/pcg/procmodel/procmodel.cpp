#include "procmodel.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <sstream>

#include "engine/core/logger.h"

#include "engine/content/cache/image_cache.h"
#include "engine/content/loaders/image_loader.h"
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
#include "engine/renderer/model/draw_settings.h"
#include "engine/renderer/model/lod_generation.h"
#include "engine/scene/model_entity_factory.h"

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
  if (descriptor.poses) {
    ProcModel::ModelGraph& graph = resource->GetGraph();
    const auto& clips = graph.skeletal.clips;
    const auto clip =
        std::find_if(clips.begin(), clips.end(), [&](const AnimationClip& c) {
          return c.name == descriptor.poses->clip;
        });
    const int index = static_cast<int>(clip - clips.begin());
    if (clip != clips.end() && BuildAdditive(graph.skeletal, index)) {
      graph.poses = descriptor.poses;
      graph.pose_clip = index;
    } else {
      Logger::getInstance().Log(LogLevel::Warning,
                                "[ProcModel::Subsystem] No pose clip '" +
                                    descriptor.poses->clip + "' in " +
                                    descriptor.path);
    }
  }

  // Each gait's clip, and the pace its planted feet keep where none is given
  if (descriptor.gaits) {
    ProcModel::ModelGraph& graph = resource->GetGraph();
    graph.gaits = std::make_shared<GaitSet>(*descriptor.gaits);
    const auto& clips = graph.skeletal.clips;
    for (GaitSet::Gait& gait : graph.gaits->gaits) {
      const auto clip =
          std::find_if(clips.begin(), clips.end(), [&](const AnimationClip& c) {
            return c.name == gait.clip;
          });
      if (clip == clips.end()) {
        Logger::getInstance().Log(LogLevel::Warning,
                                  "[ProcModel::Subsystem] No gait clip '" +
                                      gait.clip + "' in " + descriptor.path);
        continue;
      }
      gait.clip_index = static_cast<int>(clip - clips.begin());
      if (gait.pace <= 0.0f)
        gait.pace = glm::length(StrideVelocity(graph.skeletal, gait.clip_index));
    }
  }

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
  ApplyDraw(read->GetGraph());
  read->SetResolved(true);
  procmodel_cache_[descriptor_path] =
      EngineContext::resourceManager().Add(descriptor_path, read);
  return read;
}

void Subsystem::ApplyDraw(const ModelGraph& graph) {
  const DrawSettings settings = DrawSettings::Read(graph.source_path);
  std::vector<std::vector<const Mesh*>> lods;
  for (const DrawSettings::Lod& lod : settings.lods) {
    std::vector<const Mesh*>& meshes = lods.emplace_back();
    if (!lod.source.empty()) {
      for (const auto& [mesh, material] :
           ModelEntityFactory::Meshes(lod.source, root_))
        meshes.push_back(mesh);
      continue;
    }
    auto& generated = generated_lods_[graph.source_path + "#lod" +
                                      std::to_string(lod.detail)];
    if (generated.empty())
      for (const Geometry::MeshData& mesh : graph.mesh_data)
        generated.push_back(std::make_unique<Mesh>(
            LodGeneration::Simplify(mesh, lod.detail)));
    for (const auto& mesh : generated)
      meshes.push_back(mesh.get());
  }
  for (size_t i = 0; i < graph.gpu_meshes.size(); ++i) {
    std::vector<Mesh::Lod> chain;
    for (size_t l = 0; l < lods.size(); ++l)
      if (i < lods[l].size())
        chain.push_back({settings.lods[l].distance, lods[l][i]});
    graph.gpu_meshes[i]->SetDraw(settings.fade_start, settings.fade_end,
                                 std::move(chain));
  }
}

void Subsystem::ReloadDrawSettings(const std::filesystem::path& model) {
  for (const auto& [descriptor_path, id] : procmodel_cache_) {
    const auto archetype = LoadedArchetype(descriptor_path);
    std::error_code error;
    if (archetype && std::filesystem::equivalent(
                         archetype->GetGraph().source_path, model, error))
      ApplyDraw(archetype->GetGraph());
  }
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

  return Instantiate(*resolved, *resource, parent,
                     planet ? TexturedMaterials(descriptor_path, *resource, *planet)
                            : nullptr);
}

ProcModel::ModelInstantiator::InstantiateResult Subsystem::SpawnInstance(
    const std::string& descriptor_path, const ProcModel::InstanceData& instance,
    Entity parent, const Worldgen::TextureInput* planet) {
  const auto resource = Archetype(descriptor_path);
  if (!resource)
    return {};
  return Instantiate(instance, *resource, parent,
                     planet ? TexturedMaterials(descriptor_path, *resource,
                                                *planet)
                            : nullptr);
}

std::optional<std::vector<ProcModel::ModelInstantiator::Part>>
Subsystem::InstanceParts(const std::string& descriptor_path,
                         const ProcModel::InstanceData& instance,
                         const Worldgen::TextureInput* planet) {
  const auto resource = Archetype(descriptor_path);
  if (!resource)
    return std::vector<ProcModel::ModelInstantiator::Part>();
  return ProcModel::ModelInstantiator::Parts(
      instance.model, resource->GetGraph(),
      planet ? TexturedMaterials(descriptor_path, *resource, *planet)
             : nullptr);
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
    const ProcModel::InstanceData& instance, const ProcModelResource& archetype,
    Entity parent, const std::vector<MaterialHandle>* materials) {
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

// Halved (2x2 averages) until no side passes `max_size` (0: as it is)
static Worldgen::TextureImage _Shrink(Worldgen::TextureImage image,
                                      int max_size) {
  while (max_size > 0 && std::max(image.width, image.height) > max_size &&
         image.width > 1 && image.height > 1) {
    const int w = image.width / 2, h = image.height / 2;
    std::vector<uint8_t> out(static_cast<size_t>(w) * h * 4);
    for (int y = 0; y < h; ++y)
      for (int x = 0; x < w; ++x)
        for (int c = 0; c < 4; ++c) {
          const auto at = [&](int dx, int dy) {
            return image.rgba[(static_cast<size_t>(2 * y + dy) * image.width +
                               2 * x + dx) * 4 + c];
          };
          out[(static_cast<size_t>(y) * w + x) * 4 + c] = static_cast<uint8_t>(
              (at(0, 0) + at(1, 0) + at(0, 1) + at(1, 1) + 2) / 4);
        }
    image = {w, h, std::move(out)};
  }
  return image;
}

// The descriptor and the input's seed and palettes
static std::string _TextureKey(const std::string& descriptor_path,
                               const Worldgen::TextureInput& input) {
  uint64_t key = _Hash(0xcbf29ce484222325ull, &input.seed, sizeof(input.seed));
  key = _Hash(key, input.palettes.data(),
              input.palettes.size() * sizeof(Worldgen::PlanetPalette));
  return descriptor_path + "|" + std::to_string(key);
}

const std::vector<MaterialHandle>* Subsystem::TexturedMaterials(
    const std::string& descriptor_path, const ProcModelResource& archetype,
    const Worldgen::TextureInput& planet) {
  const ModelDescriptor& descriptor = archetype.GetDescriptor();
  if (descriptor.textures.empty() || planet.palettes.empty())
    return nullptr;
  // Made once per kind of model and planet, so they all share the textures
  Worldgen::TextureInput input = planet;
  input.seed = _Hash(planet.seed, descriptor.model_id.data(),
                     descriptor.model_id.size());
  if (auto it = model_materials_.find(_TextureKey(descriptor_path, input));
      it != model_materials_.end())
    return &it->second;
  return ApplyTextures(descriptor_path, archetype,
                       PrepareTextures(descriptor_path, descriptor, input));
}

Subsystem::TextureSet Subsystem::PrepareTextures(
    const std::string& descriptor_path, const ModelDescriptor& descriptor,
    const Worldgen::TextureInput& input, int max_size,
    TextureClaims* claims) {
  TextureSet set;
  set.key = _TextureKey(descriptor_path, input);
  if (descriptor.textures.empty() || input.palettes.empty())
    return set;
  for (const auto& [material, path] : descriptor.textures) {
    const Worldgen::TextureContent* content = LoadTextureContent(path);
    if (!content)
      continue;
    // Equal choices give equal names: one image for all the inputs they fit.
    // The maps don't take the colours: equal picks share them
    const Worldgen::ProceduralTexture texture =
        Worldgen::BuildProceduralTexture(*content, input);
    uint64_t picks = 0xcbf29ce484222325ull;
    for (const Worldgen::TextureChoice& choice : texture.choices)
      for (const std::string* name :
           {&choice.layer, &choice.group, &choice.option_name})
        picks = _Hash(picks, name->data(), name->size() + 1);
    const std::string key = "proctexture:" + path + "#" + texture.name;
    const std::string maps = "proctexture:" + path + "#" +
                             std::to_string(picks) + "@" +
                             std::to_string(max_size);
    const std::filesystem::path folder =
        std::filesystem::path(path).parent_path();
    // The layers at most `max_size` too: compositing costs by the texel
    const auto load = [&](const std::string& file) {
      return LoadLayerImage((folder / file).string(), max_size);
    };
    // The image under `image_key` unless another set claimed it
    const auto make = [&](const std::string& image_key, auto composite) {
      TextureSet::Image image{image_key, {}};
      if (claims && !claims->Claim(image_key))
        return image;
      image.image = _Shrink(composite(), max_size);
      return image;
    };

    TextureSet::Material made;
    made.albedo = make(key + "@" + std::to_string(max_size), [&] {
      return Worldgen::CompositeTexture(texture, load);
    });
    made.normal = make(maps + "#normal", [&] {
      return _RebuildNormal(
          Worldgen::CompositeMap(texture, Worldgen::TextureMap::Normal, load));
    });
    made.masks = make(maps + "#masks", [&] {
      return Worldgen::CompositeMap(texture, Worldgen::TextureMap::Masks, load);
    });
    made.occlusion = make(maps + "#occlusion", [&] {
      return Worldgen::CompositeMap(texture, Worldgen::TextureMap::Occlusion,
                                    load);
    });
    made.name = material;
    set.materials.push_back(std::move(made));
  }
  return set;
}

void Subsystem::StoreTextureImages(TextureSet& set) {
  auto& images = Content::ImageCache::Instance();
  for (TextureSet::Material& made : set.materials)
    for (TextureSet::Image* image :
         {&made.albedo, &made.normal, &made.masks, &made.occlusion}) {
      // An empty one is left to the set that claimed it, or has nothing
      if (image->image.width > 0 && image->image.width <= 0xffff &&
          image->image.height <= 0xffff && !images.Contains(image->key)) {
        Content::ImageData data;
        data.width = static_cast<uint16_t>(image->image.width);
        data.height = static_cast<uint16_t>(image->image.height);
        data.pixels = std::move(image->image.rgba);
        images.Put(image->key, std::move(data));
      }
      image->image = {};
    }
}

const std::vector<MaterialHandle>* Subsystem::ApplyTextures(
    const std::string& descriptor_path, const ProcModelResource& archetype,
    TextureSet set) {
  auto [it, added] = model_materials_.try_emplace(set.key);
  std::vector<MaterialHandle>& materials = it->second;
  if (!added)
    return &materials;
  const ModelGraph& graph = archetype.GetGraph();
  const ModelDescriptor& descriptor = archetype.GetDescriptor();
  materials = graph.material_handles;

  StoreTextureImages(set);
  auto& images = Content::ImageCache::Instance();
  // False when the image has nothing to show
  const auto put = [&](const TextureSet::Image& image) {
    return images.Contains(image.key) && images.Get(image.key).Valid();
  };
  for (TextureSet::Material& made : set.materials)
    for (size_t i = 0; i < graph.materials.size() && i < materials.size();
         ++i) {
      const Content::MaterialData& authored = graph.materials[i];
      if (authored.name != made.name)
        continue;
      if (!put(made.albedo))
        continue;  // nothing loaded: keep the authored material
      auto [material, created] = textured_materials_.try_emplace(
          descriptor_path + "|" + authored.name + "|" + made.albedo.key + "|" +
          made.normal.key);
      if (created) {
        Content::MaterialData textured = authored;
        textured.albedo_map = made.albedo.key;
        textured.base_color = glm::vec4(1.0f);  // the texture holds the colour
        // The maps the layers have; masks in the shader's metallic-roughness
        // layout (g roughness, b metallic)
        if (put(made.normal))
          textured.normal_map = made.normal.key;
        if (put(made.masks))
          textured.metallic_roughness_map = made.masks.key;
        if (put(made.occlusion))
          textured.ao_map = made.occlusion.key;
        material->second = MaterialRegistry::Instance().Register(textured);
        Logger::getInstance().Log(
            LogLevel::Debug,
            "[ProcModel::Subsystem] " + descriptor.model_id + " " +
                authored.name + ": texture " + made.albedo.key +
                (textured.normal_map.empty() ? "" : ", normal") +
                (textured.metallic_roughness_map.empty() ? "" : ", masks") +
                (textured.ao_map.empty() ? "" : ", occlusion"));
      }
      materials[i] = material->second;
    }
  return &materials;
}

// Read without the lock, so threads read apart; one that reads a path
// another kept meanwhile drops its copy
const Worldgen::TextureContent* Subsystem::LoadTextureContent(
    const std::string& path) {
  {
    std::lock_guard lock(texture_mutex_);
    if (auto it = texture_contents_.find(path); it != texture_contents_.end())
      return it->second ? &*it->second : nullptr;
  }
  std::optional<Worldgen::TextureContent> read;
  std::ifstream file(path);
  std::stringstream text;
  text << file.rdbuf();
  Worldgen::TextureContent content;
  std::string error = "can't open it";
  if (file && Worldgen::Json::LoadTextureContent(text.str(), content, &error))
    read = std::move(content);
  else
    Logger::getInstance().Log(
        LogLevel::Error, "[ProcModel::Subsystem] " + path + ": " + error);
  std::lock_guard lock(texture_mutex_);
  auto it = texture_contents_.try_emplace(path, std::move(read)).first;
  return it->second ? &*it->second : nullptr;
}

const Worldgen::TextureImage* Subsystem::LoadLayerImage(
    const std::string& path, int max_size) {
  const std::string key = path + "@" + std::to_string(max_size);
  {
    std::lock_guard lock(texture_mutex_);
    if (auto it = layer_images_.find(key); it != layer_images_.end())
      return it->second.width > 0 ? &it->second : nullptr;
  }
  // Kept here as worldgen reads it, not in the image cache too
  Worldgen::TextureImage image;
  Content::ImageData data = Content::ImageLoader::LoadFile(path);
  if (data.Valid() && data.format == Content::ImageFormat::RGBA8)
    image = _Shrink({data.width, data.height, std::move(data.pixels)},
                    max_size);
  std::lock_guard lock(texture_mutex_);
  auto it = layer_images_.try_emplace(key, std::move(image)).first;
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