target("tests")
    set_kind("binary")
    add_deps("engine", "FastNoise2")
 
    add_includedirs("../", {public = false})

    add_files("*.cpp")

    add_packages("boost", "libsdl2", "bgfx", "glm", "imgui", "libsdl2_ttf", "portaudio", "tinygltf", "nlohmann_json", "entt")
    
    set_rundir("$(projectdir)")
    
    after_build(function (target)
        print("Tests built successfully!")
        print("Run with: xmake run tests")
    end)
target_end()
