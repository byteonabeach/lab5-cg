#include "Engine.h"
#include "RenderingSystem.h"
#include "Light.h"
#include "Camera.h"
#include "Input.h"
#include "Culling.h"
#include "Octree.h"

#define TINYOBJLOADER_IMPLEMENTATION
#include <tiny_obj_loader.h>
#include <filesystem>
#include <string>
#include <vector>
#include <unordered_map>
#include <cmath>
#include <iostream>

namespace fs = std::filesystem;

struct FallingFlashlight {
    glm::vec3 position;
    glm::vec3 velocity;
    glm::vec3 color;
    SceneObject object;
};

enum CullingMode {
    CULLING_NONE = 0,
    CULLING_BRUTE_FORCE = 1,
    CULLING_OCTREE = 2
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
    if (!reader.ParseFromFile(objPath, cfg)) throw std::runtime_error("tinyobj failed: " + reader.Error());

    const auto& attrib = reader.GetAttrib();
    const auto& shapes = reader.GetShapes();
    const auto& materials = reader.GetMaterials();

    TextureHandle whiteTex = engine.createWhiteTexture();
    TextureHandle defNormTex = engine.createDefaultNormalTexture();
    TextureHandle blackTex = engine.createBlackTexture();

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

                obj.localBounds.expand(vert.pos);

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

            for (size_t i = 0; i < inds.size(); i += 3) {
                Vertex& v0 = verts[inds[i]];
                Vertex& v1 = verts[inds[i+1]];
                Vertex& v2 = verts[inds[i+2]];

                glm::vec3 e1 = v1.pos - v0.pos;
                glm::vec3 e2 = v2.pos - v0.pos;
                glm::vec2 duv1 = v1.texCoord - v0.texCoord;
                glm::vec2 duv2 = v2.texCoord - v0.texCoord;

                float f = 1.0f / (duv1.x * duv2.y - duv2.x * duv1.y + 0.0001f);
                glm::vec3 tangent;
                tangent.x = f * (duv2.y * e1.x - duv1.y * e2.x);
                tangent.y = f * (duv2.y * e1.y - duv1.y * e2.y);
                tangent.z = f * (duv2.y * e1.z - duv1.y * e2.z);

                if(glm::length(tangent) > 0.0001f) {
                    tangent = glm::normalize(tangent);
                } else {
                    tangent = glm::vec3(1.0f, 0.0f, 0.0f);
                }

                v0.tangent = v1.tangent = v2.tangent = tangent;
            }

            SubMesh sm;
            sm.mesh = engine.createMesh(verts, inds);

            TextureHandle tDiff = whiteTex;
            TextureHandle tNorm = defNormTex;
            TextureHandle tDisp = blackTex;
            sm.dispScale = 0.0f;
            sm.isCurtain = false;

            if (matID >= 0) {
                auto pDiff = findTexture(materials[matID].diffuse_texname, basePath);
                auto pNorm = findTexture(materials[matID].bump_texname, basePath);
                if (pNorm.empty()) pNorm = findTexture(materials[matID].normal_texname, basePath);
                auto pDisp = findTexture(materials[matID].displacement_texname, basePath);

                if (!pDiff.empty()) {
                    tDiff = engine.loadTexture(pDiff);
                    std::string lowerName = pDiff;
                    for(auto& c : lowerName) c = tolower(c);
                    if (lowerName.find("curtain") != std::string::npos || lowerName.find("fabric") != std::string::npos) {
                        sm.isCurtain = true;
                    }
                }
                if (!pNorm.empty()) tNorm = engine.loadTexture(pNorm);

                if (!pDisp.empty()) {
                    tDisp = engine.loadTexture(pDisp);
                    sm.dispScale = 0.05f;
                } else if (pNorm.find("displace") != std::string::npos && pDisp.empty()) {
                    tDisp = engine.loadTexture(pNorm);
                    sm.dispScale = 0.05f;
                }
            }

            sm.texture = tDiff;
            sm.normalMap = tNorm;
            sm.dispMap = tDisp;
            sm.matSet = engine.createMaterialSet(tDiff, tNorm, tDisp);

            obj.submeshes.push_back(sm);
        }
    }
    return obj;
}

static MeshHandle createCubeMesh(Engine& engine, AABB& outBounds) {
    std::vector<Vertex> v = {
        {{-1,-1,-1}, {0,0,-1}, {0,0}, {1,0,0}}, {{1,-1,-1}, {0,0,-1}, {1,0}, {1,0,0}}, {{1,1,-1}, {0,0,-1}, {1,1}, {1,0,0}}, {{-1,-1,-1}, {0,0,-1}, {0,0}, {1,0,0}}, {{1,1,-1}, {0,0,-1}, {1,1}, {1,0,0}}, {{-1,1,-1}, {0,0,-1}, {0,1}, {1,0,0}},
        {{-1,-1,1}, {0,0,1}, {0,0}, {1,0,0}}, {{1,-1,1}, {0,0,1}, {1,0}, {1,0,0}}, {{1,1,1}, {0,0,1}, {1,1}, {1,0,0}}, {{-1,-1,1}, {0,0,1}, {0,0}, {1,0,0}}, {{1,1,1}, {0,0,1}, {1,1}, {1,0,0}}, {{-1,1,1}, {0,0,1}, {0,1}, {1,0,0}},
        {{-1,-1,-1}, {-1,0,0}, {0,0}, {0,0,1}}, {{-1,1,-1}, {-1,0,0}, {1,0}, {0,0,1}}, {{-1,1,1}, {-1,0,0}, {1,1}, {0,0,1}}, {{-1,-1,-1}, {-1,0,0}, {0,0}, {0,0,1}}, {{-1,1,1}, {-1,0,0}, {1,1}, {0,0,1}}, {{-1,-1,1}, {-1,0,0}, {0,1}, {0,0,1}},
        {{1,-1,-1}, {1,0,0}, {0,0}, {0,0,-1}}, {{1,1,-1}, {1,0,0}, {1,0}, {0,0,-1}}, {{1,1,1}, {1,0,0}, {1,1}, {0,0,-1}}, {{1,-1,-1}, {1,0,0}, {0,0}, {0,0,-1}}, {{1,1,1}, {1,0,0}, {1,1}, {0,0,-1}}, {{1,-1,1}, {1,0,0}, {0,1}, {0,0,-1}},
        {{-1,-1,-1}, {0,-1,0}, {0,0}, {1,0,0}}, {{1,-1,-1}, {0,-1,0}, {1,0}, {1,0,0}}, {{1,-1,1}, {0,-1,0}, {1,1}, {1,0,0}}, {{-1,-1,-1}, {0,-1,0}, {0,0}, {1,0,0}}, {{1,-1,1}, {0,-1,0}, {1,1}, {1,0,0}}, {{-1,-1,1}, {0,-1,0}, {0,1}, {1,0,0}},
        {{-1,1,-1}, {0,1,0}, {0,0}, {1,0,0}}, {{1,1,-1}, {0,1,0}, {1,0}, {1,0,0}}, {{1,1,1}, {0,1,0}, {1,1}, {1,0,0}}, {{-1,1,-1}, {0,1,0}, {0,0}, {1,0,0}}, {{1,1,1}, {0,1,0}, {1,1}, {1,0,0}}, {{-1,1,1}, {0,1,0}, {0,1}, {1,0,0}}
    };
    std::vector<uint32_t> i(36);
    for(uint32_t j=0; j<36; ++j) {
        i[j] = j;
        outBounds.expand(v[j].pos);
    }
    return engine.createMesh(v, i);
}

int main() {
    glfwInit();
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    GLFWwindow* window = glfwCreateWindow(1280, 720, "Vulkan Frustum Culling & Octree", nullptr, nullptr);

    Input input;
    input.init(window);

    Engine engine;
    RenderingSystem rs;
    engine.init(window);
    rs.init(engine);

    AABB cubeLocalBounds;
    MeshHandle cubeMesh = createCubeMesh(engine, cubeLocalBounds);

    std::vector<SceneObject> staticObjects;
    std::vector<FallingFlashlight> droppedLights;

    bool fPressedLastFrame = false;
    bool key1Last = false, key2Last = false, key3Last = false;

    const float GRAVITY = -9.81f;
    const float FLOOR_Y = 0.05f;

    std::cout << "Loading Sponza..." << '\n';
    try {
        auto sponza = loadOBJ(engine, "assets/sponza.obj", false);
        sponza.transform = glm::scale(glm::mat4(1.0f), glm::vec3(0.01f));
        staticObjects.push_back(std::move(sponza));
        std::cout << "Sponza loaded successfully!" << '\n';
    } catch (const std::exception& e) {
        std::cerr << "Error loading model: " << e.what() << '\n';
    }

    std::cout << "Generating 5000 random objects for culling test..." << '\n';
    SubMesh cubeSubMesh;
    cubeSubMesh.mesh = cubeMesh;
    cubeSubMesh.texture = engine.createWhiteTexture();
    cubeSubMesh.normalMap = engine.createDefaultNormalTexture();
    cubeSubMesh.dispMap = engine.createBlackTexture();
    cubeSubMesh.dispScale = 0.0f;
    cubeSubMesh.matSet = engine.createMaterialSet(cubeSubMesh.texture, cubeSubMesh.normalMap, cubeSubMesh.dispMap);

    for (int i = 0; i < 5000; ++i) {
        SceneObject cubeObj;
        cubeObj.submeshes.push_back(cubeSubMesh);
        cubeObj.unlit = false;
        cubeObj.localBounds = cubeLocalBounds;

        float rx = ((rand() % 2000) / 10.0f) - 100.0f;
        float ry = ((rand() % 400) / 10.0f);
        float rz = ((rand() % 2000) / 10.0f) - 100.0f;

        cubeObj.transform = glm::translate(glm::mat4(1.0f), glm::vec3(rx, ry, rz)) * glm::scale(glm::mat4(1.0f), glm::vec3(0.5f));
        cubeObj.unlitColor = glm::vec4((rand()%100)/100.f, (rand()%100)/100.f, (rand()%100)/100.f, 1.0f);

        staticObjects.push_back(cubeObj);
    }
    std::cout << "Random objects generated." << '\n';

    std::cout << "Building Octree..." << '\n';
    Octree sceneOctree;
    sceneOctree.build(staticObjects);
    std::cout << "Octree built successfully." << '\n';

    Camera camera;
    camera.position = glm::vec3(0.0f, 1.5f, 0.0f);
    double lastTime = glfwGetTime();
    double statTimer = 0.0;

    CullingMode currentMode = CULLING_OCTREE;
    std::cout << "\nControls:\n"
              << "[1] No Culling\n"
              << "[2] Brute-force Frustum Culling\n"
              << "[3] Octree Frustum Culling\n"
              << "[F] Drop flashlight\n";

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
        statTimer += dt;
        camera.update(input, dt);

        bool k1 = input.isKeyDown(GLFW_KEY_1);
        bool k2 = input.isKeyDown(GLFW_KEY_2);
        bool k3 = input.isKeyDown(GLFW_KEY_3);
        if (k1 && !key1Last) currentMode = CULLING_NONE;
        if (k2 && !key2Last) currentMode = CULLING_BRUTE_FORCE;
        if (k3 && !key3Last) currentMode = CULLING_OCTREE;
        key1Last = k1; key2Last = k2; key3Last = k3;

        bool fIsDown = input.isKeyDown(GLFW_KEY_F);
        if (fIsDown && !fPressedLastFrame) {
            FallingFlashlight fl;
            fl.position = camera.position;
            fl.velocity = camera.front() * 10.0f;
            fl.color = glm::vec3((rand()%100)/100.f, (rand()%100)/100.f, (rand()%100)/100.f) * 2.0f + 0.5f;

            fl.object.submeshes.push_back(cubeSubMesh);
            fl.object.unlit = true;
            fl.object.unlitColor = glm::vec4(fl.color, 1.0f);
            fl.object.localBounds = cubeLocalBounds;
            droppedLights.push_back(fl);
        }
        fPressedLastFrame = fIsDown;

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

        std::vector<LightData> allLights;
        allLights.push_back(Light::makeDirectional({-0.2f, -1.0f, 0.1f}, {1.0f, 0.95f, 0.9f}, 3.0f, true, 0));

        float px = 8.0f * (float)std::cos(now * 0.5);
        float pz = 3.0f * (float)std::sin(now * 0.5);
        allLights.push_back(Light::makePoint({px, 1.5f, pz}, {0.5f, 0.7f, 1.0f}, 2.0f, 15.0f));

        allLights.push_back(Light::makeSpot({0.0f, 8.0f, 0.0f}, {0.0f, -1.0f, 0.0f}, 15.0f, 35.0f, {1.0f, 0.8f, 0.6f}, 5.0f, 25.0f, true, 1));

        for (const auto& fl : droppedLights) {
            allLights.push_back(Light::makePoint(fl.position, fl.color, 4.0f, 10.0f));
        }

        if (allLights.size() > 64) allLights.resize(64);
        rs.setLights(allLights);

        auto ext = engine.getSwapExtent();
        glm::mat4 viewProj = camera.projection((float)ext.width / (float)ext.height) * camera.view();
        Frustum cameraFrustum;
        cameraFrustum.extract(viewProj);

        std::vector<const SceneObject*> visibleObjects;

        if (currentMode == CULLING_NONE) {
            for (const auto& obj : staticObjects) visibleObjects.push_back(&obj);
        }
        else if (currentMode == CULLING_BRUTE_FORCE) {
            for (const auto& obj : staticObjects) {
                if (cameraFrustum.intersects(obj.getWorldBounds())) {
                    visibleObjects.push_back(&obj);
                }
            }
        }
        else if (currentMode == CULLING_OCTREE) {
            sceneOctree.query(cameraFrustum, visibleObjects);
        }

        for (const auto& fl : droppedLights) {
            if (currentMode == CULLING_NONE || cameraFrustum.intersects(fl.object.getWorldBounds())) {
                visibleObjects.push_back(&fl.object);
            }
        }

        if (statTimer >= 1.0) {
            statTimer = 0.0;
            const char* modeStr = (currentMode == CULLING_NONE) ? "NONE" :
                                  (currentMode == CULLING_BRUTE_FORCE) ? "BRUTE-FORCE" : "OCTREE";
            std::cout << "Mode: " << modeStr
                      << " | Visible: " << visibleObjects.size()
                      << " / " << (staticObjects.size() + droppedLights.size()) << "\n";
        }

        FrameContext ctx = engine.beginFrame();
        if (!ctx.valid) {
            engine.recreateSwapchain();
            rs.onResize(engine);
            input.clearResized();
            continue;
        }

        rs.recordFrame(ctx.cmd, ctx.imageIndex, ctx.frameIndex, camera, visibleObjects, engine, (float)now);
        engine.endFrame(ctx);
    }

    rs.cleanup(engine);
    engine.cleanup();
    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}
