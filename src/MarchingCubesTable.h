#pragma once
#include <array>
#include <stdexcept>

namespace mc {

constexpr int kTriStride = 16;
constexpr int kMaxTris = kTriStride / 3;

constexpr int kCornerPos[8][3] = {
    {0, 0, 0}, {1, 0, 0}, {1, 1, 0}, {0, 1, 0},
    {0, 0, 1}, {1, 0, 1}, {1, 1, 1}, {0, 1, 1}
};

constexpr int kEdgeCorners[12][2] = {
    {0, 1}, {1, 2}, {2, 3}, {3, 0},
    {4, 5}, {5, 6}, {6, 7}, {7, 4},
    {0, 4}, {1, 5}, {2, 6}, {3, 7}
};

constexpr int kFaceCorners[6][4] = {
    {0, 1, 2, 3}, {4, 5, 6, 7}, {0, 1, 5, 4},
    {3, 2, 6, 7}, {0, 3, 7, 4}, {1, 2, 6, 5}
};

inline int edgeBetween(int a, int b) {
    for (int e = 0; e < 12; ++e) {
        int p = kEdgeCorners[e][0], q = kEdgeCorners[e][1];
        if ((p == a && q == b) || (p == b && q == a)) return e;
    }
    throw std::logic_error("corners do not share an edge");
}

inline std::array<int, 256 * kTriStride> buildTriTable() {
    std::array<int, 256 * kTriStride> table;
    table.fill(-1);

    for (int mask = 1; mask < 255; ++mask) {
        int next[12][2];
        int degree[12] = {};
        for (auto& n : next) n[0] = n[1] = -1;

        auto link = [&](int a, int b) {
            next[a][degree[a]++] = b;
            next[b][degree[b]++] = a;
        };

        for (int f = 0; f < 6; ++f) {
            int inside[4], edge[4];
            for (int k = 0; k < 4; ++k) {
                inside[k] = (mask >> kFaceCorners[f][k]) & 1;
                edge[k] = edgeBetween(kFaceCorners[f][k], kFaceCorners[f][(k + 1) & 3]);
            }
            int active[4], count = 0;
            for (int k = 0; k < 4; ++k)
                if (inside[k] != inside[(k + 1) & 3]) active[count++] = k;

            if (count == 2) {
                link(edge[active[0]], edge[active[1]]);
            } else if (count == 4) {
                for (int k = 0; k < 4; ++k)
                    if (inside[k]) link(edge[(k + 3) & 3], edge[k]);
            }
        }

        bool visited[12] = {};
        int tris = 0;
        int* out = &table[mask * kTriStride];

        for (int start = 0; start < 12; ++start) {
            if (degree[start] == 0 || visited[start]) continue;
            if (degree[start] != 2) throw std::logic_error("open contour in marching cubes table");

            int loop[12], n = 0, prev = -1, cur = start;
            do {
                visited[cur] = true;
                loop[n++] = cur;
                int nx = next[cur][0] != prev ? next[cur][0] : next[cur][1];
                prev = cur;
                cur = nx;
            } while (cur != start);

            for (int i = 1; i + 1 < n; ++i) {
                if (tris >= kMaxTris) throw std::logic_error("marching cubes case exceeds triangle budget");
                out[tris * 3 + 0] = loop[0];
                out[tris * 3 + 1] = loop[i];
                out[tris * 3 + 2] = loop[i + 1];
                ++tris;
            }
        }
    }
    return table;
}

}
