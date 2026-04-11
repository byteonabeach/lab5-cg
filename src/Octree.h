#pragma once
#include "Culling.h"
#include <vector>
#include <memory>

class SceneObject;

class OctreeNode {
public:
    AABB bounds;
    std::vector<const SceneObject*> objects;
    std::unique_ptr<OctreeNode> children[8];
    bool isLeaf = true;

    OctreeNode(const AABB& b) : bounds(b) {}

    void split() {
        glm::vec3 center = (bounds.minVal + bounds.maxVal) * 0.5f;
        glm::vec3 size = (bounds.maxVal - bounds.minVal) * 0.5f;

        for (int i = 0; i < 8; ++i) {
            glm::vec3 offset(
                (i & 1) ? size.x : 0,
                (i & 2) ? size.y : 0,
                (i & 4) ? size.z : 0
            );
            AABB childBounds;
            childBounds.minVal = bounds.minVal + offset;
            childBounds.maxVal = childBounds.minVal + size;
            children[i] = std::make_unique<OctreeNode>(childBounds);
        }
        isLeaf = false;
    }
};

class Octree {
public:
    static constexpr int MAX_OBJECTS_PER_NODE = 16;
    static constexpr int MAX_DEPTH = 6;

    std::unique_ptr<OctreeNode> root;

    void build(const std::vector<SceneObject>& allObjects);
    void query(const Frustum& frustum, std::vector<const SceneObject*>& result) const;

private:
    void insert(OctreeNode* node, const SceneObject* obj, const AABB& objBounds, int depth);
    void queryNode(const OctreeNode* node, const Frustum& frustum, std::vector<const SceneObject*>& result) const;
};
