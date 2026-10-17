---------------------------------------------------------------
--  engine target
---------------------------------------------------------------
target("engine")
set_kind("static")

add_deps("platform", "FastNoise2")

-- PUBLIC INCLUDE DIRS -------
add_includedirs("..", { public = true })

-- IMPLEMENTATION FILES -------
add_files(
	"core/*.cpp",
	"audio/*.cpp",
	"misc/*.cpp",
	"scene/*.cpp",
	"context/*.cpp",
	"ecs/*.cpp",
	"memory/*.cpp",
	"transform/*.cpp",
	"time/*.cpp",
	"math/*.cpp",
	"pcg/*.cpp",
	"diagnostics/*.cpp",

	-- GEOMETRY FILES -------
	"geometry/meshing/*.cpp",
	"geometry/projection/*.cpp",

	-- renderer files
	"renderer/*.cpp",
	"renderer/lighting/*.cpp",
	"renderer/shaders/*.cpp",
	"renderer/icons/*.cpp",
	"renderer/texture/*.cpp",
	"renderer/model/*.cpp",
	"renderer/skybox/*.cpp",
	"renderer/gizmos/*.cpp",
	"renderer/shadows/*.cpp",
	"renderer/material/*.cpp",

	-- Math files
	"math/noise/OpenSimplex2S.cpp",
	"math/noise/perlin.cpp",

	-- content files
	"content/cache/*.cpp",
	"content/loaders/*.cpp",
	"content/io/*.cpp",

	-- resources files
	"resources/*.cpp",
	"resources/textures/*.cpp",

	-- PCG files
	"pcg/operators/*.cpp",
	"pcg/constraints/constraint_system.cpp",
	"pcg/terrain/*.cpp",
	"pcg/graph/*.cpp",
	"pcg/procmodel/*.cpp",
	"pcg/procmodel/descriptor/*.cpp",
	"pcg/procmodel/model_graph/*.cpp",
	"pcg/procmodel/generator/*.cpp",
	"pcg/procmodel/generator/operations/*.cpp",
	"pcg/procmodel/instantiator/*.cpp",
	"pcg/procmodel/validation/*.cpp",
	"pcg/pipeline/*.cpp"
)

-- HEADER FILES -------
add_headerfiles(
	"core/*.h",
	"audio/*.h",
	"scene/*.h",
	"context/*.h",
	"ecs/*.h",
	"memory/*.h",
	"transform/*.h",
	"time/*.h",
	"math/*.h",
	"pcg/*.h",
	"diagnostics/*.h",

	-- renderer files
	"renderer/*.h",
	"renderer/lighting/*.h",
	"renderer/shaders/*.h",
	"renderer/icons/*.h",
	"renderer/texture/*.h",
	"renderer/model/*.h",
	"renderer/skybox/*.h",
	"renderer/gizmos/*.h",
	"renderer/shadows/*.h",
	"renderer/material/*.h",

	-- math files
	"math/noise/stb_perlin.h",
	"math/noise/perlin.h",

	-- geometry files
	"geometry/meshing/*.h",
	"geometry/projection/*.h",

	-- content files
	"content/cache/*.h",
	"content/loaders/*.h",
	"content/io/*.h",

	-- resource files
	"resources/*.h",
	"resources/textures/*.h",

	-- procmodel files
	"pcg/procmodel/*.h",
	"pcg/procmodel/descriptor/*.h",
	"pcg/procmodel/generator/*.h",
	"pcg/procmodel/generator/operations/*.h",
	"pcg/procmodel/instantiator/*.h",
	"pcg/procmodel/model_graph/*.h",
	"pcg/procmodel/validation/*.h",
	"pcg/pipeline/*.h"
)

add_rules("shaderc.build")
add_files("$(projectdir)/src/assets/shaders/**.sc")
remove_files("$(projectdir)/src/assets/shaders/varying*.sc")

add_packages(
	"boost",
	"libsdl2",
	"bgfx",
	"glm",
	"imgui",
	"libsdl2_ttf",
	"portaudio",
	"tinygltf",
	"nlohmann_json",
	"entt",
	"assimp",
	"pcg-cpp"
)

if is_mode("debug") then
	add_links("bimg_decodeDebug", "bimg_encodeDebug")
else
	add_links("bimg_decodeRelease", "bimg_encodeRelease")
end
add_defines("BGFX_STATIC_LIB")

-- macOS specific frameworks that BGFX needs
if is_plat("macosx") then
	add_frameworks("Metal", "MetalKit", "QuartzCore")
end
target_end()

--───────────────────────────────────────────────────────────
--  SHADERC RULE – self-contained shader compiler
--───────────────────────────────────────────────────────────
rule("shaderc.build")
set_extensions(".sc", ".vert", ".frag", ".comp")

before_buildcmd_file(function(target, batchcmds, shaderfile, opt)
	-- shaderc exe ------------------------------------------------------------
	local exe = target:data("shaderc_exe")
	if not exe then
		import("lib.detect.find_program")

		-- print("Searching for shaderc in system PATH...")
		exe = find_program("shaderc") or find_program("shadercRelease") or find_program("shadercDebug")

		if not exe then
			print("Shaderc not found in PATH. Now searching inside the bgfx package...")
			local bgfx = target:pkg("bgfx")
			if bgfx then
				local bin = bgfx:installdir("bin")
				print("Checking in BGFX bin directory:", bin)

				exe = find_program("shaderc", { paths = bin })
					or find_program("shadercRelease", { paths = bin })
					or find_program("shadercDebug", { paths = bin })
			else
				print("BGFX package not available on target.")
			end
		end

		assert(exe, "shaderc executable not found; rebuild bgfx with tools=true")
		target:data_set("shaderc_exe", exe)
	end

	-- skip non-shader files ---------------------------------------------------
	local fname = path.filename(shaderfile)
	if fname:startswith("varying") then
		return
	end

	local stype = fname:match("^vs_") and "vertex"
		or fname:match("^fs_") and "fragment"
		or fname:match("^cs_") and "compute"
	if not stype then
		return
	end

	-- varying files -----------------------------------------------------------
	local shader_root = path.join(os.projectdir(), "src/assets/shaders")

	-- default varying file (not used)
	local varying = path.join(shader_root, "varying.def.sc")

	-- dispatch table – new varying files here go here
	local map = {
		imgui = "varying_imgui.def.sc",
		skybox = "varying_skybox.def.sc",
		terrain = "varying_terrain_pbr.def.sc",
		shadow = "varying_shadow.def.sc",
		water = "varying_water.def.sc",
		gltf = "varying_gltf.def.sc",
		default = "varying_default.def.sc",
		debug = "varying_debug.def.sc",
		materials = "varying_materials.def.sc",
		selection = "varying_default.def.sc",
	}

	-- pick first key matching anywhere in relative path
	local rel = path.relative(shaderfile, shader_root)
	for key, file in pairs(map) do
		if rel:find(key, 1, true) then
			varying = path.join(shader_root, file)
			break
		end
	end

	assert(os.isfile(varying), "No varying file for shader '" .. fname .. "'\n  selected: " .. varying)

	-- output bin -------------------------------------------------------------
	local binname = fname:gsub("%.sc$", ".bin")

	-- backend matrix ---------------------------------------------------------
	local matrix = {
		{ platform = "windows", profile = "s_5_0", folder = "dx11" },
		{ platform = "windows", profile = "s_5_0", folder = "dx12" },
		{ platform = "linux", profile = "440", folder = "glsl" },
		{ platform = "linux", profile = "spirv", folder = "spirv" },
		{ platform = "osx", profile = "metal", folder = "metal" },
		{ platform = "android", profile = "spirv", folder = "spirv" },
	}
	local current = is_plat("windows") and "windows"
		or is_plat("macosx") and "osx"
		or is_plat("linux") and "linux"
		or is_plat("android") and "android"
		or ""
	local backends = {}
	for _, b in ipairs(matrix) do
		if b.platform == current then
			table.insert(backends, b)
		end
	end
	if #backends == 0 then
		print("Warning: No backend for platform '" .. current .. "', default GL.")
		table.insert(backends, { platform = "linux", profile = "440", folder = "glsl" })
	end

	-- fenerate for each backend ----------------------------------------------
	for _, backend in ipairs(backends) do
		local outdir = path.join(target:targetdir(), "assets/shaders", backend.folder)
		batchcmds:mkdir(outdir)
		local outfile = path.join(outdir, binname)

		-- build args -------------------------------------------------------------
		local args = {
			"--platform",
			backend.platform,
			"--type",
			stype,
			"--profile",
			backend.profile,
			"--varyingdef",
			varying,
			"-i",
			path.join(os.projectdir(), "thirdparty/bgfx_utils/common"),
			"-i",
			path.join(os.projectdir(), "thirdparty/bgfx_utils/src"),
			"-f",
			shaderfile,
			"-o",
			outfile,
		}
		-- Print shader file progress
		batchcmds:show_progress(
			opt.progress,
			"[shaderc] %-15s → %-20s | varying: %s",
			fname,
			backend.folder,
			path.basename(varying)
		)
		batchcmds:vrunv(exe, args)
		batchcmds:set_depmtime(os.mtime(outfile))
	end

	batchcmds:add_depfiles(shaderfile, varying)
end)
