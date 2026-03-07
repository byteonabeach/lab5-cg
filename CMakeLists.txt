cmake_minimum_required(VERSION 3.20)
project(VulkanDeferred)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Vulkan REQUIRED)
find_package(glfw3 REQUIRED)

include(FetchContent)

FetchContent_Declare(glm
    GIT_REPOSITORY https://github.com/g-truc/glm.git
    GIT_TAG        1.0.3)
FetchContent_MakeAvailable(glm)

FetchContent_Declare(tinyobjloader
    GIT_REPOSITORY https://github.com/tinyobjloader/tinyobjloader.git
    GIT_TAG        v2.0.0rc13)
FetchContent_MakeAvailable(tinyobjloader)

FetchContent_Declare(stb
    GIT_REPOSITORY https://github.com/nothings/stb.git
    GIT_TAG        master)
FetchContent_MakeAvailable(stb)


add_executable(VulkanDeferred
    src/main.cpp
    src/Engine.cpp
    src/GBuffer.cpp
    src/RenderingSystem.cpp
)

target_include_directories(VulkanDeferred PRIVATE
    src/
    ${stb_SOURCE_DIR}
    ${tinyobjloader_SOURCE_DIR}
)

target_link_libraries(VulkanDeferred PRIVATE
    Vulkan::Vulkan
    glfw
    glm::glm
)

target_compile_definitions(VulkanDeferred PRIVATE
    GLM_FORCE_RADIANS
    GLM_FORCE_DEPTH_ZERO_TO_ONE
    $<$<CONFIG:Debug>:ENABLE_VALIDATION_LAYERS>
)


find_program(GLSLC glslc HINTS ENV VULKAN_SDK PATH_SUFFIXES bin)
if(NOT GLSLC)
    message(FATAL_ERROR "glslc not found — install Vulkan SDK")
endif()

set(SHADER_DIR ${CMAKE_SOURCE_DIR}/shaders)
set(SHADER_OUT ${CMAKE_BINARY_DIR}/shaders)
file(MAKE_DIRECTORY ${SHADER_OUT})

set(SHADERS
    gbuffer.vert
    gbuffer.frag
    lighting.vert
    lighting.frag
    phong.vert
    phong.frag
    shadows.vert
)

foreach(SHADER ${SHADERS})
    add_custom_command(
        OUTPUT  ${SHADER_OUT}/${SHADER}.spv
        COMMAND ${GLSLC} ${SHADER_DIR}/${SHADER} -o ${SHADER_OUT}/${SHADER}.spv
        DEPENDS ${SHADER_DIR}/${SHADER}
        COMMENT "Compiling ${SHADER}"
    )
    list(APPEND SPIRV_FILES ${SHADER_OUT}/${SHADER}.spv)
endforeach()

add_custom_target(Shaders ALL DEPENDS ${SPIRV_FILES})
add_dependencies(VulkanDeferred Shaders)

# Symlink assets into build directory
add_custom_command(TARGET VulkanDeferred POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E create_symlink
        ${CMAKE_SOURCE_DIR}/assets $<TARGET_FILE_DIR:VulkanDeferred>/assets
    COMMENT "Linking assets"
)
