#include "shader_manager.h"

#include "engine/core/logger.h"
#include "engine/resources/shader_utils.h"

std::unordered_map<std::string, bgfx::ProgramHandle>
    ShaderManager::program_cache_;
std::unordered_map<std::string, bgfx::ShaderHandle>
    ShaderManager::shader_cache_;

void ShaderManager::Init() {
  program_cache_.clear();
  shader_cache_.clear();
}

void ShaderManager::Shutdown() {
  // Destroy all programs (this automatically destroys shaders, because of the
  // 'true' flag)
  for (auto& pair : program_cache_) {
    if (bgfx::isValid(pair.second))
      bgfx::destroy(pair.second);
  }
  program_cache_.clear();

  // Don't manually destroy shaders that were auto-destroyed
  shader_cache_.clear();
}


bgfx::ShaderHandle ShaderManager::LoadShader(const std::string& path) {
  if (shader_cache_.count(path))
    return shader_cache_[path];

  bgfx::ShaderHandle handle = loadShader(path.c_str());
  if (!bgfx::isValid(handle)) {
    Logger::getInstance().Log(LogLevel::Error,
                              "ShaderManager failed to load shader: " + path);
  } else {
    shader_cache_[path] = handle;
  }

  return handle;
}

bgfx::ProgramHandle ShaderManager::LoadProgram(const std::string& name,
                                               const std::string& vsPath,
                                               const std::string& fsPath) {
  // Keyed by the shader pair: one name can hold several programs (e.g. a
  // material's plain and instanced ones)
  const std::string key = vsPath + "|" + fsPath;
  if (program_cache_.count(key))
    return program_cache_[key];

  bgfx::ShaderHandle vs = LoadShader(vsPath);
  bgfx::ShaderHandle fs = LoadShader(fsPath);

  if (!bgfx::isValid(vs) || !bgfx::isValid(fs)) {
    Logger::getInstance().Log(LogLevel::Error,
                              "ShaderManager failed to load program: " + name);
    return BGFX_INVALID_HANDLE;
  }

  bgfx::ProgramHandle program = bgfx::createProgram(vs, fs, true);

  if (!bgfx::isValid(program)) {
    Logger::getInstance().Log(
        LogLevel::Error,
        "ShaderManager: bgfx::createProgram FAILED for VS_handle=" +
            std::to_string(vs.idx) + " FS_handle=" + std::to_string(fs.idx) +
            " for program '" + name + "'");
  } else {
    Logger::getInstance().Log(
        LogLevel::Debug,
        "ShaderManager: bgfx::createProgram SUCCEEDED for program '" + name +
            "', handle=" + std::to_string(program.idx));
  }
  // To catch linker errors
  program_cache_[key] = program;
  return program;
 
}
