---------------------------------------------------------------
--  platform target
---------------------------------------------------------------
target("platform")
    set_kind("static")

    add_deps("FastNoise2")

    add_files("*.cpp", "input/*.cpp")      -- portable code only

    add_headerfiles("*.h", "input/*.h")

    -- Exactly one OS implementation is compiled
    if is_plat("macosx") then
        add_files("os/macos.mm")
    elseif is_plat("windows") then
        add_files("os/windows.cpp")
        add_syslinks("dwmapi")
    elseif is_plat("linux") then
        add_files("os/linux.cpp")
    end

    add_packages("libsdl2", "bgfx", "imgui", "boost", "nlohmann_json", "efsw", "reflect-cpp", "entt", "glm")

    add_includedirs("..")  

    -- Apple frameworks for Metal + Cocoa
    if is_plat("macosx") then
        add_frameworks("Metal", "MetalKit", "QuartzCore")
    end
target_end()