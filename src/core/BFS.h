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

    /// 队列元素：坐标 + 去重键
    struct Pending {
        Pos blockPos;
        Key hashedPos;
    };

    explicit BFS(HasherF hasher) : hasher_(std::move(hasher)) {}

    /// 默认构造：构造后必须经 reset/seed/adopt 才能使用；
    /// 若 HasherF 承载维度等参数，请用显式构造并在 reset 前重建一次。
    BFS()                          = default;
    BFS(BFS&&) noexcept            = default;
    BFS& operator=(BFS&&) noexcept = default;
    BFS(BFS const&)                = delete;
    BFS& operator=(BFS const&)     = delete;

    /// 启动一轮搜索：起点入队并标记已访问（起点不计入结果），丢弃上一轮全部状态。
    /// limit 同时作为容量提示（>0 时按 2×limit 预分配 visited），结果达到上限即 done。
    void reset(Pos const& start, Key const& startKey, int limit = 0) {
        clearState(limit);
        queue_.emplace_back(Pending{start, startKey});
        visited_.insert(startKey);
        resultBegin_ = 1; // 起点占位 [0]，result 视图跳过它
    }

    /// 预填已确定集合（普通种子：允许重复输入，会做去重与哈希）。已知结果集请用 adopt（零哈希）。
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

    /// 无损接管已确定集合（结果集交接：不在 BFS 内重新哈希 / 去重 -- 源头已唯一）。
    /// 传入集合应不含起点；交接后以 nextPop 纯消费，不再扩展。
    void adopt(std::vector<Pending>&& settled) {
        clearState();
        queue_       = std::move(settled);
        resultBegin_ = 0; // 无起点
    }

    /// 容量预留提示（无界搜索场景按已知规模预先扩容，减少 rehash/重分配）
    void reserve(size_t queueHint, size_t visitedHint) {
        queue_.reserve(queueHint);
        visited_.reserve(visitedHint);
    }

    /// 推进一个元素：出队 -> onPop 消费 -> 方向扩展（匹配入队并收集）。
    /// 返回 false 表示队列已耗尽或已达 limit，无需再推进。
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

    /// 仅消费一个已入队元素（不扩展）-- adopt/seed 后的纯挖掘阶段
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

    /// 预算级批量推进（等价于连续调用 next，逐个检查完成）；返回 false 表示完成。
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

    /// 预算级纯消费（等价于连续调用 nextPop）
    template <typename World, typename OnPopF>
        requires std::invocable<OnPopF&, World&, Pending const&>
    bool consume(World& world, int budget, OnPopF& onPop) {
        if (budget <= 0) budget = 1;
        for (int i = 0; i < budget; ++i) {
            if (!nextPop(world, onPop)) return false;
        }
        return true;
    }

    /// 已搜到的方块集合（不含起点，顺序即 BFS 扩展顺序）--队列的起点偏移视图，
    [[nodiscard]] std::span<Pending const> result() const noexcept {
        return std::span<Pending const>(queue_).subspan(resultBegin_);
    }

    /// 全量队列（含起点与预填集合）：挖掘阶段遍历已消费/待消费元素用
    [[nodiscard]] std::vector<Pending> const& queue() const noexcept { return queue_; }

    /// 移交全部待通知方块（任务收尾后队列不再使用）
    [[nodiscard]] std::vector<Pending>&& releaseQueue() noexcept { return std::move(queue_); }

    [[nodiscard]] bool done() const noexcept { return done_; }
    [[nodiscard]] bool exhausted() const noexcept { return head_ >= queue_.size(); }

private:
    void clearState(int limit = 0) {
        queue_.clear();
        visited_.clear();
        // 恢复容量以复用上一轮分配（避免连续搜索场景反复 realloc）
        queue_.reserve(limit > 0 ? static_cast<size_t>(limit) : queue_.capacity());
        visited_.reserve(limit > 0 ? static_cast<size_t>(limit) * 2 : visited_.capacity());
        head_        = 0;
        limit_       = limit;
        resultBegin_ = 0;
        done_        = false;
    }

    HasherF                  hasher_;
    std::vector<Pending>     queue_;          // 搜索队列（head 游标顺序消费，末尾一次性释放）；起点之后即结果集
    absl::flat_hash_set<Key> visited_;        // 已访问 / 已扫描标记
    size_t                   head_{0};        // 消费游标
    size_t                   resultBegin_{0}; // 结果视图起点（reset 后 =1 跳过起点；seed/adopt 后 =0）
    int                      limit_{0};       // 结果上限（>0 时 queue.size()-resultBegin_ 达到即 done）
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