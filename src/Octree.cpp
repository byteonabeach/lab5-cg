#include "Octree.h"
#include "Engine.h"

static bool boxContains(const AABB& outer, const AABB& inner) {
    return inner.minVal.x >= outer.minVal.x && inner.maxVal.x <= outer.maxVal.x &&
           inner.minVal.y >= outer.minVal.y && inner.maxVal.y <= outer.maxVal.y &&
           inner.minVal.z >= outer.minVal.z && inner.maxVal.z <= outer.maxVal.z;
}

void Octree::build(const std::vector<SceneObject>& allObjects) {
    root.reset();
    if (allObjects.empty()) return;

    AABB sceneBounds;
    for (const auto& obj : allObjects) sceneBounds.expand(obj.worldBounds);
    sceneBounds.minVal -= glm::vec3(1.0f);
    sceneBounds.maxVal += glm::vec3(1.0f);

    root = std::make_unique<OctreeNode>(sceneBounds);
    for (const auto& obj : allObjects) insert(root.get(), &obj, obj.worldBounds, 0);
}

void Octree::buildFromPointers(const std::vector<const SceneObject*>& objectPtrs) {
    root.reset();
    if (objectPtrs.empty()) return;

    AABB sceneBounds;
    for (const auto* obj : objectPtrs) sceneBounds.expand(obj->worldBounds);
    sceneBounds.minVal -= glm::vec3(1.0f);
    sceneBounds.maxVal += glm::vec3(1.0f);

    root = std::make_unique<OctreeNode>(sceneBounds);
    for (const auto* obj : objectPtrs) insert(root.get(), obj, obj->worldBounds, 0);
}

void Octree::insert(OctreeNode* node, const SceneObject* obj, const AABB& objBounds, int depth) {
    if (node->isLeaf) {
        if (node->objects.size() < MAX_OBJECTS_PER_NODE || depth >= MAX_DEPTH) {
            node->objects.push_back(obj);
            return;
        }

        node->split();
        auto oldObjects = std::move(node->objects);
        node->objects.clear();

        for (const auto* oldObj : oldObjects) {
            bool placed = false;
            for (int i = 0; i < 8; ++i) {
                if (boxContains(node->children[i]->bounds, oldObj->worldBounds)) {
                    insert(node->children[i].get(), oldObj, oldObj->worldBounds, depth + 1);
                    placed = true;
                    break;
                }
            }
            if (!placed) {
                node->objects.push_back(oldObj);
            }
        }
    }

    bool placed = false;
    for (int i = 0; i < 8; ++i) {
        if (boxContains(node->children[i]->bounds, objBounds)) {
            insert(node->children[i].get(), obj, objBounds, depth + 1);
            placed = true;
            break;
        }
    }
    if (!placed) {
        node->objects.push_back(obj);
    }
}

void Octree::query(const Frustum& frustum, std::vector<const SceneObject*>& result, uint32_t frameId) const {
    if (!root) return;
    queryNode(root.get(), frustum, result, frameId);
}

void Octree::queryNode(const OctreeNode* node, const Frustum& frustum, std::vector<const SceneObject*>& result, uint32_t frameId) const {
    if (!frustum.intersects(node->bounds)) return;

    for (const auto* obj : node->objects) {
        if (obj->lastQueryFrame != frameId) {
            if (frustum.intersects(obj->worldBounds)) {
                obj->lastQueryFrame = frameId;
                result.push_back(obj);
            }
        }
    }

    if (!node->isLeaf) {
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
