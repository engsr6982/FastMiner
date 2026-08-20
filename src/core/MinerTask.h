#pragma once
#include "Global.h"
#include "core/MinerTaskContext.h"
#include "core/MinerUtil.h"

#include "absl/container/flat_hash_set.h"

#include "ll/api/event/EventBus.h"

#include "mc/deps/core/string/HashedString.h"
#include "mc/world/level/BlockPos.h"
#include <mc/world/level/block/BlockChangeContext.h>

#include <vector>

class Player;
class ItemStack;
class Block;
class BlockPos;
class BlockSource;

namespace fm {

class MinerDispatcher;

using TaskID = uint64_t;

/**
 * @brief 挖掘任务
 * 每个任务的承载单元
 */
struct MinerTask {
    struct Direction {
        int8_t dx, dy, dz;
    };
    struct QueueElement {
        BlockPos     blockPos;
        HashedDimPos hashedPos;
    };

    enum class State {
        Pending,     // 待处理
        Running,     // 正在处理
        Finished,    // 处理完成
        Interrupted, // 被中断
    };

    State                       state_{State::Pending}; // 任务状态
    Player&                     player_;                // 执行任务的玩家
    ItemStack&                  tool_;                  // 使用的工具
    unsigned short const        blockId_;               // 方块 Id
    BlockPos const              startPos_;              // 任务起始位置
    HashedDimPos const          hashedStartPos_;        // 任务起始位置的哈希值
    RuntimeSingleBlockConfigPtr blockConfig_;           // 方块配置
    BlockSource&                blockSource_;           // 方块源
    int const                   limit_{0};              // 挖掘次数限制
    int const                   dimension_;             // 任务所在的维度
    int const                   durability_{0};         // 工具耐久度

    BlockChangeContext   blockChangeCtx_; // 方块改变上下文
    ll::event::EventBus& eventBus_;       // 事件总线

    // BFS
    std::vector<QueueElement>         queue_{};    // 搜索队列
    absl::flat_hash_set<HashedDimPos> visited_{};  // 已访问过的方块索引
    std::vector<Direction> const&     directions_; // 方向
    MinerDispatcher&                  dispatcher_; // 任务调度器

    // 计数
    int count_{0};        // 挖掘次数
    int deductDamage_{0}; // 扣除的耐久度
    int quota_{0};        // 任务执行次数配额

    using NotifyFinishedHook = std::function<void(MinerTask const& task, long long cpuTime)>;
    NotifyFinishedHook notifyFinishedHook_{nullptr};

    FM_DISABLE_COPY(MinerTask);
    using Ptr = std::shared_ptr<MinerTask>;

    explicit MinerTask(MinerTaskContext ctx, MinerDispatcher& dispatcher, NotifyFinishedHook finishedHook = nullptr);

    void execute();
    void tryBreakBlock(QueueElement const& element);
    void calculateDurabilityDeduction();
    void searchAdjacentBlocks(QueueElement const& element);
    void notifyFinished(long long cpuTime);
    void notifyClientBlockUpdate(); // 通知客户端方块更新

    void interrupt();
    bool isInterrupted() const;
    bool isFinished() const;
    bool isRunning() const;
    bool isPending() const;
    bool canContinue() const;
};


} // namespace fm