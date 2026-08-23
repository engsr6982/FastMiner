#include "render/ChainOutline.h"

#include "preview/ChainPreview.h"

#include "ll/api/memory/Hook.h"

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

// ---- 体积外轮廓线段集（集合变化时重建，render 线程独占）----
struct Edge {
    float ax, ay, az; // 锚点本地坐标
    float bx, by, bz;
    float nx, ny, nz; // 外侧方向（归属暴露面法线的归一化和向量，仅用于白线表面外偏移）
};

std::vector<Edge> gEdges;
uint64_t          gEdgeGen{UINT64_MAX};
BlockPos          gEdgeAnchor{};

// 渲染线程专属的缓存线框 mesh（仅集合变化时重建，相机无关-可见性交给深度缓冲）：
//   gMeshGray  — 全量外壳灰线，真透视（深测关闭）提交 -> 被地形遮挡处显示灰色
//   gMeshWhite — 全量外壳白线，深度测试开启提交 -> 不可见边被深度裁剪，只留真可见白色
std::unique_ptr<mce::Mesh> gMeshGray;
std::unique_ptr<mce::Mesh> gMeshWhite;
int                        gGrayVerts{0};
int                        gWhiteVerts{0};
bool                       gMeshValid{false};

// 颜色（均不透明，不依赖 blend）
constexpr int   kWhiteAbgr   = 0xFFFFFFFF;
constexpr int   kHiddenAbgr  = 0xFF969696; // 灰 (150,150,150)：被遮挡 / 背侧
constexpr float kShellOffset = 0.01f;      // 白线沿外侧法线的表面外偏移（格），使深度测试通过

/// RAII：真透视。绘制前保存并临时关闭线框材质的深度测试/深度写入，绘制后还原，
/// 避免污染原版选择描边材质。
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

/// RAII：仅关闭深度写入（深度测试保持原样）-白线绘制时防止污染场景深度。
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

/// 局部坐标打包（锚点相对坐标 [-2048, 2048) 内可唯一映射到 uint64）
constexpr int kLocalOffset = 2048;

inline uint64_t packCell(int x, int y, int z) {
    return (static_cast<uint64_t>(static_cast<uint32_t>(x + kLocalOffset)) << 42)
         | (static_cast<uint64_t>(static_cast<uint32_t>(y + kLocalOffset)) << 21)
         | static_cast<uint64_t>(static_cast<uint32_t>(z + kLocalOffset));
}
inline uint64_t packEdgeKey(int x, int y, int z, int axis) {
    return packCell(x, y, z) | (static_cast<uint64_t>(axis) << 62);
}

/// 重建体积外壳轮廓线段集。
///
/// 算法（暴露面区域周界，聚合相邻方块共享边）：
/// 1. 对集合内每个方块 p、每个方向 d：若 p+d 不在集合中，则 p 的 d 面为暴露面；
///    同方向暴露面构成「平面区域」（面单元集合）。
/// 2. 对每个平面区域，仅描画其外缘：只有「外侧邻接面单元不在本区域」的边产生
///    线段 -> 每面只画一圈轮廓，区域内部（中间方块）不产生任何线段。
/// 3. 外缘线以规范化晶格线 key 去重（同一几何线段从相邻方向面区域沿相反方向
///    发射时起点取沿轴较小端，保证只有一个 key）。
///
/// 输出 gEdges：锚点本地坐标的单位线段（可见性不在此处判定-由渲染时
/// 深度缓冲语义决定：真透视灰线 + 深度裁剪白线）。
void rebuildEdges(ChainPreview::Snapshot const& snap) {
    gEdgeGen    = snap.generation;
    gEdgeAnchor = snap.anchor;
    gEdges.clear();

    absl::flat_hash_set<uint64_t> inSet;
    inSet.reserve(snap.blocks.size() * 2 + 8);
    // 注意：snapshot.blocks 不含准星锚点方块（BFS 结果不含起点）-必须把锚点
    // 显式算作集合成员，否则锚点会被当成空气 -> 其四周暴露面全部外露，
    // 表现为「准星方块仍有轮廓框」+ 多余的竖直毛刺线段。
    inSet.insert(packCell(0, 0, 0));
    for (auto const& p : snap.blocks) {
        inSet.insert(packCell(p.x - snap.anchor.x, p.y - snap.anchor.y, p.z - snap.anchor.z));
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
    // 面单元收集：snap.blocks + 锚点自身（anchor 已在上方加入 inSet 用于邻居判定，
    // 但这里必须同样遍历它贡献暴露面单元 - 否则锚点的 ±y 面不进入顶/底面区域，
    // 顶/底区域在锚点处留洞 -> 准星方块的顶/底面边缘被误渲染出来）
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
    for (auto const& p : snap.blocks) {
        collectBlockFaces(p.x - snap.anchor.x, p.y - snap.anchor.y, p.z - snap.anchor.z);
    }

    // 外缘晶格线：规范化 key -> 外侧法线累加（棱线属于两个方向的面区域时取矢量和，
    // 归一化后作为"外侧朝向"，供白线做表面外微小偏移以通过深度测试）
    struct NormalSum {
        float x{0.0f}, y{0.0f}, z{0.0f};
    };
    absl::flat_hash_map<uint64_t, NormalSum> edgeNormals;
    edgeNormals.reserve(snap.blocks.size() * 8 + 32);

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

    // 法线归一化 -> gEdges
    gEdges.reserve(edgeNormals.size());
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
            gEdges.push_back(Edge{ax, ay, az, ax + 1.0f, ay, az, n.x * inv, n.y * inv, n.z * inv});
        } else if (axis == 1) {
            gEdges.push_back(Edge{ax, ay, az, ax, ay + 1.0f, az, n.x * inv, n.y * inv, n.z * inv});
        } else {
            gEdges.push_back(Edge{ax, ay, az, ax, ay, az + 1.0f, n.x * inv, n.y * inv, n.z * inv});
        }
    }

    static bool sLogged = false;
    if (!sLogged) {
        sLogged = true;
        FM_TRACE("[FastMiner] ChainOutline edges=" << gEdges.size() << " for " << snap.blocks.size() << " blocks");
    }
}

/// 构建外壳 mesh（仅集合变化时调用，相机无关-可见性完全交给渲染时的深度语义）：
/// mesh 顶点为锚点本地坐标 + 均匀单色，构建两份：
///   - 灰色 mesh：真透视（深测关闭）提交 -> 全量外壳，被地形遮挡处呈现灰色
///   - 白色 mesh：深度测试开启提交 -> 不可见边被深度缓冲裁剪，只留下真正可见的白色
void buildOutlineMesh(BaseActorRenderContext& ctx) {
    gMeshGray.reset();
    gMeshWhite.reset();
    gGrayVerts  = 0;
    gWhiteVerts = 0;
    gMeshValid  = false;
    if (gEdges.empty()) return;

    auto& tessellator = ctx.getTessellator();
    for (int pass = 0; pass < 2; ++pass) {
        bool const white = pass == 0;
        tessellator.cancel();
        tessellator.begin(
            Tessellator::DebugContextCallback{},
            mce::PrimitiveMode::LineList,
            static_cast<int>(gEdges.size() * 2),
            false
        );
        tessellator.colorABGR(white ? kWhiteAbgr : kHiddenAbgr);
        int verts = 0;
        for (auto const& e : gEdges) {
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
            white ? "FastMinerChainOutlineWhite" : "FastMinerChainOutlineGray",
            Tessellator::SupplementaryFieldAutoGenerationMode::None
        ));
        if (white) {
            gMeshWhite  = std::move(mesh);
            gWhiteVerts = verts;
        } else {
            gMeshGray  = std::move(mesh);
            gGrayVerts = verts;
        }
    }
    gMeshValid = (gMeshGray && gGrayVerts > 0) || (gMeshWhite && gWhiteVerts > 0);
    FM_TRACE(
        "[FastMiner] ChainOutline mesh built, grayVerts=" << gGrayVerts << ", whiteVerts=" << gWhiteVerts
                                                          << ", valid=" << gMeshValid
    );
}

} // namespace

// 坑位提示：
// - $renderBlockEntities 在 SDK 中标记 MCFOLD（编译器折叠），Hook 该地址会同时拦截
//   LevelRendererShadowCamera 的折叠实现 - 因此只在透明 pass（renderAlphaLayer==true）
//   绘制，阴影相机 pass 透明层不触发轮廓提交，实测主视图生效。
// - 透明 pass 内构建 mesh（tessellator.begin/end）与提交是经过多次实测的稳定路径，
//   不要“优化”成在不透明 pass 构建 - 曾导致轮廓完全不显示。
// - 图元类型由 tessellator.end 时烘焙进顶点缓冲：提交时修改 material->mPrimitiveMode
//   不会改变已建 mesh 的渲染图元（曾尝试 QuadList 白带，白色直接消失）。
LL_TYPE_INSTANCE_HOOK(
    ChainOutlineRenderHook,
    ll::memory::HookPriority::Normal,
    LevelRendererPlayer,
    &LevelRendererPlayer::$renderBlockEntities,
    void,
    ::BaseActorRenderContext& renderContext,
    bool                      renderAlphaLayer
) {
    origin(renderContext, renderAlphaLayer);
    if (renderAlphaLayer) {
        ::fm::client::ChainOutline::draw(renderContext); // 透明层提交，描边覆盖在所有不透明块之上
    }
}

static bool gHooked = false;

void ChainOutline::install() {
    if (!gHooked) {
        gHooked = ChainOutlineRenderHook::hook() >= 0;
        FM_TRACE("[FastMiner] ChainOutline hook installed: " << (gHooked ? "true" : "false"));
    }
}

void ChainOutline::uninstall() {
    if (gHooked) {
        ChainOutlineRenderHook::unhook();
        gHooked = false;
    }
    gEdges.clear();
    gMeshGray.reset();
    gMeshWhite.reset();
    gMeshValid = false;
}

void ChainOutline::draw(BaseActorRenderContext& ctx) {
    auto* preview = ChainPreview::active();
    if (!preview) return;

    auto snap = preview->getSnapshot();
    if (!snap || snap->blocks.empty()) {
        gEdges.clear();
        gMeshGray.reset();
        gMeshWhite.reset();
        gMeshValid = false;
        return;
    }

    // 轮廓线段集与顶点 mesh 均仅在集合变化时重建（相机无关，可见性交给深度语义）
    if (gEdgeGen != snap->generation) {
        rebuildEdges(*snap);
        buildOutlineMesh(ctx);
    }
    if (!gMeshValid) return;

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
        static_cast<float>(gEdgeAnchor.x) - camera.x,
        static_cast<float>(gEdgeAnchor.y) - camera.y,
        static_cast<float>(gEdgeAnchor.z) - camera.z
    );

    auto* mat = const_cast<mce::RenderMaterial*>(material.operator->());

    // pass 1：灰色 - 真透视（深测/深写关闭）全量外壳，被地形遮挡处显示灰色
    {
        ScopedDepthXRay const xray{mat};
        if (gMeshGray && gGrayVerts > 0) {
            gMeshGray->renderMesh(
                ctx.getScreenContext(),
                material,
                0,
                static_cast<uint>(gGrayVerts),
                ctx.mOffscreenCaptureDescription.get(),
                nullptr
            );
        }
    }
    // pass 2：白色 - 深度测试保持原状（可见性由深度缓冲决定），仅关深度写入：
    // 不可见（被遮挡）边被深度裁剪 -> 露出的只有灰色；真正可见边被白色覆盖
    {
        ScopedDepthWriteOff const writeOff{mat};
        if (gMeshWhite && gWhiteVerts > 0) {
            gMeshWhite->renderMesh(
                ctx.getScreenContext(),
                material,
                0,
                static_cast<uint>(gWhiteVerts),
                ctx.mOffscreenCaptureDescription.get(),
                nullptr
            );
        }
    }
}

} // namespace fm::client