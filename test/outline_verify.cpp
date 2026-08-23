// 独立验证程序：复刻 ChainOutline::rebuildEdges 的面区域周界算法
// 编译：clang++ -O2 -std=c++20 outline_verify.cpp -o outline_verify.exe
// 用法：设置好下方案例后运行，输出每条线段端点与法线，人工核对。

#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <unordered_map>
#include <unordered_set>
#include <vector>

static bool traceEmit = false;

// ---- 以下代码与 ChainOutline.cc 转写一致（std 容器替代 absl） ----
constexpr int kLocalOffset = 2048;

inline uint64_t packCell(int x, int y, int z) {
    return (static_cast<uint64_t>(static_cast<uint32_t>(x + kLocalOffset)) << 42)
         | (static_cast<uint64_t>(static_cast<uint32_t>(y + kLocalOffset)) << 21)
         | static_cast<uint64_t>(static_cast<uint32_t>(z + kLocalOffset));
}
inline uint64_t packEdgeKey(int x, int y, int z, int axis) {
    return packCell(x, y, z) | (static_cast<uint64_t>(axis) << 62);
}

struct EdgeDump {
    int   ax, ay, az, bx, by, bz;
    float n1x, n1y, n1z, n2x, n2y, n2z;
    int   faceCount; // normals.count
};

void rebuildEdges(
    std::unordered_set<uint64_t> const& inSet,
    int                                 anchorLocalX,
    int                                 anchorLocalY,
    int                                 anchorLocalZ,
    std::vector<EdgeDump>&              out
) {
    struct FaceDir {
        int   uAxis, vAxis, wAxis;
        int   wOff;
        float nx, ny, nz;
    };
    static constexpr FaceDir kFaces[6] = {
        {1, 2, 0, 1, 1.0f,  0.0f,  0.0f },
        {1, 2, 0, 0, -1.0f, 0.0f,  0.0f },
        {0, 2, 1, 1, 0.0f,  1.0f,  0.0f },
        {0, 2, 1, 0, 0.0f,  -1.0f, 0.0f },
        {0, 1, 2, 1, 0.0f,  0.0f,  1.0f },
        {0, 1, 2, 0, 0.0f,  0.0f,  -1.0f},
    };
    auto cellFromPlane = [](FaceDir const& f, int u, int v, int wFixed) -> uint64_t {
        int c[3]   = {0, 0, 0};
        c[f.uAxis] = u;
        c[f.vAxis] = v;
        c[f.wAxis] = wFixed;
        return packCell(c[0], c[1], c[2]);
    };

    std::unordered_set<uint64_t> faceSets[6];
    for (uint64_t cell : inSet) {
        int const lx       = static_cast<int>(static_cast<uint32_t>(cell >> 42)) - kLocalOffset;
        int const ly       = static_cast<int>(static_cast<uint32_t>((cell >> 21) & 0x1FFFFFu)) - kLocalOffset;
        int const lz       = static_cast<int>(static_cast<uint32_t>(cell & 0x1FFFFFu)) - kLocalOffset;
        int const coord[3] = {lx, ly, lz};
        for (int d = 0; d < 6; ++d) {
            auto const& f         = kFaces[d];
            int         neigh[3]  = {coord[0], coord[1], coord[2]};
            neigh[f.wAxis]       += f.wOff == 1 ? 1 : -1;
            if (!inSet.contains(packCell(neigh[0], neigh[1], neigh[2]))) {
                faceSets[d].insert(cellFromPlane(f, coord[f.uAxis], coord[f.vAxis], coord[f.wAxis] + f.wOff));
            }
        }
    }

    struct NormalPair {
        std::array<float, 3> n1{};
        std::array<float, 3> n2{};
        int                  count{0};
    };
    std::unordered_map<uint64_t, NormalPair> normals;

    struct PlaneEdge {
        int su, sv, du, dv, ou, ov;
    };
    static constexpr PlaneEdge kPlaneEdges[4] = {
        {0, 0, 1,  0,  0,  -1},
        {1, 0, 0,  1,  1,  0 },
        {1, 1, -1, 0,  0,  1 },
        {0, 1, 0,  -1, -1, 0 },
    };

    for (int d = 0; d < 6; ++d) {
        auto const& f = kFaces[d];
        for (uint64_t cell : faceSets[d]) {
            int const cx     = static_cast<int>(static_cast<uint32_t>(cell >> 42)) - kLocalOffset;
            int const cy     = static_cast<int>(static_cast<uint32_t>((cell >> 21) & 0x1FFFFFu)) - kLocalOffset;
            int const cz     = static_cast<int>(static_cast<uint32_t>(cell & 0x1FFFFFu)) - kLocalOffset;
            auto      comp   = [&](int axis) { return axis == 0 ? cx : axis == 1 ? cy : cz; };
            int const u      = comp(f.uAxis);
            int const v      = comp(f.vAxis);
            int const wFixed = comp(f.wAxis);

            for (auto const& pe : kPlaneEdges) {
                int const nu = u + pe.ou, nv = v + pe.ov;
                if (faceSets[d].contains(cellFromPlane(f, nu, nv, wFixed))) continue;

                int a[3] = {0, 0, 0}, b[3] = {0, 0, 0};
                a[f.uAxis] = u + pe.su;
                a[f.vAxis] = v + pe.sv;
                a[f.wAxis] = wFixed;
                b[f.uAxis] = u + pe.su + pe.du;
                b[f.vAxis] = v + pe.sv + pe.dv;
                b[f.wAxis] = wFixed;

                int const axis = pe.du != 0 ? f.uAxis : f.vAxis;
                // 规范化线段 key：同一条几何线段可能从相邻方向的面区域沿相反方向
                // 发射（顶边/右/左），起点必须取"沿 axis 轴坐标较小的一端"，
                // 否则两个方向产出不同 key -> 去重失败 -> 棱线重复绘制/毛刺。
                uint64_t segKey =
                    a[axis] <= b[axis] ? packEdgeKey(a[0], a[1], a[2], axis) : packEdgeKey(b[0], b[1], b[2], axis);
                // 发射追踪（仅诊断用）
                if (traceEmit) {
                    printf(
                        "EMIT d=%d face(%d,%d,%d) u=%d v=%d w=%d  edge(s%s) seg=(%d,%d,%d)->(%d,%d,%d) axis=%d  "
                        "outer=(%d,%d) inRegion=%d\n",
                        d,
                        cx,
                        cy,
                        cz,
                        u,
                        v,
                        wFixed,
                        pe.su == 0 && pe.sv == 0 && pe.du == 1 ? "bottom"
                        : pe.du == 1 && pe.sv == 0             ? "right"
                        : pe.du == -1                          ? "top"
                                                               : "left",
                        a[0],
                        a[1],
                        a[2],
                        b[0],
                        b[1],
                        b[2],
                        axis,
                        nu,
                        nv,
                        (int)faceSets[d].contains(cellFromPlane(f, nu, nv, wFixed))
                    );
                }
                auto& np = normals[segKey];
                if (np.count == 0) {
                    np.n1 = {f.nx, f.ny, f.nz};
                } else if (np.count == 1) {
                    np.n2 = {f.nx, f.ny, f.nz};
                }
                ++np.count;
            }
        }
    }

    out.reserve(normals.size());
    for (auto& [key, np] : normals) {
        if (np.count == 0) continue;
        uint64_t const axis = key >> 62;
        uint64_t const cell = key & 0x3FFFFFFFFFFFFFFFull;
        int const      ax   = static_cast<int>(static_cast<uint32_t>(cell >> 42)) - kLocalOffset;
        int const      ay   = static_cast<int>(static_cast<uint32_t>((cell >> 21) & 0x1FFFFFu)) - kLocalOffset;
        int const      az   = static_cast<int>(static_cast<uint32_t>(cell & 0x1FFFFFu)) - kLocalOffset;
        if (np.count == 1) np.n2 = np.n1;
        if (axis == 0) {
            out.push_back(
                {ax, ay, az, ax + 1, ay, az, np.n1[0], np.n1[1], np.n1[2], np.n2[0], np.n2[1], np.n2[2], np.count}
            );
        } else if (axis == 1) {
            out.push_back(
                {ax, ay, az, ax, ay + 1, az, np.n1[0], np.n1[1], np.n1[2], np.n2[0], np.n2[1], np.n2[2], np.count}
            );
        } else {
            out.push_back(
                {ax, ay, az, ax, ay, az + 1, np.n1[0], np.n1[1], np.n1[2], np.n2[0], np.n2[1], np.n2[2], np.count}
            );
        }
    }
}

// ASCII 可视化：打印 y=layer 平面上 x-z 网格线段
static void
dumpPlane(char const* title, int layer, std::vector<EdgeDump> const& edges, int minX, int maxX, int minZ, int maxZ) {
    printf("%s (y=%d)\n", title, layer);
    for (int z = maxZ; z >= minZ - 1; --z) {
        for (int x = minX - 1; x <= maxX; ++x) {
            bool has = false;
            for (auto const& e : edges)
                if (e.ay == layer && e.by == layer && e.az == z && e.bz == z
                    && ((e.ax == x && e.bx == x + 1) || (e.bx == x && e.ax == x + 1))) {
                    has = true;
                    break;
                }
            printf(has ? "+---" : "+   ");
        }
        printf("+ z=%d\n", z);
        for (int x = minX - 1; x <= maxX; ++x) {
            bool has = false;
            for (auto const& e : edges)
                if (e.ay == layer && e.by == layer && e.ax == x && e.bx == x
                    && ((e.az == z && e.bz == z + 1) || (e.bz == z && e.az == z + 1))) {
                    has = true;
                    break;
                }
            printf(has ? "|  " : "   ");
        }
        printf(" z=%d..%d\n", z, z + 1);
    }
}

// ---- 测试 ----
int main() {
    // 案例：5x8 水平平面 y=0, x∈[0,5), z∈[0,8)，anchor=(2,0,3)（平面内部）
    // 期望外壳：顶面 26 + 底面 26 + 竖棱 4 = 56 条
    {
        std::unordered_set<uint64_t> inSet;
        inSet.insert(packCell(0, 0, 0)); // anchor
        for (int x = -2; x <= 2; ++x)
            for (int z = -3; z <= 4; ++z) inSet.insert(packCell(x, 0, z));

        std::vector<EdgeDump> edges;
        rebuildEdges(inSet, 0, 0, 0, edges);
        printf("CASE1 5x8 plane (anchor inside): %zu edges (expect 56)\n", edges.size());
    }

    // 案例2：单方块（1 个方块）-> 12 条边（带发射追踪）
    {
        traceEmit = true;
        std::unordered_set<uint64_t> inSet{packCell(0, 0, 0)};
        std::vector<EdgeDump>        edges;
        rebuildEdges(inSet, 0, 0, 0, edges);
        traceEmit = false;
        printf("\nCASE2 single block: %zu edges (expect 12)\n", edges.size());
        for (auto const& e : edges) {
            printf("  (%d,%d,%d)->(%d,%d,%d)\n", e.ax, e.ay, e.az, e.bx, e.by, e.bz);
        }
    }

    // 案例3：L 形（5x5 缺一角）：x∈[0,4], y=0, z∈[0,4] 去掉 (4,0,4)
    {
        std::unordered_set<uint64_t> inSet{packCell(0, 0, 0)};
        for (int x = 0; x < 5; ++x)
            for (int z = 0; z < 5; ++z)
                if (!(x == 4 && z == 4)) inSet.insert(packCell(x, 0, z));
        std::vector<EdgeDump> edges;
        rebuildEdges(inSet, 0, 0, 0, edges);
        printf("\nCASE3 L-shape (5x5 missing corner): %zu edges\n", edges.size());
        dumpPlane("L-SHAPE top face (y=1)", 1, edges, 0, 5, 0, 5);
        dumpPlane("L-SHAPE bottom face (y=0)", 0, edges, 0, 5, 0, 5);
        int verticals = 0;
        for (auto const& e : edges)
            if (e.ay != e.by) ++verticals;
        printf("vertical edges: %d\n", verticals);
    }

    // 案例4：双层 3x3x2 立方体柱（y=0 与 y=1 各 3x3）-> 期望 顶 12 + 底 12 + 竖棱 12 = 36
    {
        std::unordered_set<uint64_t> inSet{packCell(0, 0, 0)};
        for (int y = 0; y < 2; ++y)
            for (int x = 0; x < 3; ++x)
                for (int z = 0; z < 3; ++z) inSet.insert(packCell(x, y, z));
        std::vector<EdgeDump> edges;
        rebuildEdges(inSet, 0, 0, 0, edges);
        printf("\nCASE4 3x3x2 box: %zu edges (expect 36)\n", edges.size());
        int verticals = 0;
        for (auto const& e : edges)
            if (e.ay != e.by) ++verticals;
        printf("  vertical edges: %d (expect 12)\n", verticals);
    }

    // 案例5：楼梯形（staircase）5x5 斜面
    {
        std::unordered_set<uint64_t> inSet;
        for (int i = 0; i < 5; ++i)
            for (int x = 0; x <= i; ++x)
                for (int z = 0; z < 3; ++z) inSet.insert(packCell(x, i, z));
        std::vector<EdgeDump> edges;
        rebuildEdges(inSet, 0, 0, 0, edges);
        printf("\nCASE5 staircase 5x3: %zu edges\n", edges.size());
        dumpPlane("staircase y=1", 1, edges, 0, 5, 0, 3);
        dumpPlane("staircase y=2", 2, edges, 0, 5, 0, 3);
    }

    // 案例6：环形（5x5 挖中心 3x3）-> 外圈 + 内孔
    {
        std::unordered_set<uint64_t> inSet;
        for (int x = 0; x < 5; ++x)
            for (int z = 0; z < 5; ++z)
                if (!(x >= 1 && x <= 3 && z >= 1 && z <= 3)) inSet.insert(packCell(x, 0, z));
        std::vector<EdgeDump> edges;
        rebuildEdges(inSet, 0, 0, 0, edges);
        printf("\nCASE6 ring 5x5 minus 3x3: %zu edges\n", edges.size());
        dumpPlane("ring top face (y=1)", 1, edges, 0, 5, 0, 5);
    }
    return 0;
}
