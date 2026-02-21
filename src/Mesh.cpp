#define TINYOBJLOADER_IMPLEMENTATION
#include <tiny_obj_loader.h>
#include <unordered_map>
#include "Mesh.h"
#include <filesystem>
#include <stdexcept>
#include <iostream>
#include <fstream>
#include <sstream>

namespace fs = std::filesystem;

static std::string readFile(const std::string& path) {
    std::ifstream f(path);
    if (!f) throw std::runtime_error("cannot open: " + path);
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

static std::string readFirstMtlName(const std::string& mtlText) {
    std::istringstream ss(mtlText);
    std::string line;
    while (std::getline(ss, line)) {
        if (line.rfind("newmtl", 0) == 0) {
            std::istringstream ls(line);
            std::string token, name;
            ls >> token >> name;
            return name;
        }
    }
    return {};
}

static std::string patchOBJ(const std::string& objText, const std::string& realMatName) {
    std::istringstream in(objText);
    std::ostringstream out;
    std::string line;
    while (std::getline(in, line)) {
        if (line.rfind("mtllib", 0) == 0);
        else if (line.rfind("usemtl", 0) == 0)
            out << "usemtl " << realMatName << "\n";
        else
            out << line << "\n";
    }
    return out.str();
}

std::vector<MeshData> loadOBJ(const std::string& path) {
    fs::path objDir = fs::path(path).parent_path();

    std::string mtlPath;
    for (auto& e : fs::directory_iterator(objDir))
        if (e.path().extension() == ".mtl") { mtlPath = e.path().string(); break; }

    tinyobj::ObjReaderConfig cfg;
    cfg.mtl_search_path = objDir.string();
    cfg.triangulate     = true;

    tinyobj::ObjReader reader;

    if (!mtlPath.empty()) {
        std::string mtlText     = readFile(mtlPath);
        std::string realMatName = readFirstMtlName(mtlText);
        std::string objText     = patchOBJ(readFile(path), realMatName);

        std::cout << "using mtl: " << mtlPath << "\n";
        std::cout << "patching usemtl -> '" << realMatName << "'\n";

        if (!reader.ParseFromString(objText, mtlText, cfg))
            throw std::runtime_error(reader.Error());
    } else {
        if (!reader.ParseFromFile(path, cfg))
            throw std::runtime_error(reader.Error());
    }

    if (!reader.Warning().empty())
        std::cerr << "[obj warning] " << reader.Warning() << "\n";

    auto& attrib    = reader.GetAttrib();
    auto& shapes    = reader.GetShapes();
    auto& materials = reader.GetMaterials();

    std::cout << "materials found: " << materials.size() << "\n";
    for (auto& mat : materials)
        std::cout << "  mat: '" << mat.name << "' tex: '" << mat.diffuse_texname << "'\n";

    std::unordered_map<int, MeshData> groups;

    for (auto& shape : shapes) {
        size_t offset = 0;
        for (size_t fi = 0; fi < shape.mesh.num_face_vertices.size(); fi++) {
            int fv    = shape.mesh.num_face_vertices[fi];
            int matId = shape.mesh.material_ids.empty() ? -1 : shape.mesh.material_ids[fi];

            MeshData& md = groups[matId];

            if (md.texturePath.empty() && matId >= 0 && matId < (int)materials.size()) {
                auto& tex = materials[matId].diffuse_texname;
                if (!tex.empty())
                    md.texturePath = objDir.string() + "/" + tex;
            }

            for (int v = 0; v < fv; v++) {
                auto idx = shape.mesh.indices[offset + v];
                Vertex vert{};

                vert.pos = {
                    attrib.vertices[3 * idx.vertex_index + 0],
                    attrib.vertices[3 * idx.vertex_index + 1],
                    attrib.vertices[3 * idx.vertex_index + 2],
                };

                if (idx.normal_index >= 0)
                    vert.normal = {
                        attrib.normals[3 * idx.normal_index + 0],
                        attrib.normals[3 * idx.normal_index + 1],
                        attrib.normals[3 * idx.normal_index + 2],
                    };

                if (idx.texcoord_index >= 0)
                    vert.uv = {
                        attrib.texcoords[2 * idx.texcoord_index + 0],
                        1.f - attrib.texcoords[2 * idx.texcoord_index + 1],
                    };

                md.indices.push_back(md.vertices.size());
                md.vertices.push_back(vert);
            }
            offset += fv;
        }
    }

    std::vector<MeshData> out;
    for (auto& [id, md] : groups) {
        if (md.vertices.empty() || md.indices.empty()) continue;
        std::cout << "  mesh group " << id << ": "
                  << md.vertices.size() << " verts, tex: "
                  << (md.texturePath.empty() ? "(none)" : md.texturePath) << "\n";
        out.push_back(std::move(md));
    }

    std::cout << "loaded " << path << " (" << out.size() << " meshes)\n";
    return out;
}
