add_rules("mode.debug", "mode.release")

add_requires("glfw 3.4", "glm", "tinyobjloader", "stb")

target("VulkanApp")
    set_kind("binary")
    set_languages("c++17")

    add_files("src/*.cpp")
    add_packages("glfw", "glm", "tinyobjloader", "stb")

    add_links("vulkan")
    add_linkdirs("/usr/lib", "/usr/lib/x86_64-linux-gnu")
    add_includedirs("/usr/include", "src")

    add_defines("SHADER_DIR=\"shaders/\"")

    if is_mode("debug") then
        add_defines("DEBUG_BUILD")
    end

    before_build(function(target)
        local shaderdir = path.join(target:targetdir(), "shaders")
        os.mkdir(shaderdir)
        local shaders = {"phong.vert", "phong.frag"}
        for _, s in ipairs(shaders) do
            local src = path.join("shaders", s)
            local out = path.join(shaderdir, s:gsub("%.", "_") .. ".spv")
            if os.isfile(src) then
                os.execv("glslc", {src, "-o", out})
            end
        end
    end)

    after_build(function(target)
        if os.isdir("assets") then
            os.cp("assets", path.join(target:targetdir(), "assets"))
        end
    end)
