#include "MinerDispatcher.h"
#include "config/StaticGlobalConfigHost.h"
#include "core/MinerTask.h"

#include "mc/world/actor/player/Player.h"

namespace fm {

MinerDispatcher::MinerDispatcher() { processingBlocks.reserve(128); }
MinerDispatcher::~MinerDispatcher() { shutdown(); }

bool MinerDispatcher::canLaunchTask(Player& player) const { return !tasks_.contains(player.getUuid()); }

void MinerDispatcher::launch(MinerTask::Ptr task) {
    if (!canLaunchTask(task->player_)) {
        throw std::runtime_error("Player already has a task running");
    }
    tasks_.emplace(task->player_.getUuid(), task);
    task->execute();
}

void MinerDispatcher::enqueue(MinerTask* task, std::coroutine_handle<> h) { pending_.push_back({task, h}); }

void MinerDispatcher::interruptPlayerTask(Player& player) {
    auto iter = tasks_.find(player.getUuid());
    if (iter != tasks_.end()) {
        iter->second->interrupt();
        tasks_.erase(iter);
    }
}

void MinerDispatcher::onTaskFinished(MinerTask* task) { tasks_.erase(task->player_.getUuid()); }

void MinerDispatcher::shutdown() {
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

void MinerDispatcher::tick() {
    static constexpr int Burst = 64; // 单次最大突发量(Burst)

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
    // 清理已恢复的协程
    pending_.erase(pending_.begin(), pending_.begin() + resumed);
}

} // namespace fm