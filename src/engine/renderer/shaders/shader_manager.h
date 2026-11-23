#ifndef SHADER_MANAGER_H
#define SHADER_MANAGER_H

#include <bgfx/bgfx.h>
#include <string>
#include <unordered_map>

class ShaderManager {
public:
  static void Init();      // Call once during engine init
  static void Shutdown();  // Call on exit

  static bgfx::ProgramHandle LoadProgram(const std::string& name,
                                         const std::string& vsPath,
                                         const std::string& fsPath);
  static bgfx::ProgramHandle LoadComputeProgram(const std::string& csPath);
  static bgfx::ShaderHandle LoadShader(const std::string& path);

private:
  static std::unordered_map<std::string, bgfx::ProgramHandle> program_cache_;
  static std::unordered_map<std::string, bgfx::ShaderHandle> shader_cache_;
};

#endif  // SHADER_MANAGER_H
