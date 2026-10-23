---------------------------------------------------------------
--  editor target
---------------------------------------------------------------
target("scape")
set_kind("binary")
set_default(true)

add_deps("engine", "platform", "sample_game", "FastNoise2", "implot", "imgui-node-editor", "ImGuiFileDialog")

add_includedirs("..", "vendor", "systems")

add_files(
	"main.cpp",
	"*.cpp",
	"runtime/*.cpp",
	"gui/*.cpp",
	"gui/inspectables/*.cpp",
	"gui/search/*.cpp",
	"gui/utils/*.cpp",
	"gui/modals/*.cpp",
	"gui/components/*.cpp",
	"gui/viewport/*.cpp",
	"gui/styles/*.cpp",
	"systems/*.cpp",
	"project/*.cpp",
	"registry/*.cpp",
	"pipelines/*.cpp",
	"gizmos/*.cpp",
	"camera/*.cpp"
)

add_files("vendor/imgui/imgui_impl_bgfx.cpp", "vendor/imguizmo/*.cpp")

add_headerfiles(
	"runtime/*.h",
	"gui/*.h",
	"gui/inspectables/*.h",
	"gui/utils/*.h",
	"gui/search/*.h",
	"gui/viewport/*.h",
	"gui/modals/*.h",
	"gui/components/*.h",
	"gui/styles/*.h",
	"systems/*.h",
	"project/*.h",
	"registry/*.h",
	"pipelines/*.h",
	"gizmos/*.h",
	"camera/*.h"
)

add_packages(
	"imgui",
	"boost",
	"libsdl2",
	"bgfx",
	"glm",
	"libsdl2_ttf",
	"portaudio",
	"efsw",
	"nlohmann_json",
	"reflect-cpp",
	"entt",
	"pcg-cpp"
)

-- copy all assets
after_build(function(target)
	local bindir = target:targetdir()
	os.cp("src/assets/textures", path.join(bindir, "assets/textures"))
	os.cp("src/assets/audio", path.join(bindir, "assets/audio"))
	os.cp("src/editor/resources/icons", path.join(bindir, "resources/icons"))
	os.cp("src/editor/resources/fonts", path.join(bindir, "resources/fonts"))
end)
add_defines("SDL_MAIN_HANDLED", "DISPLAY_GRAPHICAL")
target_end()

