#include "render/VoxelOutlineRenderer.h"

#include "Global.h"

#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"

#include "mc/client/game/IClientInstance.h"
#include "mc/client/gui/screens/ScreenContext.h"
#include "mc/client/renderer/BaseActorRenderContext.h"
#include "mc/client/renderer/RenderMaterialGroup.h"
#include "mc/client/renderer/Tessellator.h"
#include "mc/client/renderer/TextureGroup.h"
#include "mc/client/renderer/game/LevelRenderer.h"
#include "mc/client/renderer/game/LevelRendererPlayer.h"
#include "mc/deps/core/file/PathView.h"
#include "mc/deps/core/math/Vec2.h"
#include "mc/deps/core/math/Vec3.h"
#include "mc/deps/core/renderer/RenderMaterialInfo.h"
#include "mc/deps/core/resource/ResourceLocation.h"
#include "mc/deps/core_graphics/TextureSetLayerType.h"
#include "mc/deps/minecraft_renderer/renderer/BedrockTextureData.h"
#include "mc/deps/minecraft_renderer/renderer/IsMissingTexture.h"
#include "mc/deps/core_graphics/enums/DepthWriteMask.h"
#include "mc/deps/core_graphics/enums/PrimitiveMode.h"
#include "mc/deps/minecraft_renderer/renderer/MaterialPtr.h"
#include "mc/deps/minecraft_renderer/renderer/Mesh.h"
#include "mc/deps/minecraft_renderer/renderer/RenderMaterial.h"
#include "mc/deps/minecraft_renderer/renderer/TexturePtr.h"
#include "mc/deps/minecraft_renderer/resources/ClientTexture.h"
#include "mc/deps/minecraft_renderer/resources/OffscreenCaptureDescription.h"
#include "mc/deps/minecraft_renderer/resources/ServerTexture.h"

#include "Helper.h"

#include <cmath>
#include <cstdint>
#include <memory>
#include <variant>
#include <vector>

namespace fm::client {
namespace {

// 颜色（均不透明，不依赖 blend）。经 Tessellator 顶点色随 mesh 一起烘焙。
constexpr uint  kWhiteRgb    = 0xFFFFFFu;
constexpr uint  kHiddenRgb   = 0x969696u; // 灰 (150,150,150)：被遮挡 / 背侧
constexpr float kShellOffset = 0.01f;     // 白线沿外侧法线的表面外偏移（格），使深度测试通过
// 白线 pass 的深度偏移：消除与所在方块表面的 z-fighting（详见 ScopedDepthWriteOff）
constexpr float kWhitePassDepthBias = 100.0f;
constexpr float kWhitePassSlopeBias = 15.0f;

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
    float                savedDepthBias{0.0f};
    float                savedSlopeBias{0.0f};

    explicit ScopedDepthWriteOff(mce::RenderMaterial* mat) : material(mat) {
        if (!material) return;
        auto& desc                      = material->depthStencilStateDescription.get();
        saved                           = desc.depthWriteMask;
        savedDepthBias                  = material->mDepthBias;
        savedSlopeBias                  = material->mSlopeScaledDepthBias;
        desc.depthWriteMask             = static_cast<mce::DepthWriteMask>(0);
        // 白线与所在方块的表面严格共面，只靠 0.01 的几何外推在稍远处即会 z-fighting：
        // 白线时而过不了深度测试，露出底下灰线 -> 视觉上频繁闪烁。
        // 原版描边材质自带合适的深度偏移所以不闪，glow_sign_text 没有，故此处显式钉住
        // （偏移量参考 LHolo v26.40 适配版对共面 overlay 的取值）。
        material->mDepthBias            = kWhitePassDepthBias;
        material->mSlopeScaledDepthBias = kWhitePassSlopeBias;
    }
    ~ScopedDepthWriteOff() {
        if (!material) return;
        material->depthStencilStateDescription.get().depthWriteMask = saved;
        material->mDepthBias                                        = savedDepthBias;
        material->mSlopeScaledDepthBias                             = savedSlopeBias;
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

using TextureVariant = std::variant<std::monostate, mce::TexturePtr, mce::ClientTexture, mce::ServerTexture>;

/**
 * @brief 取原版 2x2 纯白贴图，作为 mesh 的纹理绑定。
 *
 * 原版描边材质会采样绑定纹理并做 alpha test：若绑定空 variant，采样即缺失纹理
 * （洋红）—— 这正是轮廓整体发紫红的原因。该白贴图被采样在中心处恰为 (1,1,1,1)，
 * 纹理乘法保持中性且 alpha test 永不丢弃，因此不会干扰顶点色
 * （采样点见 buildOutlineMesh 的 tex2）。
 * 贴图组要等资源加载后才可用，故解析成功即缓存，未就绪时逐帧重试。
 */
TextureVariant resolveWhiteTextureVariant(LevelRenderer* levelRenderer) {
    static mce::TexturePtr cached{};
    static bool            resolved = false;
    if (!resolved && levelRenderer) {
        auto const& textureGroup = levelRenderer->mTextureGroup.get();
        if (textureGroup) {
            auto      texture       = textureGroup->getTexture(
                ResourceLocation{Core::PathView{"textures/ui/white_background"}},
                false,
                std::nullopt,
                cg::TextureSetLayerType::Color
            );
            auto const& clientTexture = texture.mClientTexture;
            if (clientTexture && clientTexture->mIsMissingTexture != IsMissingTexture::Yes) {
                cached   = std::move(texture);
                resolved = true;
            }
        }
    }
    return resolved ? TextureVariant{cached} : TextureVariant{};
}

/**
 * @brief 解析 glow_sign_text 材质（着色器直接输出顶点色 COLOR0）。
 *
 * vanilla 的 mOutlineSelectionMaterial 颜色由 uniform 驱动、会忽略顶点色
 * （IDA: LevelRendererPlayer::_renderOutlineSelection 用 ShaderColor::setColor 着色，
 * tessellateWireBox 不写顶点色），因此用它无法画出白/灰两色。
 * glow_sign_text 直接输出 COLOR0，正是把烘焙进 mesh 的顶点色原样画出来所需。
 * 解析失败返回 nullptr，调用方回退到原版描边材质。
 *
 * 客户端库不导出 mce::MaterialPtr 的构造函数，故句柄放在零初始化的对齐存储里，
 * 只赋值其内部 shared_ptr，避免调用构造函数。
 */
mce::MaterialPtr const* resolveGlowSignMaterial() {
    alignas(mce::MaterialPtr) static std::byte storage[sizeof(mce::MaterialPtr)]{};
    static auto* const                        cached   = reinterpret_cast<mce::MaterialPtr*>(storage);
    static bool                               resolved = false;
    if (!resolved) {
        resolved   = true;
        bool found = false;
        auto scan  = [&](mce::RenderMaterialGroup& group) {
            if (found) return;
            for (auto const& entry : group.mMaterials.get()) {
                auto const& info = entry.second;
                if (!info || !info->mPtr) continue;
                if (entry.first.getString() != "glow_sign_text") continue;
                cached->mRenderMaterialInfoPtr = info;
                found                          = true;
                break;
            }
        };
        scan(mce::RenderMaterialGroup::common());
        if (!found) {
            scan(mce::RenderMaterialGroup::switchable());
        }
    }
    return cached->mRenderMaterialInfoPtr ? cached : nullptr;
}

} // namespace

VoxelOutlineRenderer::VoxelOutlineRenderer()  = default;
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

    auto& tessellator = helper::getTessellator(ctx);
    for (int pass = 0; pass < 2; ++pass) {
        bool const white = pass == 0;

        // tessellator.cancel() 自 v26.40 起不再导出。
        // IDA: cancel() 的全部实现就是单个 bool 置 0（即 mTessellating = false），无其它副作用。
        tessellator.mTessellating = false;

        tessellator.begin(
            Tessellator::DebugContextCallback{},
            mce::PrimitiveMode::LineList,
            static_cast<int>(edges_.size() * 2),
            false
        );
        // tessellator.colorABGR(...) 自 v26.40 起不再导出，但等价的 color(mce::Color) 仍然导出。
        // 必须走官方 color()：它内部会以正确的 VertexField 枚举值调用 MeshData::enableField，
        // 而 MeshData::enableField 本身在 26.40 已不再导出（仅剩 mFieldEnabled 成员），
        // 且 mFieldEnabled 由 array<bool,14> 变为 array<bool,15> —— 枚举已变，
        // 手写裸下标 1 无法保证指向 Color，会导致颜色字段未启用（表现为颜色异常）。
        // IDA: color(mce::Color) 与 colorABGR 写入同一编码 (a<<24)|(b<<16)|(g<<8)|r。
        tessellator.color(white ? mce::Color(kWhiteRgb) : mce::Color(kHiddenRgb));

        // 每个顶点都要给 UV：材质会按 UV 采样绑定纹理并做 alpha test，
        // 缺 UV 时采样无效 -> 输出缺失纹理色（洋红）。统一采纯白贴图中心，
        // 使纹理乘法中性、alpha test 永不丢弃，顶点色得以原样呈现。
        auto const emitVertex = [&](float x, float y, float z) {
            tessellator.tex2(Vec2{0.5f, 0.5f});
            tessellator.vertex(x, y, z);
        };

        int verts = 0;
        for (auto const& e : edges_) {
            if (white) {
                // 白线 pass 保持深度测试开启（可见性交给深度缓冲），但线紧贴方块表面
                // 会被 LESS 测试裁掉 -> 沿外侧法线整体外推 kShellOffset，使其位于
                // 表面之前可稳定通过；被真实前景遮挡的线段深度仍更深 -> 保持裁剪。
                float const ox = e.nx * kShellOffset;
                float const oy = e.ny * kShellOffset;
                float const oz = e.nz * kShellOffset;
                emitVertex(e.ax + ox, e.ay + oy, e.az + oz);
                emitVertex(e.bx + ox, e.by + oy, e.bz + oz);
            } else {
                emitVertex(e.ax, e.ay, e.az);
                emitVertex(e.bx, e.by, e.bz);
            }
            verts += 2;
        }

        // SupplementaryFieldAutoGenerationMode 加载器未导出声明，根据旧版本推测 None 依旧为 0
        // TODO: 等待加载器补全声明 https://github.com/LiteLDev/mcapi-requests/issues/249
        // v26.20
        // enum class Tessellator::SupplementaryFieldAutoGenerationMode : int {
        //     None               = 0,
        //     NormalsAndTangents = 1,
        // };
        //
        // v26.40
        // enum class SupplementaryFieldAutoGenerationMode : ushort {};
        //
        auto mesh = std::make_unique<mce::Mesh>(tessellator.end(
            Tessellator::UploadMode::Buffered,
            white ? "FastMinerVoxelOutlineWhite" : "FastMinerVoxelOutlineGray",
            // Tessellator::SupplementaryFieldAutoGenerationMode::None
            static_cast<SupplementaryFieldAutoGenerationMode>(0)
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

    auto& client        = ctx.mClientInstance;
    auto* levelRenderer = client.getLevelRenderer();
    if (!levelRenderer) return;

    auto& levelRendererPlayer = levelRenderer->mLevelRendererPlayer;
    if (!levelRendererPlayer) return;

    // 优先用 glow_sign_text（着色器直接输出顶点色），回退到原版描边材质。
    // 原版描边材质颜色由 uniform 驱动、忽略顶点色，用它画不出白/灰两色；
    // 且两者都必须绑定纹理（见 resolveWhiteTextureVariant），否则采样缺失纹理发洋红。
    auto const* glowMaterial = resolveGlowSignMaterial();
    auto const& material     = glowMaterial ? *glowMaterial
                                            : levelRendererPlayer->mOutlineSelectionMaterial.get();
    if (!material.mRenderMaterialInfoPtr) return;

    // 白色贴图尚未解析出来时直接跳过本帧：空 variant 会让材质采样到缺失纹理，
    // 表现为首帧闪一下洋红再变正常。宁可少画一帧，也不要闪错色。
    auto const texture = resolveWhiteTextureVariant(levelRenderer);
    if (std::holds_alternative<std::monostate>(texture)) return;

    // BaseActorRenderContext::getCameraPosition 自 v26.40 起不再由 SDK 导出。
    // IDA: 原实现只是构造期缓存 LevelRendererCamera::getCameraPos() 的返回值，
    // 而 getCameraPos() 就是 `return &mCameraPos`。LevelRendererPlayer 继承链为
    // LevelRendererPlayer -> LevelRendererCameraListeners -> LevelRendererCamera，
    // 故直接取同一成员：与 getWorldMatrix() 的矩阵来源(mScreenContext.camera)
    // 指向同一相机，避免新旧两次取值不同步。
    Vec3 const& camera = levelRendererPlayer->mCameraPos.get();

    // 注意：world matrix 在透明 pass 指世界->视图->投影变换；网格顶点为锚点本地坐标，
    // 世界矩阵平移(anchor - camera) 映射到相机相对空间（缺失此步渲染到视野外）。
    // MatrixStackRef::operator-> 自 v26.40 起被移除，改为直接经公开的 mat 成员取矩阵。
    auto matrix = helper::getWorldMatrix(ctx).push(false);
    matrix.mat->translate(
        static_cast<float>(edgeAnchor_.x) - camera.x,
        static_cast<float>(edgeAnchor_.y) - camera.y,
        static_cast<float>(edgeAnchor_.z) - camera.z
    );

    // mce::MaterialPtr::operator-> 自 v26.40 起不再由 SDK 导出。
    // IDA: 其实现为「返回所持有 RenderMaterialInfo 内那个 unique_ptr 的裸指针」
    //       (if (info) return *(void**)((char*)info + offsetof(RenderMaterialInfo, mPtr));)，
    // 即等价于 info->mPtr.get()。RenderMaterialInfo 在 26.40 中仍完整导出
    // (mc/deps/core/renderer/RenderMaterialInfo.h)，故按成员名取值，不依赖偏移。
    auto* mat = material.mRenderMaterialInfoPtr->mPtr.get();

    // mOffscreenCaptureDescription 自 v26.40 起移入不透明的 BaseActorRenderContext::Impl，
    // SDK 不再暴露。该成员仅在离屏捕获(全景图/缩略图)时被置为非 monostate，正常世界渲染
    // 恒为 monostate，故此处传默认构造值（= 不做离屏捕获）。
    OffscreenCaptureDescription const noCapture{};

    // pass 1：灰色 - 真透视（深测/深写关闭）全量外壳，被地形遮挡处显示灰色
    {
        ScopedDepthXRay const xray{mat};
        if (meshGray_ && grayVerts_ > 0) {
            meshGray_->renderMesh(
                ctx.mScreenContext,
                material,
                texture,
                0,
                static_cast<uint>(grayVerts_),
                noCapture,
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
                ctx.mScreenContext,
                material,
                texture,
                0,
                static_cast<uint>(whiteVerts_),
                noCapture,
                nullptr
            );
        }
    }
}

} // namespace fm::client