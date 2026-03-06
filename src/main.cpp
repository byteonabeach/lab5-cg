#include "Engine.h"
#include "RenderingSystem.h"
#include "Light.h"
#include "Camera.h"

#define TINYOBJLOADER_IMPLEMENTATION
#include <tiny_obj_loader.h>

#include <iostream>
#include <filesystem>
#include <string>
#include <vector>
#include <unordered_map>
#include <algorithm>
#include <cmath>

namespace fs = std::filesystem;

// ─────────────────────────────────────────────────────────────────────────────
//  Globals for GLFW callbacks
// ─────────────────────────────────────────────────────────────────────────────

static Engine*          g_engine        = nullptr;
static RenderingSystem* g_rs            = nullptr;
static Camera*          g_camera        = nullptr;
static bool             g_mouseCaptured = true;
static bool             g_firstMouse    = true;
static double           g_lastX = 0, g_lastY = 0;
static bool             g_needResize    = false;

static std::vector<SceneObject>* g_objects    = nullptr;
static int                        g_animObjIdx = -1;

static void framebufferCB(GLFWwindow*, int, int) {
    if (g_engine) { g_engine->framebufferResized = true; g_needResize = true; }
}

static void mouseCB(GLFWwindow*, double x, double y) {
    if (!g_mouseCaptured) return;
    if (g_firstMouse) { g_lastX = x; g_lastY = y; g_firstMouse = false; }
    g_camera->processMouse(x - g_lastX, y - g_lastY);
    g_lastX = x; g_lastY = y;
}

static void keyCB(GLFWwindow* w, int key, int, int action, int) {
    if (action != GLFW_PRESS) return;
    if (key == GLFW_KEY_ESCAPE) {
        g_mouseCaptured = !g_mouseCaptured;
        glfwSetInputMode(w, GLFW_CURSOR,
            g_mouseCaptured ? GLFW_CURSOR_DISABLED : GLFW_CURSOR_NORMAL);
        g_firstMouse = true;
    }
    if (key == GLFW_KEY_U && g_objects && g_animObjIdx >= 0) {
        (*g_objects)[g_animObjIdx].nextAnimFrame();
        std::cout << "[Anim] frame=" << (*g_objects)[g_animObjIdx].animFrame << "\n";
    }
}

// ─────────────────────────────────────────────────────────────────────────────
//  Helpers
// ─────────────────────────────────────────────────────────────────────────────

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

// ─────────────────────────────────────────────────────────────────────────────
//  OBJ loader
// ─────────────────────────────────────────────────────────────────────────────

static SceneObject loadOBJ(Engine& engine, const std::string& objPath,
                            bool animatable = false) {
    fs::path basePath = fs::path(objPath).parent_path();

    tinyobj::ObjReaderConfig cfg;
    cfg.mtl_search_path = basePath.string();
    cfg.triangulate     = true;

    tinyobj::ObjReader reader;
    if (!reader.ParseFromFile(objPath, cfg))
        throw std::runtime_error("tinyobj: " + reader.Error());
    if (!reader.Warning().empty())
        std::cerr << "[OBJ warn] " << reader.Warning();

    const auto& attrib    = reader.GetAttrib();
    const auto& shapes    = reader.GetShapes();
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
            int matID = shape.mesh.material_ids.empty() ? -1
                        : shape.mesh.material_ids[f];

            auto& [verts, inds] = batches[matID];
            for (int v = 0; v < 3; ++v) {
                tinyobj::index_t idx = shape.mesh.indices[off + v];
                Vertex vert{};
                vert.pos = {
                    attrib.vertices[3*idx.vertex_index+0],
                    attrib.vertices[3*idx.vertex_index+1],
                    attrib.vertices[3*idx.vertex_index+2]
                };
                if (idx.normal_index >= 0)
                    vert.normal = {
                        attrib.normals[3*idx.normal_index+0],
                        attrib.normals[3*idx.normal_index+1],
                        attrib.normals[3*idx.normal_index+2]
                    };
                if (idx.texcoord_index >= 0)
                    vert.texCoord = {
                        attrib.texcoords[2*idx.texcoord_index+0],
                        1.0f - attrib.texcoords[2*idx.texcoord_index+1]
                    };
                inds.push_back((uint32_t)verts.size());
                verts.push_back(vert);
            }
            off += 3;
        }

        for (auto& [matID, pair] : batches) {
            auto& [verts, inds] = pair;
            if (verts.empty()) continue;
            SubMesh sm;
            sm.mesh    = engine.createMesh(verts, inds);
            sm.texture = getMatTex(matID);
            obj.submeshes.push_back(sm);
        }
    }

    // Collect animation frames from directory images
    if (animatable) {
        static const std::vector<std::string> imgExts = {
            ".png",".jpg",".jpeg",".tga",".bmp",
            ".PNG",".JPG",".TGA",".BMP"
        };
        std::vector<TextureHandle> animTex;
        for (const auto& e : fs::directory_iterator(basePath)) {
            auto ext = e.path().extension().string();
            if (std::find(imgExts.begin(), imgExts.end(), ext) != imgExts.end())
                animTex.push_back(engine.loadTexture(e.path().string()));
        }
        std::sort(animTex.begin(), animTex.end(),
            [](const TextureHandle& a, const TextureHandle& b){ return a.id < b.id; });
        if (!animTex.empty()) {
            for (auto& sm : obj.submeshes) sm.animTextures = animTex;
            std::cout << "[Anim] " << animTex.size() << " frames loaded\n";
        }
    }

    std::cout << "[OBJ] " << objPath
              << " — " << obj.submeshes.size() << " submeshes\n";
    return obj;
}

// ─────────────────────────────────────────────────────────────────────────────
//  Build scene lights
// ─────────────────────────────────────────────────────────────────────────────

static std::vector<LightData> buildLights(double t) {
    std::vector<LightData> lights;

    // 1. Directional sun (warm daylight)
    lights.push_back(Light::makeDirectional(
        {-0.5f, -1.0f, -0.3f},
        {1.0f, 0.95f, 0.85f},
        1.2f));

    // 2. Orbiting point light (cool blue fill)
    float px = 3.0f * (float)std::cos(t * 0.5);
    float pz = 3.0f * (float)std::sin(t * 0.5);
    lights.push_back(Light::makePoint(
        {px, 2.5f, pz},
        {0.4f, 0.6f, 1.0f},
        3.0f,
        8.0f));

    // 3. Red spot light (above, pointing down — like a theatre spotlight)
    lights.push_back(Light::makeSpot(
        {0.0f, 5.0f, 0.0f},
        {0.0f, -1.0f, 0.0f},
        15.0f, 25.0f,
        {1.0f, 0.3f, 0.2f},
        4.0f,
        15.0f));

    return lights;
}

// ─────────────────────────────────────────────────────────────────────────────
//  main
// ─────────────────────────────────────────────────────────────────────────────

int main() {
    if (!glfwInit()) { std::cerr << "glfwInit failed\n"; return 1; }
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    GLFWwindow* window = glfwCreateWindow(1280, 720, "Vulkan Deferred", nullptr, nullptr);

    glfwSetFramebufferSizeCallback(window, framebufferCB);
    glfwSetCursorPosCallback      (window, mouseCB);
    glfwSetKeyCallback            (window, keyCB);
    glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);

    Engine engine;
    RenderingSystem rs;
    g_engine = &engine;
    g_rs     = &rs;

    engine.init(window);
    rs.init(engine);

    // ── Scene ─────────────────────────────────────────────────────────────
    std::vector<SceneObject> objects;

    try {
        auto sponza     = loadOBJ(engine, "assets/sponza/sponza.obj", false);
        sponza.transform= glm::scale(glm::mat4(1.0f), glm::vec3(0.01f));
        objects.push_back(std::move(sponza));
    } catch (const std::exception& e) {
        std::cerr << "[WARN] Sponza: " << e.what() << "\n";
    }

    int animIdx = -1;
    try {
        auto m2     = loadOBJ(engine, "assets/model2/model.obj", true);
        m2.transform= glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 0.0f, 0.0f));
        animIdx = (int)objects.size();
        objects.push_back(std::move(m2));
    } catch (const std::exception& e) {
        std::cerr << "[WARN] model2: " << e.what() << "\n";
    }

    g_objects    = &objects;
    g_animObjIdx = animIdx;

    // ── Camera ────────────────────────────────────────────────────────────
    Camera camera;
    camera.position = {0.0f, 1.5f, 3.0f};
    g_camera = &camera;

    double lastTime = glfwGetTime();

    // ── Main loop ─────────────────────────────────────────────────────────
    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();

        double now = glfwGetTime();
        float  dt  = (float)(now - lastTime);
        lastTime   = now;

        camera.processKeyboard(window, dt);

        FrameContext ctx = engine.beginFrame();

        if (!ctx.valid) {
            // Swapchain was recreated — notify RenderingSystem
            rs.onResize(engine);
            g_needResize = false;
            continue;
        }

        if (g_needResize) {
            // Window resize came via GLFW callback
            vkDeviceWaitIdle(engine.getDevice());
            rs.onResize(engine);
            g_needResize = false;
        }

        auto lights = buildLights(now);
        rs.setLights(lights);
        rs.recordFrame(ctx.cmd, ctx.imageIndex, ctx.frameIndex,
                       camera, objects, engine);

        engine.endFrame(ctx);
    }

    rs.cleanup(engine);
    engine.cleanup();
    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}
