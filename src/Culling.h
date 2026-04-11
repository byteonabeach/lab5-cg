#pragma once
#include <glm/glm.hpp>
#include <vector>

struct AABB {
    glm::vec3 minVal = glm::vec3(1e30f);
    glm::vec3 maxVal = glm::vec3(-1e30f);

    void expand(const glm::vec3& p) {
        minVal = glm::min(minVal, p);
        maxVal = glm::max(maxVal, p);
    }

    void expand(const AABB& other) {
        minVal = glm::min(minVal, other.minVal);
        maxVal = glm::max(maxVal, other.maxVal);
    }

    AABB transform(const glm::mat4& m) const {
        AABB res;
        glm::vec3 corners[8] = {
            {minVal.x, minVal.y, minVal.z}, {maxVal.x, minVal.y, minVal.z},
            {minVal.x, maxVal.y, minVal.z}, {maxVal.x, maxVal.y, minVal.z},
            {minVal.x, minVal.y, maxVal.z}, {maxVal.x, minVal.y, maxVal.z},
            {minVal.x, maxVal.y, maxVal.z}, {maxVal.x, maxVal.y, maxVal.z}
        };
        for (int i = 0; i < 8; ++i) {
            glm::vec4 p = m * glm::vec4(corners[i], 1.0f);
            res.expand(glm::vec3(p) / p.w);
        }
        return res;
    }
};

struct Plane {
    glm::vec3 normal;
    float d;

    void normalize() {
        float length = glm::length(normal);
        normal /= length;
        d /= length;
    }
};

struct Frustum {
    Plane planes[6];

    void extract(const glm::mat4& vp) {
        planes[0].normal = {vp[0][3] + vp[0][0], vp[1][3] + vp[1][0], vp[2][3] + vp[2][0]};
        planes[0].d = vp[3][3] + vp[3][0];

        planes[1].normal = {vp[0][3] - vp[0][0], vp[1][3] - vp[1][0], vp[2][3] - vp[2][0]};
        planes[1].d = vp[3][3] - vp[3][0];

        planes[2].normal = {vp[0][3] + vp[0][1], vp[1][3] + vp[1][1], vp[2][3] + vp[2][1]};
        planes[2].d = vp[3][3] + vp[3][1];

        planes[3].normal = {vp[0][3] - vp[0][1], vp[1][3] - vp[1][1], vp[2][3] - vp[2][1]};
        planes[3].d = vp[3][3] - vp[3][1];

        planes[4].normal = {vp[0][2], vp[1][2], vp[2][2]};
        planes[4].d = vp[3][2];

        planes[5].normal = {vp[0][3] - vp[0][2], vp[1][3] - vp[1][2], vp[2][3] - vp[2][2]};
        planes[5].d = vp[3][3] - vp[3][2];

        for (int i = 0; i < 6; ++i) planes[i].normalize();
    }

    bool intersects(const AABB& box) const {
        for (int i = 0; i < 6; i++) {
            glm::vec3 p = box.minVal;

            if (planes[i].normal.x >= 0) p.x = box.maxVal.x;
            if (planes[i].normal.y >= 0) p.y = box.maxVal.y;
            if (planes[i].normal.z >= 0) p.z = box.maxVal.z;

            if (glm::dot(planes[i].normal, p) + planes[i].d < 0) {
                return false;
            }
        }
        return true;
    }
};
