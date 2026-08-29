#pragma once
#include "core/TaskControl.h"
#include "ll/api/base/Concepts.h"

#include <coroutine>

namespace fm {


class TaskDispatcher;

class TaskPermitAwaiter {
    TaskControl*    task;
    TaskDispatcher& dispatcher;

public:
    explicit TaskPermitAwaiter(TaskControl* task, TaskDispatcher& dispatcher);

    // 永远不就绪, 由全局的 TaskDispatcher 调度分配许可
    constexpr bool await_ready() noexcept { return false; }

    void await_suspend(std::coroutine_handle<> h);

    void await_resume() noexcept {}
};

static_assert(ll::concepts::Awaitable<TaskPermitAwaiter>);


} // namespace fm