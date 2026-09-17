----------------------------------------------------------------
--  Global settings
----------------------------------------------------------------
set_languages("c++20")

-- MSVC-specific compiler flags
if is_plat("windows") then
	add_cxxflags("/Zc:__cplusplus", "/Zc:preprocessor")
	add_defines("NOMINMAX")
	add_defines("_CRT_SECURE_NO_WARNINGS")
	set_config("vs", "2022")

	-- use static CRT
	set_runtimes(is_mode("debug") and "MTd" or "MT")
end

-- compiler flags
add_cxflags("-fno-strict-aliasing", { tools = { "gcc", "clang" } })
add_cxflags("/permissive-", { tools = "cl" })

-- set path globally
add_includedirs("thirdparty")

-- set_xmakever("2.8.7")
add_rules("mode.debug", "mode.release")
set_policy("check.auto_ignore_flags", false)

-- external dependencies
add_requires(
	"boost",
	"libsdl2",
	"libsdl2_ttf",
	"portaudio",
	"glm",
	"tinygltf",
	"efsw",
	"nlohmann_json",
	"entt",
	"pcg-cpp"
)
add_requires("imgui 1.91.8-docking", { configs = { sdl2 = true, sdl2_renderer = true, docking = true } })
add_requires("bgfx", { configs = { tools = true } })
add_requires("reflect-cpp", { configs = { json = true } })
add_requires(
	"assimp",
	{
		configs = {
			shared = false,
			build_all_importers = false,
			build_gltf_importer = true,
			build_obj_importer = true,
			build_fbx_importer = true,
			build_all_exporters = false,
			no_export = true,
		},
	}
)

if is_mode("debug") then
	add_defines("BX_CONFIG_DEBUG=1")
else
	add_defines("BX_CONFIG_DEBUG=0")
end

-- macOS frameworks
if is_plat("macosx") then
	add_frameworks("Metal", "MetalKit", "QuartzCore")
end

-- ! Enforce arm64 on macOS
if is_host("macosx") then
    set_config("arch", "arm64")
end

----------------------------------------------------------------
--  per-module build scripts
----------------------------------------------------------------
includes("src/platform/xmake.lua")
includes("src/engine/xmake.lua")
includes("src/editor/xmake.lua")
includes("src/tests/xmake.lua")
includes("src/game/xmake.lua")

includes("thirdparty/xmake.lua")

