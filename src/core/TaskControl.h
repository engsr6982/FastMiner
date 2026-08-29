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
        Pending,
        Running,
        Finished,
        Interrupted,
    };

    Player& player_;
    State   state_{State::Pending};
    int     quota_{0};
    int     count_{0};

    explicit TaskControl(Player& player) : player_(player) {}

    inline void interrupt() { state_ = State::Interrupted; }
    inline bool isInterrupted() const { return state_ == State::Interrupted; }
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