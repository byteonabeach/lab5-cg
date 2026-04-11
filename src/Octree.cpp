#include "Octree.h"
#include "Engine.h"

void Octree::build(const std::vector<SceneObject>& allObjects) {
    if (allObjects.empty()) return;

    AABB sceneBounds;
    for (const auto& obj : allObjects) {
        sceneBounds.expand(obj.worldBounds);
    }

    sceneBounds.minVal -= glm::vec3(0.1f);
    sceneBounds.maxVal += glm::vec3(0.1f);

    root = std::make_unique<OctreeNode>(sceneBounds);

    for (const auto& obj : allObjects) {
        insert(root.get(), &obj, obj.worldBounds, 0);
    }
}

void Octree::insert(OctreeNode* node, const SceneObject* obj, const AABB& objBounds, int depth) {
    if (node->isLeaf) {
        node->objects.push_back(obj);
        if (node->objects.size() > MAX_OBJECTS_PER_NODE && depth < MAX_DEPTH) {
            node->split();
            auto oldObjects = std::move(node->objects);
            node->objects.clear();

            for (const auto& oldObj : oldObjects) {
                AABB oldBounds = oldObj->worldBounds;
                for (int i = 0; i < 8; ++i) {
                    if (glm::max(node->children[i]->bounds.minVal, oldBounds.minVal).x <= glm::min(node->children[i]->bounds.maxVal, oldBounds.maxVal).x &&
                        glm::max(node->children[i]->bounds.minVal, oldBounds.minVal).y <= glm::min(node->children[i]->bounds.maxVal, oldBounds.maxVal).y &&
                        glm::max(node->children[i]->bounds.minVal, oldBounds.minVal).z <= glm::min(node->children[i]->bounds.maxVal, oldBounds.maxVal).z) {
                        insert(node->children[i].get(), oldObj, oldBounds, depth + 1);
                    }
                }
            }
        }
    } else {
        for (int i = 0; i < 8; ++i) {
            if (glm::max(node->children[i]->bounds.minVal, objBounds.minVal).x <= glm::min(node->children[i]->bounds.maxVal, objBounds.maxVal).x &&
                glm::max(node->children[i]->bounds.minVal, objBounds.minVal).y <= glm::min(node->children[i]->bounds.maxVal, objBounds.maxVal).y &&
                glm::max(node->children[i]->bounds.minVal, objBounds.minVal).z <= glm::min(node->children[i]->bounds.maxVal, objBounds.maxVal).z) {
                insert(node->children[i].get(), obj, objBounds, depth + 1);
            }
        }
    }
}

void Octree::query(const Frustum& frustum, std::vector<const SceneObject*>& result, uint32_t frameId) const {
    if (!root) return;
    queryNode(root.get(), frustum, result, frameId);
}

void Octree::queryNode(const OctreeNode* node, const Frustum& frustum, std::vector<const SceneObject*>& result, uint32_t frameId) const {
    if (!frustum.intersects(node->bounds)) return;

    if (node->isLeaf) {
        for (const auto& obj : node->objects) {
            if (obj->lastQueryFrame != frameId) {
                if (frustum.intersects(obj->worldBounds)) {
                    obj->lastQueryFrame = frameId;
                    result.push_back(obj);
                }
            }
        }
    } else {
        for (int i = 0; i < 8; ++i) {
            queryNode(node->children[i].get(), frustum, result, frameId);
        }
    }
}

void Octree::getActiveNodes(std::vector<AABB>& outNodes) const {
    if (root) collectNodes(root.get(), outNodes);
}

void Octree::collectNodes(const OctreeNode* node, std::vector<AABB>& outNodes) const {
    outNodes.push_back(node->bounds);
    if (!node->isLeaf) {
        for (int i = 0; i < 8; ++i) {
            collectNodes(node->children[i].get(), outNodes);
        }
    }
}
