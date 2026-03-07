#include "Engine.h"
#include "RenderingSystem.h"
#include "Light.h"
#include "Camera.h"
#include "Input.h"

#define TINYOBJLOADER_IMPLEMENTATION
#include <tiny_obj_loader.h>
#include <filesystem>
#include <string>
#include <vector>
#include <unordered_map>
#include <algorithm>
#include <cmath>

namespace fs = std::filesystem;

struct FallingFlashlight {
    glm::vec3 position;
    glm::vec3 velocity;
    glm::vec3 color;
    SceneObject object;
};

static std::string normSlashes(std::string p) {
    for (char& c : p) if (c == '\\') c = '/';
    return p;
}

static std::string findTexture(const std::string& name, const fs::path& baseDir) {
    if (name.empty()) return {};
    std::string n = normSlashes(name);
    if (fs::exists(n)) return n;
    if (fs::exists(baseDir / n)) return (baseDir / n).string();
    auto fn = fs::path(n).filename();
    if (fs::exists(baseDir / fn)) return (baseDir / fn).string();
    if (fs::exists(baseDir / "textures" / fn)) return (baseDir / "textures" / fn).string();
    return {};
}

static SceneObject loadOBJ(Engine& engine, const std::string& objPath, bool animatable = false) {
    fs::path basePath = fs::path(objPath).parent_path();
    tinyobj::ObjReaderConfig cfg;
    cfg.mtl_search_path = basePath.string();
    cfg.triangulate = true;
    tinyobj::ObjReader reader;
    if (!reader.ParseFromFile(objPath, cfg)) throw std::runtime_error("tinyobj failed");
    const auto& attrib = reader.GetAttrib();
    const auto& shapes = reader.GetShapes();
    const auto& materials = reader.GetMaterials();
    TextureHandle whiteTex = engine.createWhiteTexture();
    std::unordered_map<int, TextureHandle> texCache;
    auto getMatTex = [&](int id) -> TextureHandle {
        if (id < 0) return whiteTex;
        auto it = texCache.find(id);
        if (it != texCache.end()) return it->second;
        auto path = findTexture(materials[id].diffuse_texname, basePath);
        TextureHandle h = path.empty() ? whiteTex : engine.loadTexture(path);
        texCache[id] = h;
        return h;
    };
    SceneObject obj;
    obj.animatable = animatable;
    for (const auto& shape : shapes) {
        std::unordered_map<int, std::pair<std::vector<Vertex>, std::vector<uint32_t>>> batches;
        size_t off = 0;
        for (size_t f = 0; f < shape.mesh.num_face_vertices.size(); ++f) {
            int matID = shape.mesh.material_ids.empty() ? -1 : shape.mesh.material_ids[f];
            auto& [verts, inds] = batches[matID];
            for (int v = 0; v < 3; ++v) {
                tinyobj::index_t idx = shape.mesh.indices[off + v];
                Vertex vert{};
                vert.pos = {attrib.vertices[3*idx.vertex_index+0], attrib.vertices[3*idx.vertex_index+1], attrib.vertices[3*idx.vertex_index+2]};
                if (idx.normal_index >= 0) vert.normal = {attrib.normals[3*idx.normal_index+0], attrib.normals[3*idx.normal_index+1], attrib.normals[3*idx.normal_index+2]};
                if (idx.texcoord_index >= 0) vert.texCoord = {attrib.texcoords[2*idx.texcoord_index+0], 1.0f - attrib.texcoords[2*idx.texcoord_index+1]};
                inds.push_back((uint32_t)verts.size());
                verts.push_back(vert);
            }
            off += 3;
        }
        for (auto& [matID, pair] : batches) {
            auto& [verts, inds] = pair;
            if (verts.empty()) continue;
            SubMesh sm;
            sm.mesh = engine.createMesh(verts, inds);
            sm.texture = getMatTex(matID);
            obj.submeshes.push_back(sm);
        }
    }
    if (animatable) {
        static const std::vector<std::string> imgExts = {".png",".jpg",".jpeg",".tga",".bmp",".PNG",".JPG",".TGA",".BMP"};
        std::vector<TextureHandle> animTex;
        for (const auto& e : fs::directory_iterator(basePath)) {
            auto ext = e.path().extension().string();
            if (std::find(imgExts.begin(), imgExts.end(), ext) != imgExts.end())
                animTex.push_back(engine.loadTexture(e.path().string()));
        }
        std::sort(animTex.begin(), animTex.end(), [](const TextureHandle& a, const TextureHandle& b){ return a.id < b.id; });
        if (!animTex.empty()) {
            for (auto& sm : obj.submeshes) sm.animTextures = animTex;
        }
    }
    return obj;
}

static MeshHandle createCubeMesh(Engine& engine) {
    std::vector<Vertex> v = {
        {{-1,-1,-1}, {0,0,-1}, {0,0}}, {{1,-1,-1}, {0,0,-1}, {1,0}}, {{1,1,-1}, {0,0,-1}, {1,1}}, {{-1,-1,-1}, {0,0,-1}, {0,0}}, {{1,1,-1}, {0,0,-1}, {1,1}}, {{-1,1,-1}, {0,0,-1}, {0,1}},
        {{-1,-1,1}, {0,0,1}, {0,0}}, {{1,-1,1}, {0,0,1}, {1,0}}, {{1,1,1}, {0,0,1}, {1,1}}, {{-1,-1,1}, {0,0,1}, {0,0}}, {{1,1,1}, {0,0,1}, {1,1}}, {{-1,1,1}, {0,0,1}, {0,1}},
        {{-1,-1,-1}, {-1,0,0}, {0,0}}, {{-1,1,-1}, {-1,0,0}, {1,0}}, {{-1,1,1}, {-1,0,0}, {1,1}}, {{-1,-1,-1}, {-1,0,0}, {0,0}}, {{-1,1,1}, {-1,0,0}, {1,1}}, {{-1,-1,1}, {-1,0,0}, {0,1}},
        {{1,-1,-1}, {1,0,0}, {0,0}}, {{1,1,-1}, {1,0,0}, {1,0}}, {{1,1,1}, {1,0,0}, {1,1}}, {{1,-1,-1}, {1,0,0}, {0,0}}, {{1,1,1}, {1,0,0}, {1,1}}, {{1,-1,1}, {1,0,0}, {0,1}},
        {{-1,-1,-1}, {0,-1,0}, {0,0}}, {{1,-1,-1}, {0,-1,0}, {1,0}}, {{1,-1,1}, {0,-1,0}, {1,1}}, {{-1,-1,-1}, {0,-1,0}, {0,0}}, {{1,-1,1}, {0,-1,0}, {1,1}}, {{-1,-1,1}, {0,-1,0}, {0,1}},
        {{-1,1,-1}, {0,1,0}, {0,0}}, {{1,1,-1}, {0,1,0}, {1,0}}, {{1,1,1}, {0,1,0}, {1,1}}, {{-1,1,-1}, {0,1,0}, {0,0}}, {{1,1,1}, {0,1,0}, {1,1}}, {{-1,1,1}, {0,1,0}, {0,1}}
    };
    std::vector<uint32_t> i(36);
    for(uint32_t j=0; j<36; ++j) i[j] = j;
    return engine.createMesh(v, i);
}

int main() {
    glfwInit();
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    GLFWwindow* window = glfwCreateWindow(1280, 720, "Vulkan Deferred", nullptr, nullptr);

    Input input;
    input.init(window);

    Engine engine;
    RenderingSystem rs;
    engine.init(window);
    rs.init(engine);

    MeshHandle cubeMesh = createCubeMesh(engine);
    std::vector<SceneObject> objects;

    std::vector<FallingFlashlight> droppedLights;
    bool fPressedLastFrame = false;

    const float GRAVITY = -9.81f;
    const float FLOOR_Y = 0.05f;


    try {
        auto sponza = loadOBJ(engine, "assets/sponza/sponza.obj", false);
        sponza.transform = glm::scale(glm::mat4(1.0f), glm::vec3(0.01f));
        objects.push_back(std::move(sponza));
    } catch (...) {}

    int animIdx = -1;
    try {
        auto m2 = loadOBJ(engine, "assets/model2/model.obj", true);
        m2.transform = glm::mat4(1.0f);
        animIdx = (int)objects.size();
        objects.push_back(std::move(m2));
    } catch (...) {}

    for (int i=0; i<3; ++i) {
        SceneObject cubeLight;
        SubMesh sm;
        sm.mesh = cubeMesh;
        sm.texture = engine.createWhiteTexture();
        cubeLight.submeshes.push_back(sm);
        cubeLight.unlit = true;
        objects.push_back(cubeLight);
    }

    Camera camera;
    double lastTime = glfwGetTime();

    while (!glfwWindowShouldClose(window)) {
            input.update();
            if (input.wasResized()) {
                int w=0, h=0;
                glfwGetFramebufferSize(window, &w, &h);
                if (w == 0 || h == 0) continue;
                engine.recreateSwapchain();
                rs.onResize(engine);
                input.clearResized();
            }

            double now = glfwGetTime();
            float dt = (float)(now - lastTime);
            lastTime = now;
            camera.update(input, dt);

            // 1. ЛОГИКА АНИМАЦИИ И СПАВНА ФОНАРИКОВ
            if (input.isKeyDown(GLFW_KEY_U) && animIdx >= 0) objects[animIdx].nextAnimFrame();

            bool fIsDown = input.isKeyDown(GLFW_KEY_F);
            if (fIsDown && !fPressedLastFrame) {
                FallingFlashlight fl;
                fl.position = camera.position;
                fl.velocity = camera.front() * 10.0f;
                fl.color = glm::vec3((rand()%100)/100.f, (rand()%100)/100.f, (rand()%100)/100.f) * 2.0f + 0.5f;

                SubMesh sm;
                sm.mesh = cubeMesh;
                sm.texture = engine.createWhiteTexture();
                fl.object.submeshes.push_back(sm);
                fl.object.unlit = true;
                fl.object.unlitColor = glm::vec4(fl.color, 1.0f);
                droppedLights.push_back(fl);
            }
            fPressedLastFrame = fIsDown;

            // 2. ФИЗИКА ФОНАРИКОВ
            for (auto& fl : droppedLights) {
                if (fl.position.y > FLOOR_Y) {
                    fl.velocity.y += GRAVITY * dt;
                    fl.position += fl.velocity * dt;
                } else {
                    fl.position.y = FLOOR_Y;
                    fl.velocity = glm::vec3(0.0f);
                }
                fl.object.transform = glm::translate(glm::mat4(1.0f), fl.position) * glm::scale(glm::mat4(1.0f), glm::vec3(0.15f));
            }

            // 3. ПОДГОТОВКА ВСЕХ ИСТОЧНИКОВ СВЕТА (Static + Dropped)
            std::vector<LightData> allLights;
            // Основные источники
            allLights.push_back(Light::makeDirectional({-0.5f, -1.0f, -0.3f}, {1.0f, 0.95f, 0.85f}, 2.0f, true, 0));
            float px = 3.0f * (float)std::cos(now * 0.5);
            float pz = 3.0f * (float)std::sin(now * 0.5);
            allLights.push_back(Light::makePoint({px, 2.5f, pz}, {0.4f, 0.6f, 1.0f}, 5.0f, 10.0f));
            allLights.push_back(Light::makeSpot({0.0f, 5.0f, 0.0f}, {0.0f, -1.0f, 0.0f}, 15.0f, 25.0f, {1.0f, 0.3f, 0.2f}, 10.0f, 20.0f, true, 1));

            // Добавляем свет от фонариков
            for (const auto& fl : droppedLights) {
                allLights.push_back(Light::makePoint(fl.position, fl.color, 8.0f, 12.0f));
            }

            if (allLights.size() > 64) allLights.resize(64);
            rs.setLights(allLights);

            // Обновляем визуальные кубики для основных лампочек (последние 3 объекта в векторе objects)
            for(size_t i=0; i<3; ++i) {
                size_t objIdx = objects.size() - 3 + i;
                if (objIdx < objects.size() && i + 1 < allLights.size()) { // i+1 так как 0-й свет это солнце
                    objects[objIdx].transform = glm::translate(glm::mat4(1.0f), glm::vec3(allLights[i+1].position)) * glm::scale(glm::mat4(1.0f), glm::vec3(0.2f));
                    objects[objIdx].unlitColor = allLights[i+1].color * 3.0f;
                }
            }

            std::vector<SceneObject> frameObjects = objects;
            for (const auto& fl : droppedLights) {
                frameObjects.push_back(fl.object);
            }

            FrameContext ctx = engine.beginFrame();
            if (!ctx.valid) {
                engine.recreateSwapchain();
                rs.onResize(engine);
                input.clearResized();
                continue;
            }

            rs.recordFrame(ctx.cmd, ctx.imageIndex, ctx.frameIndex, camera, frameObjects, engine);
            engine.endFrame(ctx);
        }

    rs.cleanup(engine);
    engine.cleanup();
    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}
