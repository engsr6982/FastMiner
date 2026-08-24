#pragma once
#include "Global.h"
#include "core/BFS.h"
#include "core/MinerTaskContext.h"
#include "core/MinerUtil.h"

#include "ll/api/event/EventBus.h"

#include "mc/deps/core/string/HashedString.h"
#include "mc/world/level/BlockPos.h"
#include <mc/world/level/block/BlockChangeContext.h>

#include <optional>
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
    /// 预搜索交接数据：已确定将连锁的方块集合
    using PreSearchData = std::vector<BlockBFS::Pending>;
    using QueueElement  = BlockBFS::Pending;

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
    BlockBFS         search_;
    MinerDispatcher& dispatcher_; // 任务调度器

    // 计数
    int count_{0};        // 挖掘次数
    int deductDamage_{0}; // 扣除的耐久度
    int quota_{0};        // 任务执行次数配额

    // 本批次已挖掘尚未提交邻居更新的方块，批边界 flushBlockUpdates() 清空
    std::vector<BlockPos> pendingUpdate_{};

    // 预搜索交接（两阶段）。非空时：挖掘前已由客户端预搜索确定集合，直接按顺序挖掘，不再扩散搜索
    std::vector<BlockBFS::Pending> preSearchBlocks_{};
    bool const                     seeded_{false};

    using NotifyFinishedHook = std::function<void(MinerTask const& task, long long cpuTime)>;
    NotifyFinishedHook notifyFinishedHook_{nullptr};

    FM_DISABLE_COPY(MinerTask);
    using Ptr = std::shared_ptr<MinerTask>;

    explicit MinerTask(
        MinerTaskContext             ctx,
        MinerDispatcher&             dispatcher,
        NotifyFinishedHook           finishedHook = nullptr,
        std::optional<PreSearchData> preSearch    = std::nullopt
    );

    void execute();
    void tryBreakBlock(QueueElement const& element);
    void calculateDurabilityDeduction();
    void notifyFinished(long long cpuTime);
    /**
     * @brief 提交本批次(本 tick)挖掘方块的世界更新
     * setBlock 阶段仅广播(flags=2)以利用游戏子区块批量网络同步，此处补上
     * 被跳过的邻居通知(updateNeighborsAt)，保证火把/藤蔓/液体/红石等正确响应。
     */
    void flushBlockUpdates();

    void interrupt();
    bool isInterrupted() const;
    bool isFinished() const;
    bool isRunning() const;
    bool isPending() const;
    bool canContinue() const;
};


} // namespace fm