#pragma once

#include "Global.h"

#include <type_traits>

class Player;

namespace fm {

/**
 * @brief 任务通用控制基类
 *
 * 供 TaskDispatcher 以 `shared_ptr<TaskControl>` 统一持有不同派生任务
 * shared_ptr 类型擦除 deleter 保证释放。
 */
struct TaskControl {
    enum class State {
        Pending,     // 等待执行
        Running,     // 执行中
        Finished,    // 执行完毕
        Interrupted, // 被外部中断
        Aborted,     // 外部熔断
    };

    Player& player_;
    State   state_{State::Pending};
    int     quota_{0};
    int     count_{0};

    explicit TaskControl(Player& player) : player_(player) {}

    inline void interrupt() { state_ = State::Interrupted; }
    inline void abort() { state_ = State::Aborted; }
    inline bool isInterrupted() const { return state_ == State::Interrupted; }
    inline bool isAborted() const { return state_ == State::Aborted; }
    inline bool isFinished() const { return state_ == State::Finished; }
    inline bool isRunning() const { return state_ == State::Running; }
    inline bool isPending() const { return state_ == State::Pending; }
    inline bool canContinue() const { return state_ == State::Running || state_ == State::Pending; }
};

static_assert(
    !std::is_polymorphic_v<TaskControl>,
    "TaskControl wants a vtable-free layout for dispatcher LTO inlining"
);

} // namespace fm