#include "render/VoxelOutlineRenderer.h"

#include "Global.h"

#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"

#include "mc/client/game/IClientInstance.h"
#include "mc/client/gui/screens/ScreenContext.h"
#include "mc/client/renderer/BaseActorRenderContext.h"
#include "mc/client/renderer/Tessellator.h"
#include "mc/client/renderer/game/LevelRenderer.h"
#include "mc/client/renderer/game/LevelRendererPlayer.h"
#include "mc/deps/core/math/Vec3.h"
#include "mc/deps/core_graphics/enums/DepthWriteMask.h"
#include "mc/deps/core_graphics/enums/PrimitiveMode.h"
#include "mc/deps/minecraft_renderer/renderer/MaterialPtr.h"
#include "mc/deps/minecraft_renderer/renderer/Mesh.h"
#include "mc/deps/minecraft_renderer/renderer/RenderMaterial.h"

#include <cmath>
#include <cstdint>
#include <memory>
#include <vector>

namespace fm::client {
namespace {

// 颜色（均不透明，不依赖 blend）
constexpr int   kWhiteAbgr   = 0xFFFFFFFF;
constexpr int   kHiddenAbgr  = 0xFF969696; // 灰 (150,150,150)：被遮挡 / 背侧
constexpr float kShellOffset = 0.01f;      // 白线沿外侧法线的表面外偏移（格），使深度测试通过

/**
 * @brief 真透视深度状态 RAII。
 *
 * 临时关闭线框材质的深度测试与写入，绘制后还原，避免污染原版选择描边材质。
 */
struct ScopedDepthXRay {
    mce::RenderMaterial* material;
    bool                 savedDepthTest{true};
    mce::DepthWriteMask  savedDepthWrite{static_cast<mce::DepthWriteMask>(1)};

    explicit ScopedDepthXRay(mce::RenderMaterial* mat) : material(mat) {
        if (!material) return;
        auto& desc            = material->depthStencilStateDescription.get();
        savedDepthTest        = desc.depthTestEnabled;
        savedDepthWrite       = desc.depthWriteMask;
        desc.depthTestEnabled = false; // 真透视：关闭深度测试
        desc.depthWriteMask   = static_cast<mce::DepthWriteMask>(0);
    }

    ~ScopedDepthXRay() {
        if (!material) return;
        auto& desc            = material->depthStencilStateDescription.get();
        desc.depthTestEnabled = savedDepthTest;
        desc.depthWriteMask   = savedDepthWrite;
    }
};

/**
 * @brief 仅关闭深度写入的 RAII（深度测试保持原样），防止白线绘制污染场景深度。
 */
struct ScopedDepthWriteOff {
    mce::RenderMaterial* material{nullptr};
    mce::DepthWriteMask  saved{static_cast<mce::DepthWriteMask>(1)};

    explicit ScopedDepthWriteOff(mce::RenderMaterial* mat) : material(mat) {
        if (!material) return;
        auto& desc          = material->depthStencilStateDescription.get();
        saved               = desc.depthWriteMask;
        desc.depthWriteMask = static_cast<mce::DepthWriteMask>(0);
    }
    ~ScopedDepthWriteOff() {
        if (!material) return;
        material->depthStencilStateDescription.get().depthWriteMask = saved;
    }
};

// 锚点相对坐标 [-2048, 2048) 到 uint64 的唯一映射偏移
constexpr int kLocalOffset = 2048;

inline uint64_t packCell(int x, int y, int z) {
    return (static_cast<uint64_t>(static_cast<uint32_t>(x + kLocalOffset)) << 42)
         | (static_cast<uint64_t>(static_cast<uint32_t>(y + kLocalOffset)) << 21)
         | static_cast<uint64_t>(static_cast<uint32_t>(z + kLocalOffset));
}
inline uint64_t packEdgeKey(int x, int y, int z, int axis) {
    return packCell(x, y, z) | (static_cast<uint64_t>(axis) << 62);
}

} // namespace

VoxelOutlineRenderer::VoxelOutlineRenderer() = default;
VoxelOutlineRenderer::~VoxelOutlineRenderer() = default;

void VoxelOutlineRenderer::update(VoxelSet const& view) {
    if (view.generation == edgeGen_ && view.anchor == edgeAnchor_) return;
    if (view.blocks.empty()) {
        // 空集合 = 无物可画；顺带复位重建基准，避免旧几何被下一帧误用
        clear();
        return;
    }
    edgeGen_    = view.generation;
    edgeAnchor_ = view.anchor;
    // 视图仅在本次调用内有效（span 不持有数据），必须立即同步消费
    rebuildEdges(view.blocks, view.anchor);
    meshDirty_ = true;
}

void VoxelOutlineRenderer::clear() {
    edgeGen_    = UINT64_MAX;
    edgeAnchor_ = {};
    edges_.clear();
    meshGray_.reset();
    meshWhite_.reset();
    grayVerts_  = 0;
    whiteVerts_ = 0;
    meshValid_  = false;
    meshDirty_  = false;
}

/**
 * @brief 重建体积外壳轮廓线段集。
 *
 * 算法：暴露面区域周界，聚合相邻方块共享边。
 * 1. 对集合内每个方块 p、每个方向 d：若 p+d 不在集合中，则 p 的 d 面为暴露面；
 *    同方向暴露面构成「平面区域」。
 * 2. 对每个平面区域仅描画外缘：只有「外侧邻接面单元不在本区域」的边产生线段，
 *    区域内部不产生线段。
 * 3. 外缘线以规范化晶格线 key 去重，同一几何线段从相邻面区域沿相反方向发射时，
 *    起点取沿轴较小端，保证唯一 key。
 *
 * 可见性不在此处判定，由渲染时深度缓冲语义决定：真透视灰线 + 深度裁剪白线。
 */
void VoxelOutlineRenderer::rebuildEdges(absl::Span<BlockPos const> blocks, BlockPos const& anchor) {
    edges_.clear();

    absl::flat_hash_set<uint64_t> inSet;
    inSet.reserve(blocks.size() * 2 + 8);
    // 注意：输入集合不含锚点方块（调用方上游搜索不含起点）-必须把锚点
    // 显式算作集合成员，否则锚点会被当成空气 -> 其四周暴露面全部外露，
    // 表现为「锚点方块仍有轮廓框」+ 多余的竖直毛刺线段。
    inSet.insert(packCell(0, 0, 0));
    for (auto const& p : blocks) {
        inSet.insert(packCell(p.x - anchor.x, p.y - anchor.y, p.z - anchor.z));
    }

    // 六个方向的暴露面单元：面单元 key = packCell(三维本地坐标)，其中两轴为平面
    // 坐标、法向轴为平面所在层（方块坐标 + 偏移）
    struct FaceDir {
        int   uAxis, vAxis, wAxis; // 平面两轴 + 法向轴
        int   wOff;                // 平面偏移（0 = 方块自身侧，1 = 法向侧）
        float nx, ny, nz;          // 该方向单位外侧法线
    };
    static constexpr FaceDir kFaces[6] = {
        {1, 2, 0, 1, 1.0f,  0.0f,  0.0f }, // +x
        {1, 2, 0, 0, -1.0f, 0.0f,  0.0f }, // -x
        {0, 2, 1, 1, 0.0f,  1.0f,  0.0f }, // +y
        {0, 2, 1, 0, 0.0f,  -1.0f, 0.0f }, // -y
        {0, 1, 2, 1, 0.0f,  0.0f,  1.0f }, // +z
        {0, 1, 2, 0, 0.0f,  0.0f,  -1.0f}, // -z
    };
    // 平面坐标 (u, v) + 法向层 wFixed -> 三维本地坐标 packCell
    auto cellFromPlane = [](FaceDir const& f, int u, int v, int wFixed) -> uint64_t {
        int c[3]   = {0, 0, 0};
        c[f.uAxis] = u;
        c[f.vAxis] = v;
        c[f.wAxis] = wFixed;
        return packCell(c[0], c[1], c[2]);
    };

    absl::flat_hash_set<uint64_t> faceSets[6];
    // 面单元收集：输入 blocks + 锚点自身（anchor 已在上方加入 inSet 用于邻居判定，
    // 但这里必须同样遍历它贡献暴露面单元 - 否则锚点的 ±y 面不进入顶/底面区域，
    // 顶/底区域在锚点处留洞 -> 锚点方块的顶/底面边缘被误渲染出来）
    auto collectBlockFaces = [&](int lx, int ly, int lz) {
        int const coord[3] = {lx, ly, lz};
        for (int d = 0; d < 6; ++d) {
            auto const& f         = kFaces[d];
            int         neigh[3]  = {coord[0], coord[1], coord[2]};
            neigh[f.wAxis]       += f.wOff == 1 ? 1 : -1;
            if (!inSet.contains(packCell(neigh[0], neigh[1], neigh[2]))) {
                faceSets[d].insert(cellFromPlane(f, coord[f.uAxis], coord[f.vAxis], coord[f.wAxis] + f.wOff));
            }
        }
    };
    collectBlockFaces(0, 0, 0); // 锚点（本地 (0,0,0)）
    for (auto const& p : blocks) {
        collectBlockFaces(p.x - anchor.x, p.y - anchor.y, p.z - anchor.z);
    }

    // 外缘晶格线：规范化 key -> 外侧法线累加（棱线属于两个方向的面区域时取矢量和，
    // 归一化后作为"外侧朝向"，供白线做表面外微小偏移以通过深度测试）
    struct NormalSum {
        float x{0.0f}, y{0.0f}, z{0.0f};
    };
    absl::flat_hash_map<uint64_t, NormalSum> edgeNormals;
    edgeNormals.reserve(blocks.size() * 8 + 32);

    // 面单元四条边：起点平面偏移 (su,sv)、终点平移量 (du,dv)、外侧邻接单元偏移 (ou,ov)
    struct PlaneEdge {
        int su, sv, du, dv, ou, ov;
    };
    static constexpr PlaneEdge kPlaneEdges[4] = {
        {0, 0, 1,  0,  0,  -1}, // 底边：沿 u 正方向，外侧 v-1
        {1, 0, 0,  1,  1,  0 }, // 右边：沿 v 正方向，外侧 u+1
        {1, 1, -1, 0,  0,  1 }, // 顶边：沿 u 负方向，外侧 v+1
        {0, 1, 0,  -1, -1, 0 }, // 左边：沿 v 负方向，外侧 u-1
    };

    for (int d = 0; d < 6; ++d) {
        auto const& f = kFaces[d];
        for (uint64_t cell : faceSets[d]) {
            // packCell 的 u/v/w 分量即 x/y/z 本地坐标
            int const cx     = static_cast<int>(static_cast<uint32_t>(cell >> 42)) - kLocalOffset;
            int const cy     = static_cast<int>(static_cast<uint32_t>((cell >> 21) & 0x1FFFFFu)) - kLocalOffset;
            int const cz     = static_cast<int>(static_cast<uint32_t>(cell & 0x1FFFFFu)) - kLocalOffset;
            auto      comp   = [&](int axis) { return axis == 0 ? cx : axis == 1 ? cy : cz; };
            int const u      = comp(f.uAxis);
            int const v      = comp(f.vAxis);
            int const wFixed = comp(f.wAxis);

            for (auto const& pe : kPlaneEdges) {
                int const nu = u + pe.ou, nv = v + pe.ov;
                if (faceSets[d].contains(cellFromPlane(f, nu, nv, wFixed))) continue; // 区域内部边

                // 发射线段（锚点本地坐标）
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
                uint64_t const segKey =
                    a[axis] <= b[axis] ? packEdgeKey(a[0], a[1], a[2], axis) : packEdgeKey(b[0], b[1], b[2], axis);
                auto& n  = edgeNormals[segKey];
                n.x     += f.nx;
                n.y     += f.ny;
                n.z     += f.nz;
            }
        }
    }

    // 法线归一化 -> edges_
    edges_.reserve(edgeNormals.size());
    for (auto& [key, n] : edgeNormals) {
        float const nl = std::sqrt(n.x * n.x + n.y * n.y + n.z * n.z);
        if (nl < 1e-6f) continue;
        float const inv = 1.0f / nl;

        uint64_t const axis = key >> 62;
        uint64_t const cell = key & 0x3FFFFFFFFFFFFFFFull;
        float const    ax   = static_cast<float>(static_cast<int>(static_cast<uint32_t>(cell >> 42)) - kLocalOffset);
        float const    ay =
            static_cast<float>(static_cast<int>(static_cast<uint32_t>((cell >> 21) & 0x1FFFFFu)) - kLocalOffset);
        float const az = static_cast<float>(static_cast<int>(static_cast<uint32_t>(cell & 0x1FFFFFu)) - kLocalOffset);
        if (axis == 0) {
            edges_.push_back(Edge{ax, ay, az, ax + 1.0f, ay, az, n.x * inv, n.y * inv, n.z * inv});
        } else if (axis == 1) {
            edges_.push_back(Edge{ax, ay, az, ax, ay + 1.0f, az, n.x * inv, n.y * inv, n.z * inv});
        } else {
            edges_.push_back(Edge{ax, ay, az, ax, ay, az + 1.0f, n.x * inv, n.y * inv, n.z * inv});
        }
    }

    static bool sLogged = false;
    if (!sLogged) {
        sLogged = true;
        FM_TRACE("[FastMiner] VoxelOutlineRenderer edges=" << edges_.size() << " for " << blocks.size() << " blocks");
    }
}

/**
 * @brief 构建外壳 mesh（仅几何重建后调用）。
 *
 * 顶点为锚点本地坐标，构建两份：
 * - 灰色 mesh：真透视（深度测试/写入关闭）提交，被地形遮挡处呈现灰色；
 * - 白色 mesh：深度测试开启提交，不可见边被深度缓冲裁剪。
 */
void VoxelOutlineRenderer::buildOutlineMesh(BaseActorRenderContext& ctx) {
    meshGray_.reset();
    meshWhite_.reset();
    grayVerts_  = 0;
    whiteVerts_ = 0;
    meshValid_  = false;
    if (edges_.empty()) return;

    auto& tessellator = ctx.getTessellator();
    for (int pass = 0; pass < 2; ++pass) {
        bool const white = pass == 0;
        tessellator.cancel();
        tessellator.begin(
            Tessellator::DebugContextCallback{},
            mce::PrimitiveMode::LineList,
            static_cast<int>(edges_.size() * 2),
            false
        );
        tessellator.colorABGR(white ? kWhiteAbgr : kHiddenAbgr);
        int verts = 0;
        for (auto const& e : edges_) {
            if (white) {
                // 白线 pass 保持深度测试开启（可见性交给深度缓冲），但线紧贴方块表面
                // 会被 LESS 测试裁掉 -> 沿外侧法线整体外推 kShellOffset，使其位于
                // 表面之前可稳定通过；被真实前景遮挡的线段深度仍更深 -> 保持裁剪。
                float const ox = e.nx * kShellOffset;
                float const oy = e.ny * kShellOffset;
                float const oz = e.nz * kShellOffset;
                tessellator.vertex(e.ax + ox, e.ay + oy, e.az + oz);
                tessellator.vertex(e.bx + ox, e.by + oy, e.bz + oz);
            } else {
                tessellator.vertex(e.ax, e.ay, e.az);
                tessellator.vertex(e.bx, e.by, e.bz);
            }
            verts += 2;
        }
        auto mesh = std::make_unique<mce::Mesh>(tessellator.end(
            Tessellator::UploadMode::Buffered,
            white ? "FastMinerVoxelOutlineWhite" : "FastMinerVoxelOutlineGray",
            Tessellator::SupplementaryFieldAutoGenerationMode::None
        ));
        if (white) {
            meshWhite_  = std::move(mesh);
            whiteVerts_ = verts;
        } else {
            meshGray_  = std::move(mesh);
            grayVerts_ = verts;
        }
    }
    meshValid_ = (meshGray_ && grayVerts_ > 0) || (meshWhite_ && whiteVerts_ > 0);
    FM_TRACE(
        "[FastMiner] VoxelOutlineRenderer mesh built, grayVerts=" << grayVerts_ << ", whiteVerts=" << whiteVerts_
                                                                  << ", valid=" << meshValid_
    );
}

void VoxelOutlineRenderer::draw(BaseActorRenderContext& ctx) {
    if (meshDirty_) {
        meshDirty_ = false;
        buildOutlineMesh(ctx);
    }
    if (!meshValid_) return;

    auto& client        = ctx.getClient();
    auto* levelRenderer = client.getLevelRenderer();
    if (!levelRenderer) return;
    auto const& material = levelRenderer->getLevelRendererPlayer().mOutlineSelectionMaterial.get();
    if (!material) return;

    // 注意：world matrix 在透明 pass 指世界->视图->投影变换；网格顶点为锚点本地坐标，
    // 世界矩阵平移(anchor - camera) 映射到相机相对空间（缺失此步渲染到视野外）。
    Vec3 const& camera = ctx.getCameraPosition();
    auto        matrix = ctx.getWorldMatrix().push(false);
    matrix->translate(
        static_cast<float>(edgeAnchor_.x) - camera.x,
        static_cast<float>(edgeAnchor_.y) - camera.y,
        static_cast<float>(edgeAnchor_.z) - camera.z
    );

    auto* mat = const_cast<mce::RenderMaterial*>(material.operator->());

    // pass 1：灰色 - 真透视（深测/深写关闭）全量外壳，被地形遮挡处显示灰色
    {
        ScopedDepthXRay const xray{mat};
        if (meshGray_ && grayVerts_ > 0) {
            meshGray_->renderMesh(
                ctx.getScreenContext(),
                material,
                0,
                static_cast<uint>(grayVerts_),
                ctx.mOffscreenCaptureDescription.get(),
                nullptr
            );
        }
    }
    // pass 2：白色 - 深度测试保持原状（可见性由深度缓冲决定），仅关深度写入：
    // 不可见（被遮挡）边被深度裁剪 -> 露出的只有灰色；真正可见边被白色覆盖
    {
        ScopedDepthWriteOff const writeOff{mat};
        if (meshWhite_ && whiteVerts_ > 0) {
            meshWhite_->renderMesh(
                ctx.getScreenContext(),
                material,
                0,
                static_cast<uint>(whiteVerts_),
                ctx.mOffscreenCaptureDescription.get(),
                nullptr
            );
        }
    }
}

} // namespace fm::client