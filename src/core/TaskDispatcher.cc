#include "TaskDispatcher.h"
#include "config/StaticGlobalConfigHost.h"

#include "mc/world/actor/player/Player.h"

namespace fm {

TaskDispatcher::TaskDispatcher() { processingBlocks.reserve(128); }
TaskDispatcher::~TaskDispatcher() { shutdown(); }

bool TaskDispatcher::canLaunchTask(Player& player) const { return !tasks_.contains(player.getUuid()); }

void TaskDispatcher::enqueue(TaskControl* task, std::coroutine_handle<> h) { pending_.push_back({task, h}); }

void TaskDispatcher::interruptPlayerTask(Player& player) {
    auto iter = tasks_.find(player.getUuid());
    if (iter != tasks_.end()) {
        iter->second->interrupt();
        tasks_.erase(iter);
    }
}

void TaskDispatcher::onTaskFinished(TaskControl* task) { tasks_.erase(task->player_.getUuid()); }

void TaskDispatcher::shutdown() {
    for (auto& [_, task] : tasks_) {
        task->interrupt();
    }
    for (auto& [t, h] : pending_) {
        t->interrupt();
        while (!h.done()) {
            h.resume();
        }
        h.destroy();
    }
    tasks_.clear();
    pending_.clear();
}

void TaskDispatcher::tick() {
    // 限制单次授予配额，防止单个任务吞掉整池额度导致其它任务饥饿
    static constexpr int Burst = 64;

    auto const& cfg = StaticGlobalConfigHost::getDispatcherConfig();

    int remainingQuota = cfg.globalBlockLimitPerTick;
    int maxResume      = cfg.maxResumeTasksPerTick;

    int resumed = 0;
    for (size_t i = 0; i < pending_.size(); ++i) {
        if (resumed >= maxResume || remainingQuota <= 0) {
            break;
        }

        auto& p = pending_[i];
        if (p.task->canContinue()) {
            int grant = std::min(remainingQuota, Burst);

            p.task->quota_ += grant;
            remainingQuota -= grant;

            p.h.resume();
            resumed++;
        }
    }
    pending_.erase(pending_.begin(), pending_.begin() + resumed);
}

} // namespace fm