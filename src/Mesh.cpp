#define TINYOBJLOADER_IMPLEMENTATION
#include <tiny_obj_loader.h>

#include "Mesh.h"
#include <filesystem>
#include <stdexcept>
#include <iostream>
#include <unordered_map>

std::vector<MeshData> loadOBJ(const std::string& path) {
    tinyobj::ObjReaderConfig cfg;
    cfg.mtl_search_path = std::filesystem::path(path).parent_path().string();
    cfg.triangulate     = true;

    tinyobj::ObjReader reader;
    if (!reader.ParseFromFile(path, cfg))
        throw std::runtime_error(reader.Error());

    auto& attrib    = reader.GetAttrib();
    auto& shapes    = reader.GetShapes();
    auto& materials = reader.GetMaterials();

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
                    md.texturePath = cfg.mtl_search_path + "/" + tex;
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
    for (auto& [id, md] : groups)
        out.push_back(std::move(md));

    std::cout << "loaded " << path << " (" << out.size() << " meshes)\n";
    return out;
}
