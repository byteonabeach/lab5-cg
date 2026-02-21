#include "Window.h"
#include "Input.h"
#include "Timer.h"
#include "Renderer.h"
#include "Mesh.h"

#include <glm/gtc/matrix_transform.hpp>
#include <filesystem>
#include <iostream>
#include <string>
#include <cmath>

namespace fs = std::filesystem;

static std::string findOBJ() {
    if (!fs::exists("assets")) return {};
    for (auto& e : fs::directory_iterator("assets"))
        if (e.path().extension() == ".obj")
            return e.path().string();
    return {};
}

static std::string findTexture(const fs::path& dir) {
    for (auto& e : fs::directory_iterator(dir)) {
        auto ext = e.path().extension().string();
        if (ext == ".png" || ext == ".jpg" || ext == ".jpeg")
            return e.path().string();
    }
    return {};
}

int main() {
    std::string objPath = findOBJ();
    if (objPath.empty()) {
        std::cerr << "no .obj found in assets/\n";
        return 1;
    }

    Window   window(1280, 720, "VulkanApp");
    Input::get().init(window.handle());

    Renderer renderer(window);
    renderer.setLight({5, 8, 5}, {1, 1, 1}, 64.f);
    renderer.setUV({0, 0}, {1, 1});

    auto meshes = loadOBJ(objPath);
    if (meshes.empty()) {
        std::cerr << "failed to load " << objPath << "\n";
        return 1;
    }

    std::string fallbackTex = findTexture(fs::path(objPath).parent_path());
    if (!fallbackTex.empty())
        std::cout << "fallback texture: " << fallbackTex << "\n";

    for (auto& m : meshes) {
        if (m.texturePath.empty() && !fallbackTex.empty())
            m.texturePath = fallbackTex;
        renderer.uploadMesh(m);
    }

    glm::vec3 pos   = {0, 1, 3};
    float     yaw   = -90.f;
    float     pitch = -10.f;
    Timer     timer;

    int uvMode = 0;

    while (!window.shouldClose()) {
        Input::get().beginFrame();
        window.poll();
        timer.tick();

        float dt    = timer.dt();
        float total = timer.total();

        if (Input::get().isDown(GLFW_KEY_ESCAPE)) break;
        if (Input::get().pressed(GLFW_KEY_1)) uvMode = 0;
        if (Input::get().pressed(GLFW_KEY_2)) uvMode = 1;
        if (Input::get().pressed(GLFW_KEY_3)) uvMode = 2;

        switch (uvMode) {
            case 0:
                renderer.setUV({0.f, 0.f}, {1.f, 1.f});
                break;
            case 1:
                renderer.setUV({total * 0.1f, total * 0.05f}, {1.f, 1.f});
                break;
            case 2:
                renderer.setUV({0.f, 0.f}, {
                    1.f + 0.5f * sinf(total),
                    1.f + 0.5f * sinf(total)
                });
                break;
        }

        auto d = Input::get().delta();
        yaw   += d.x * 0.12f;
        pitch -= d.y * 0.12f;
        pitch  = glm::clamp(pitch, -89.f, 89.f);

        glm::vec3 dir = glm::normalize(glm::vec3(
            cos(glm::radians(yaw)) * cos(glm::radians(pitch)),
            sin(glm::radians(pitch)),
            sin(glm::radians(yaw)) * cos(glm::radians(pitch))
        ));
        glm::vec3 right = glm::normalize(glm::cross(dir, {0, 1, 0}));

        float speed = 5.f * dt;
        if (Input::get().isDown(GLFW_KEY_LEFT_SHIFT)) speed *= 3.f;
        if (Input::get().isDown(GLFW_KEY_W)) pos += dir   * speed;
        if (Input::get().isDown(GLFW_KEY_S)) pos -= dir   * speed;
        if (Input::get().isDown(GLFW_KEY_A)) pos -= right * speed;
        if (Input::get().isDown(GLFW_KEY_D)) pos += right * speed;
        if (Input::get().isDown(GLFW_KEY_SPACE))        pos.y += speed;
        if (Input::get().isDown(GLFW_KEY_LEFT_CONTROL)) pos.y -= speed;

        renderer.setCamera(pos, pos + dir, {0, 1, 0});

        if (!renderer.beginFrame()) continue;
        for (int i = 0; i < renderer.meshCount(); i++)
            renderer.draw(i, glm::mat4(1.f));
        renderer.endFrame();

        static float t = 0; t += dt;
        if (t > 0.5f) {
            t = 0;
            window.setTitle("VulkanApp | " + std::to_string(timer.fps()) +
                            " fps | UV mode: " + std::to_string(uvMode + 1));
        }
    }
}
