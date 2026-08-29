#include "TaskPermitAwaiter.h"
#include "core/TaskDispatcher.h"

namespace fm {


TaskPermitAwaiter::TaskPermitAwaiter(TaskControl* task, TaskDispatcher& dispatcher)
: task(task),
  dispatcher(dispatcher) {}

void TaskPermitAwaiter::await_suspend(std::coroutine_handle<> h) { dispatcher.enqueue(task, h); }


} // namespace fm