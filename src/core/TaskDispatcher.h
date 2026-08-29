#pragma once
#include "Global.h"
#include "core/MinerUtil.h"
#include "core/TaskControl.h"

#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "mc/platform/UUID.h"

#include <coroutine>
#include <cstddef>
#include <memory>
#include <vector>


namespace fm {


/**
 * @brief 任务调度器
 * 调度模型:
 * 1. 任务创建后，挂起并交给调度器，由调度器分配任务许可额度
 * 2. 每 tick 一次，进行计算额度，并恢复挂起的任务执行
 * 3. 任务执行完毕后，将任务从调度器中移除
 *
 * @note 额度分配模型:
 *  int quota_per_task = max(1, floor(globalBlockLimitPerTick / active_tasks));
 *
 * @note 最大恢复任务受 maxResumeTasksPerTick 限制(避免瞬间卡死线程)
 *
 * @note 同一调度器实例可同时承载 ChainTask / UseTask 等不同类型任务，
 *       它们共享同一个全局每 tick 配额池。任务以非虚 TaskControl 统一持有。
 */
class TaskDispatcher final {
public:
    FM_DISABLE_COPY_MOVE(TaskDispatcher);
    explicit TaskDispatcher();
    ~TaskDispatcher();

    inline bool isProcessing(HashedDimPos pos) const { return processingBlocks.contains(pos); }
    inline void insertProcessing(HashedDimPos pos) { processingBlocks.insert(pos); }
    inline void eraseProcessing(HashedDimPos pos) { processingBlocks.erase(pos); }

    bool canLaunchTask(Player& player) const;

    template <typename T>
        requires std::derived_from<T, TaskControl>
    void launch(std::shared_ptr<T> task) {
        if (!canLaunchTask(task->player_)) [[unlikely]] {
            throw std::runtime_error("Player already has a task running");
        }
        tasks_.emplace(task->player_.getUuid(), task);
        task->execute();
    }

    void enqueue(TaskControl* task, std::coroutine_handle<> h);

    void interruptPlayerTask(Player& player);

    void onTaskFinished(TaskControl* task);

    void tick();

    /**
     * @brief 关停清理：中断所有在途任务，恢复挂起协程由状态机自检退出后清空队列
     */
    void shutdown();

private:
    absl::flat_hash_set<HashedDimPos>                            processingBlocks;
    absl::flat_hash_map<mce::UUID, std::shared_ptr<TaskControl>> tasks_;
    struct Pending {
        TaskControl*            task;
        std::coroutine_handle<> h;
    };
    std::vector<Pending> pending_;
};


} // namespace fm