#pragma once
#include "Global.h"
#include "core/MinerUtil.h"

#include "absl/container/flat_hash_set.h"

#include "mc/world/level/BlockPos.h"

#include <concepts>
#include <cstddef>
#include <cstdint>
#include <span>
#include <utility>
#include <vector>

class BlockSource;

namespace fm {

/**
 * @brief 通用增量 BFS 引擎
 *
 *   World      -- 每次调用由调用方现取现传的环境对象。
 *   OnPopF     -- Pending 出队后的消费回调（挖掘破坏、统计等），可修改 world。
 *   MatchF     -- 匹配谓词 bool(World&, Pos)，false 的邻居会入已访问集合但不入队。
 *   NeighborF  -- 方向扩展，向 emit 回调投递邻居坐标（6 相邻 / 3x3x3 等）。
 *   HasherF    -- 位置 -> 去重键。
 *
 * visited / queue 均按 limit 预分配；单线程调用。
 *
 * 步进粒度：
 *   next()/nextPop()  -- 元素级（每次消费一个出队元素）。
 *   step()/consume()  -- 预算级批量推进（用于无调度约束的渐进搜索）。
 */
template <typename PosT, typename KeyT, typename HasherF>
class BFS {
public:
    using Pos = PosT;
    using Key = KeyT;

    struct Pending {
        Pos blockPos;
        Key hashedPos;
    };

    explicit BFS(HasherF hasher) : hasher_(std::move(hasher)) {}

    /**
     * @brief 默认构造。
     *
     * 构造后必须经 reset/seed/adopt 才能使用；若 HasherF 承载维度等参数，
     * 请用显式构造并在 reset 前重建一次。
     */
    BFS()                          = default;
    BFS(BFS&&) noexcept            = default;
    BFS& operator=(BFS&&) noexcept = default;
    BFS(BFS const&)                = delete;
    BFS& operator=(BFS const&)     = delete;

    /**
     * @brief 丢弃上轮状态并启动新搜索。
     * @note 起点入队但跳过结果视图。
     */
    void reset(Pos const& start, Key const& startKey, int limit = 0) {
        clearState(limit);
        queue_.emplace_back(Pending{start, startKey});
        visited_.insert(startKey);
        resultBegin_ = 1; // 起点占位 [0]，result 视图跳过它
    }

    /**
     * @brief 从任意集合播种，内部去重并哈希。
     * @note 已知唯一结果集请改用 adopt。
     */
    void seed(std::span<Pos const> initial, int limit = 0) {
        clearState(limit);
        queue_.reserve(initial.size() + 1);
        visited_.reserve(initial.size() * 2 + 4);
        for (auto const& p : initial) {
            Key const k = hasher_(p);
            if (visited_.insert(k).second) {
                queue_.emplace_back(Pending{p, k});
            }
        }
        resultBegin_ = 0; // 无起点
    }

    /**
     * @brief 直接接管已哈希去重的集合，后续仅消费不再扩展。
     */
    void adopt(std::vector<Pending>&& settled) {
        clearState();
        queue_       = std::move(settled);
        resultBegin_ = 0; // 无起点
    }

    /**
     * @brief 按已知规模预先扩容，减少 rehash/重分配。
     */
    void reserve(size_t queueHint, size_t visitedHint) {
        queue_.reserve(queueHint);
        visited_.reserve(visitedHint);
    }

    /**
     * @brief 消费一个元素并向匹配且未访问的邻居扩展。
     * @return false 表示队列耗尽或已达上限。
     */
    template <typename World, typename OnPopF, typename MatchF, typename NeighborF>
        requires std::invocable<OnPopF&, World&, Pending const&> && std::invocable<MatchF&, World&, Pos const&>
              && requires(NeighborF& f, Pos const& p) { f(p, [](Pos const&) {}); }
    bool next(World& world, OnPopF& onPop, MatchF& match, NeighborF& neighbors) {
        if (done_) return false;
        if (limit_ > 0 && static_cast<int>(queue_.size() - resultBegin_) >= limit_) {
            done_ = true;
            return false;
        }
        auto const& element = queue_[head_++];
        onPop(world, element);

        // 方向扩展：仅当邻居为"未访问"且匹配时入队（不匹配也标记已访问，防重复扫描）。
        neighbors(element.blockPos, [&](Pos const& adj) {
            Key const k = hasher_(adj);
            if (!visited_.insert(k).second) {
                return;
            }
            if (!match(world, adj)) {
                return;
            }
            queue_.emplace_back(Pending{adj, k});
        });

        if (head_ >= queue_.size()) {
            done_ = true;
        }
        return !done_;
    }

    /**
     * @brief 仅消费一个已入队元素，不扩展（adopt/seed 后的纯消费阶段）。
     */
    template <typename World, typename OnPopF>
        requires std::invocable<OnPopF&, World&, Pending const&>
    bool nextPop(World& world, OnPopF& onPop) {
        if (done_) return false;
        if (limit_ > 0 && static_cast<int>(queue_.size() - resultBegin_) >= limit_) {
            done_ = true;
            return false;
        }
        auto const& element = queue_[head_++];
        onPop(world, element);
        if (head_ >= queue_.size()) {
            done_ = true;
        }
        return !done_;
    }

    /**
     * @brief 按 budget 连续调用 next。
     * @return false 表示完成。
     */
    template <typename World, typename OnPopF, typename MatchF, typename NeighborF>
        requires std::invocable<OnPopF&, World&, Pending const&> && std::invocable<MatchF&, World&, Pos const&>
              && requires(NeighborF& f, Pos const& p) { f(p, [](Pos const&) {}); }
    bool step(World& world, int budget, OnPopF& onPop, MatchF& match, NeighborF& neighbors) {
        if (budget <= 0) budget = 1; // 最小预算，避免无限空转
        for (int i = 0; i < budget; ++i) {
            if (!next(world, onPop, match, neighbors)) return false;
        }
        return true;
    }

    /**
     * @brief 按 budget 连续调用 nextPop。
     */
    template <typename World, typename OnPopF>
        requires std::invocable<OnPopF&, World&, Pending const&>
    bool consume(World& world, int budget, OnPopF& onPop) {
        if (budget <= 0) budget = 1;
        for (int i = 0; i < budget; ++i) {
            if (!nextPop(world, onPop)) return false;
        }
        return true;
    }

    /**
     * @brief 不含起点的结果视图（顺序即 BFS 扩展顺序）。
     */
    [[nodiscard]] std::span<Pending const> result() const noexcept {
        return std::span<Pending const>(queue_).subspan(resultBegin_);
    }

    /**
     * @brief 全量队列（含起点），用于遍历已消费/待消费元素。
     */
    [[nodiscard]] std::vector<Pending> const& queue() const noexcept { return queue_; }

    /**
     * @brief 任务收尾后一次性移出全部待通知方块。
     */
    [[nodiscard]] std::vector<Pending>&& releaseQueue() noexcept { return std::move(queue_); }

    [[nodiscard]] bool done() const noexcept { return done_; }
    [[nodiscard]] bool exhausted() const noexcept { return head_ >= queue_.size(); }

private:
    void clearState(int limit = 0) {
        queue_.clear();
        visited_.clear();
        // 复用上一轮容量，避免连续搜索场景反复 realloc
        queue_.reserve(limit > 0 ? static_cast<size_t>(limit) : queue_.capacity());
        visited_.reserve(limit > 0 ? static_cast<size_t>(limit) * 2 : visited_.capacity());
        head_        = 0;
        limit_       = limit;
        resultBegin_ = 0;
        done_        = false;
    }

    HasherF                  hasher_;
    std::vector<Pending>     queue_;
    absl::flat_hash_set<Key> visited_;
    size_t                   head_{0};
    size_t                   resultBegin_{0}; // reset 后 =1 跳过起点；seed/adopt 后 =0
    int                      limit_{0};
    bool                     done_{false};
};


struct BFSDirection {
    int8_t dx, dy, dz;
};


[[nodiscard]] inline std::vector<BFSDirection> const& adjacentDirections() noexcept {
    static constexpr BFSDirection kAdjacent[6] = {
        {1,  0,  0 },
        {-1, 0,  0 },
        {0,  1,  0 },
        {0,  -1, 0 },
        {0,  0,  1 },
        {0,  0,  -1},
    };
    static std::vector<BFSDirection> const k = {kAdjacent, kAdjacent + 6};
    return k;
}
[[nodiscard]] inline std::vector<BFSDirection> const& cubeDirections() noexcept {
    static constexpr BFSDirection kCube[26] = {
        {1,  0,  0 },
        {-1, 0,  0 },
        {0,  1,  0 },
        {0,  -1, 0 },
        {0,  0,  1 },
        {0,  0,  -1},
        {1,  0,  1 },
        {-1, 0,  1 },
        {0,  1,  1 },
        {0,  -1, 1 },
        {1,  1,  1 },
        {-1, -1, 1 },
        {1,  -1, 1 },
        {-1, 1,  1 },
        {1,  0,  -1},
        {-1, 0,  -1},
        {0,  1,  -1},
        {0,  -1, -1},
        {1,  1,  -1},
        {-1, -1, -1},
        {1,  -1, -1},
        {-1, 1,  -1},
        {1,  1,  0 },
        {-1, -1, 0 },
        {1,  -1, 0 },
        {-1, 1,  0 },
    };
    static std::vector<BFSDirection> const k = {kCube, kCube + 26};
    return k;
}

struct DimPosHasher {
    int          dim{0};
    HashedDimPos operator()(BlockPos const& pos) const noexcept { return miner_util::hashDimensionPosition(pos, dim); }
};

using BlockBFS = BFS<BlockPos, HashedDimPos, DimPosHasher>;

} // namespace fm