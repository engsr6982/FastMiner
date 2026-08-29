#pragma once

namespace fm {

enum class DestroyMode : int {
    Default = 0, // 6 邻面
    Cube    = 1  // 3x3x3 立方（26 方向）
};

/**
 * @brief 调度器全局配额。
 *
 * 限制每 tick 所有活跃任务合计处理的方块数，以及每 tick 最多从挂起队列恢复多少个协程，
 * 防止单 tick 内大量协程同时恢复卡死服务端线程。
 */
struct DispatcherConfig {
    int globalBlockLimitPerTick{256};
    int maxResumeTasksPerTick{16};
};

} // namespace fm