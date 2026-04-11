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
#include <iostream>
namespace fs = std::filesystem;

struct FallingFlashlight { glm::vec3 position, velocity, color; SceneObject object; };
enum CullingMode { CULLING_NONE = 0, CULLING_BRUTE_FORCE = 1, CULLING_OCTREE = 2 };

static std::string normSlashes(std::string p) { for (char& c : p) if (c == '\\') c = '/'; return p; }
static std::string findTexture(const std::string& name, const fs::path& baseDir) {
    if (name.empty()) return {}; std::string n = normSlashes(name); if (fs::exists(n)) return n; if (fs::exists(baseDir / n)) return (baseDir / n).string();
    auto fn = fs::path(n).filename(); if (fs::exists(baseDir / fn)) return (baseDir / fn).string(); if (fs::exists(baseDir / "textures" / fn)) return (baseDir / "textures" / fn).string(); return {};
}

static SceneObject loadOBJ(Engine& engine, const std::string& objPath) {
    fs::path basePath = fs::path(objPath).parent_path(); tinyobj::ObjReaderConfig cfg; cfg.mtl_search_path = basePath.string(); cfg.triangulate = true;
    tinyobj::ObjReader reader; if (!reader.ParseFromFile(objPath, cfg)) throw std::runtime_error(reader.Error());
    const auto& attrib = reader.GetAttrib(); const auto& shapes = reader.GetShapes(); const auto& materials = reader.GetMaterials();
    TextureHandle white = engine.createWhiteTexture(), defNorm = engine.createDefaultNormalTexture(), black = engine.createBlackTexture();
    SceneObject obj; for (const auto& shape : shapes) {
        std::unordered_map<int, std::pair<std::vector<Vertex>, std::vector<uint32_t>>> batches; size_t off = 0;
        for (size_t f = 0; f < shape.mesh.num_face_vertices.size(); ++f) {
            int matID = shape.mesh.material_ids.empty() ? -1 : shape.mesh.material_ids[f]; auto& [verts, inds] = batches[matID];
            for (int v = 0; v < 3; ++v) {
                tinyobj::index_t idx = shape.mesh.indices[off + v]; Vertex vert{}; vert.pos = {attrib.vertices[3*idx.vertex_index+0], attrib.vertices[3*idx.vertex_index+1], attrib.vertices[3*idx.vertex_index+2]};
                obj.localBounds.expand(vert.pos); if (idx.normal_index >= 0) vert.normal = {attrib.normals[3*idx.normal_index+0], attrib.normals[3*idx.normal_index+1], attrib.normals[3*idx.normal_index+2]};
                if (idx.texcoord_index >= 0) vert.texCoord = {attrib.texcoords[2*idx.texcoord_index+0], 1.0f - attrib.texcoords[2*idx.texcoord_index+1]}; inds.push_back((uint32_t)verts.size()); verts.push_back(vert);
            } off += 3;
        }
        for (auto& [matID, pair] : batches) {
            auto& [verts, inds] = pair; for (size_t i = 0; i < inds.size(); i += 3) {
                Vertex &v0 = verts[inds[i]], &v1 = verts[inds[i+1]], &v2 = verts[inds[i+2]]; glm::vec3 e1 = v1.pos - v0.pos, e2 = v2.pos - v0.pos; glm::vec2 duv1 = v1.texCoord - v0.texCoord, duv2 = v2.texCoord - v0.texCoord;
                float f = 1.0f / (duv1.x * duv2.y - duv2.x * duv1.y + 0.0001f); glm::vec3 t = glm::normalize(f * (duv2.y * e1 - duv1.y * e2)); v0.tangent = v1.tangent = v2.tangent = t;
            }
            SubMesh sm; sm.mesh = engine.createMesh(verts, inds); TextureHandle tDiff = white, tNorm = defNorm, tDisp = black; sm.dispScale = 0; sm.isCurtain = false;
            if (matID >= 0) {
                auto pDiff = findTexture(materials[matID].diffuse_texname, basePath); auto pNorm = findTexture(materials[matID].bump_texname, basePath); if (pNorm.empty()) pNorm = findTexture(materials[matID].normal_texname, basePath);
                auto pDisp = findTexture(materials[matID].displacement_texname, basePath); if (!pDiff.empty()) { tDiff = engine.loadTexture(pDiff); if (pDiff.find("curtain") != std::string::npos) sm.isCurtain = true; }
                if (!pNorm.empty()) tNorm = engine.loadTexture(pNorm); if (!pDisp.empty()) { tDisp = engine.loadTexture(pDisp); sm.dispScale = 0.05f; }
            }
            sm.texture = tDiff; sm.normalMap = tNorm; sm.dispMap = tDisp; sm.matSet = engine.createMaterialSet(tDiff, tNorm, tDisp); obj.submeshes.push_back(sm);
        }
    } return obj;
}

static MeshHandle createCubeMesh(Engine& engine, AABB& out) {
    std::vector<Vertex> v; std::vector<uint32_t> inds;
    glm::vec3 p[8] = {{-1,-1,-1},{1,-1,-1},{1,1,-1},{-1,1,-1},{-1,-1,1},{1,-1,1},{1,1,1},{-1,1,1}};
    int f[6][4] = {{0,1,2,3},{4,5,6,7},{0,4,7,3},{1,5,6,2},{0,1,5,4},{3,2,6,7}};
    glm::vec3 n[6] = {{0,0,-1},{0,0,1},{-1,0,0},{1,0,0},{0,-1,0},{0,1,0}};
    for(int i=0; i<6; ++i) {
        for(int j=0; j<4; ++j) {
            Vertex vt{}; vt.pos=p[f[i][j]]; vt.normal=n[i]; vt.texCoord={(j==1||j==2)?1.f:0.f, (j==2||j==3)?1.f:0.f};
            v.push_back(vt); out.expand(vt.pos);
        }
        uint32_t b=(uint32_t)i*4; inds.insert(inds.end(), {b,b+1,b+2,b,b+2,b+3});
    }
    return engine.createMesh(v, inds);
}

int main() {
    const float GRAV = -9.81f, FLOOR = 0.05f; glfwInit(); glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    GLFWwindow* window = glfwCreateWindow(1280, 720, "Vulkan Octree Debugger", nullptr, nullptr);
    Input in; in.init(window); Engine eng; RenderingSystem rs; eng.init(window); rs.init(eng);
    AABB cubeBounds; MeshHandle cube = createCubeMesh(eng, cubeBounds);
    std::vector<SceneObject> statObjs; std::vector<FallingFlashlight> dynLights;
    auto sponza = loadOBJ(eng, "assets/sponza.obj"); sponza.transform = glm::scale(glm::mat4(1.0f), glm::vec3(0.01f)); sponza.updateWorldBounds(); statObjs.push_back(std::move(sponza));
    SubMesh cubeSM{cube, eng.createWhiteTexture(), eng.createDefaultNormalTexture(), eng.createBlackTexture()};
    cubeSM.matSet = eng.createMaterialSet(cubeSM.texture, cubeSM.normalMap, cubeSM.dispMap);
    for (int i = 0; i < 5000; ++i) { SceneObject c; c.submeshes.push_back(cubeSM); c.localBounds = cubeBounds; c.transform = glm::translate(glm::mat4(1.0f), glm::vec3(((rand()%2000)/10.f)-100.f, (rand()%400)/10.f, ((rand()%2000)/10.f)-100.f)) * glm::scale(glm::mat4(1.0f), glm::vec3(0.5f)); c.updateWorldBounds(); statObjs.push_back(c); }
    Octree statTree; statTree.build(statObjs); Octree dynTree;
    Camera cam; cam.position = {0, 1.5f, 0}; double last = glfwGetTime(); uint32_t fid = 1; bool drawDbg = false, fLast = false, k4Last = false, k1L=false, k2L=false, k3L=false; CullingMode mode = CULLING_OCTREE;
    std::cout << "Controls: [1-3] Culling Mode, [4] Toggle Octree Grid, [F] Drop Light" << std::endl;
    while (!glfwWindowShouldClose(window)) {
        in.update(); if (in.wasResized()) { eng.recreateSwapchain(); rs.onResize(eng); in.clearResized(); }
        double now = glfwGetTime(); float dt = (float)(now - last); last = now; cam.update(in, dt);
        if (in.isKeyDown(GLFW_KEY_1) && !k1L) { mode = CULLING_NONE; std::cout << "Mode: No Culling" << std::endl; } k1L=in.isKeyDown(GLFW_KEY_1);
        if (in.isKeyDown(GLFW_KEY_2) && !k2L) { mode = CULLING_BRUTE_FORCE; std::cout << "Mode: Brute Force Frustum Culling" << std::endl; } k2L=in.isKeyDown(GLFW_KEY_2);
        if (in.isKeyDown(GLFW_KEY_3) && !k3L) { mode = CULLING_OCTREE; std::cout << "Mode: Octree Optimized Culling" << std::endl; } k3L=in.isKeyDown(GLFW_KEY_3);
        if (in.isKeyDown(GLFW_KEY_4) && !k4Last) { drawDbg = !drawDbg; std::cout << "Debug Grid: " << (drawDbg ? "ENABLED" : "DISABLED") << std::endl; } k4Last = in.isKeyDown(GLFW_KEY_4);
        if (in.isKeyDown(GLFW_KEY_F) && !fLast) { std::cout << "Dropped Flashlight" << std::endl; FallingFlashlight fl{cam.position, cam.front()*12.f, glm::vec3((rand()%100)/100.f, (rand()%100)/100.f, (rand()%100)/100.f)*3.f}; fl.object.submeshes.push_back(cubeSM); fl.object.unlit = true; fl.object.unlitColor = glm::vec4(fl.color, 1.f); fl.object.localBounds = cubeBounds; fl.object.updateWorldBounds(); dynLights.push_back(fl); }
        fLast = in.isKeyDown(GLFW_KEY_F);
        std::vector<const SceneObject*> dynPtrs; for (auto& fl : dynLights) { if (fl.position.y > FLOOR) { fl.velocity.y += GRAV * dt; fl.position += fl.velocity * dt; fl.object.transform = glm::translate(glm::mat4(1.0f), fl.position) * glm::scale(glm::mat4(1.0f), glm::vec3(0.15f)); fl.object.updateWorldBounds(); } dynPtrs.push_back(&fl.object); }
        if (mode == CULLING_OCTREE) dynTree.buildFromPointers(dynPtrs);
        std::vector<LightData> lights; lights.push_back(Light::makeDirectional({-0.2f,-1,0.1f}, {1,0.9f,0.8f}, 2.f, true, 0));
        for (const auto& fl : dynLights) lights.push_back(Light::makePoint(fl.position, fl.color, 5.f, 12.f)); rs.setLights(lights);
        Frustum fr; auto ext = eng.getSwapExtent(); fr.extract(cam.projection((float)ext.width/(float)ext.height) * cam.view());
        std::vector<const SceneObject*> vis; uint32_t qid = fid++;
        if (mode == CULLING_NONE) { for(const auto& o:statObjs) vis.push_back(&o); for(const auto* o:dynPtrs) vis.push_back(o); }
        else if (mode == CULLING_BRUTE_FORCE) { for(const auto& o:statObjs) if(fr.intersects(o.worldBounds)) vis.push_back(&o); for(const auto* o:dynPtrs) if(fr.intersects(o->worldBounds)) vis.push_back(o); }
        else { statTree.query(fr, vis, qid); dynTree.query(fr, vis, qid); }
        std::vector<AABB> sBox, dBox; if (drawDbg) { statTree.getActiveNodes(sBox); dynTree.getActiveNodes(dBox); }
        FrameContext ctx = eng.beginFrame(); if (ctx.valid) { rs.recordFrame(ctx.cmd, ctx.imageIndex, ctx.frameIndex, cam, vis, eng, (float)now, cube, sBox, dBox); eng.endFrame(ctx); }
    } rs.cleanup(eng); eng.cleanup(); glfwDestroyWindow(window); glfwTerminate(); return 0;
}
