// Copyright (c) 2022, The Monero Project
//
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without modification, are
// permitted provided that the following conditions are met:
//
// 1. Redistributions of source code must retain the above copyright notice, this list of
//    conditions and the following disclaimer.
//
// 2. Redistributions in binary form must reproduce the above copyright notice, this list
//    of conditions and the following disclaimer in the documentation and/or other
//    materials provided with the distribution.
//
// 3. Neither the name of the copyright holder nor the names of its contributors may be
//    used to endorse or promote products derived from this software without specific
//    prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY
// EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
// MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL
// THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
// SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
// PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
// INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
// STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF
// THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

//paired header
#include "threadpool.h"

//local headers
#include "task_types.h"

//third party headers
#include <boost/optional/optional.hpp>

//standard headers
#include <cassert>
#include <list>
#include <thread>
#include <vector>

#undef MONERO_DEFAULT_LOG_CATEGORY
#define MONERO_DEFAULT_LOG_CATEGORY "async"

namespace async
{
// start at 1 so each thread's default context id does not match any actual context
static std::atomic<std::uint64_t> s_context_id_counter{1};
static thread_local std::uint64_t tl_context_id{0};  //context this thread is attached to
static thread_local std::uint16_t tl_worker_id{0};   //this thread's id within its context

//-------------------------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------------------------
static std::uint64_t initialize_threadpool_owner()
{
    assert(tl_worker_id == 0);  //only threads with id = 0 may own threadpools

    // the first time this function is called, initialize with a unique threadpool id
    // - a threadpool owner gets its own unique threadpool id to facilitate owning multiple threadpools with
    //   overlapping lifetimes
    static const std::uint64_t id{
            []()
            {
                tl_context_id = s_context_id_counter.fetch_add(1, std::memory_order_relaxed);
                return tl_context_id;
            }()
        };

    return id;
}
//-------------------------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------------------------
static void initialize_threadpool_worker_thread(const std::uint64_t threadpool_id, const std::uint16_t worker_id)
{
    assert(tl_context_id == 0);  //only threads without a context may be subthreads of a threadpool
    assert(worker_id > 0);  //id 0 is reserved for pool owners
    tl_context_id = threadpool_id;
    tl_worker_id  = worker_id;
}
//-------------------------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------------------------
static std::uint16_t thread_context_id()
{
    return tl_context_id;
}
//-------------------------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------------------------
static std::uint16_t threadpool_worker_id()
{
    return tl_worker_id;
}
//-------------------------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------------------------
static bool test_threadpool_member_invariants(const std::uint64_t threadpool_id, const std::uint64_t owner_id)
{
    // if this thread owns the threadpool, its worker id should be 0
    if (owner_id == thread_context_id())
        return threadpool_worker_id() == 0;

    // if this thread doesn't own the threadpool, it should be a subthread of the pool
    return (threadpool_id == thread_context_id()) && (threadpool_worker_id() > 0);
}
//-------------------------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------------------------
static unsigned char clamp_priority(const unsigned char max_priority_level, const unsigned char priority)
{
    if (priority > max_priority_level)
        return max_priority_level;
    return priority;
}
//-------------------------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------------------------
static void set_current_time_if_undefined(std::chrono::time_point<std::chrono::steady_clock> &time_inout)
{
    // 'undefined' means set to zero
    if (time_inout == std::chrono::time_point<std::chrono::steady_clock>::min())
        time_inout = std::chrono::steady_clock::now();
}
//-------------------------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------------------------
static TaskVariant execute_task(task_t &task)
{
    std::future<TaskVariant> result{task.get_future()};
    task();
    try { return result.get(); } catch (...) {}
    return boost::none;
}
//-------------------------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------------------------

//-------------------------------------------------------------------------------------------------------------------
// ThreadPool INTERNAL
//-------------------------------------------------------------------------------------------------------------------
void ThreadPool::perform_sleepy_queue_maintenance()
{
    // cycle through the sleepy queues once, cleaning up each queue as we go
    for (std::uint16_t queue_index{0}; queue_index < m_num_queues; ++queue_index)
    {
        // perform maintenance on this queue
        std::list<std::shared_ptr<SleepingTask>> awakened_tasks{
                m_sleepy_task_queues[queue_index].try_perform_maintenance()
            };

        // force submit the awakened sleepy tasks
        // - note: elements at the bottom of the awakened sleepy tasks are assumed to be higher priority, so we submit
        //   those first
        // - note: we ignore the queue size limit so that none of our awakened tasks get stuck waiting for overflowing
        //   queues to be dealt with
        for (std::shared_ptr<SleepingTask> &task : awakened_tasks)
        {
            if (!task) continue;
            this->submit_simple_task(std::move(task->sleepy_task).simple_task, true);
        }
    }
}
//-------------------------------------------------------------------------------------------------------------------
// ThreadPool INTERNAL
//-------------------------------------------------------------------------------------------------------------------
TaskVariant ThreadPool::submit_simple_task(SimpleTask &&simple_task, const bool ignore_queue_size_limit)
{
    // spin through the simple task queues at our task's priority level
    // - start at the task queue one-after the previous start queue as a naive/simple way to spread tasks out evenly
    const unsigned char clamped_priority{clamp_priority(m_max_priority_level, simple_task.priority)};
    const std::uint16_t start_counter{m_normal_queue_submission_counter.fetch_add(1, std::memory_order_relaxed)};
    boost::optional<std::uint16_t> full_queue_index;

    for (std::uint32_t i{0}; i < m_num_queues * m_num_submit_cycle_attempts; ++i)
    {
        // try to push into the specified queue
        const std::uint32_t queue_index{(i + start_counter) % m_num_queues};
        const TokenQueueResult result{
                m_task_queues[clamped_priority][queue_index].try_push(std::move(simple_task).task)
            };

        // if the queue is full, save its index (we always save the last confirmed-full queue's index)
        if (result == TokenQueueResult::QUEUE_FULL)
            full_queue_index = queue_index;
        // leave if submitting the task succeeded
        else if (result == TokenQueueResult::SUCCESS)
        {
            m_waiter_manager.notify_one();
            return boost::none;
        }
    }

    // if task queues are full, force insert to a known-maxed queue and immediately pull off the bottom task to
    //   execute in-line
    // - note: it's possible to push/pull from a non-maxed queue here (race condition on other workers pulling tasks),
    //   but if we are encountering maxed out queues then it's good to reduce the queue load here so future attempts
    //   to submit a task are less likely to run into full queues (which wastes time)
    if (!ignore_queue_size_limit && full_queue_index)
    {
        task_t next_task{
                m_task_queues[clamped_priority][*full_queue_index].force_push_pop(std::move(simple_task).task)
            };
        return execute_task(next_task);
    }

    // fallback: force insert
    m_task_queues[clamped_priority][start_counter % m_num_queues].force_push(std::move(simple_task).task);
    m_waiter_manager.notify_one();
    return boost::none;
}
//-------------------------------------------------------------------------------------------------------------------
// ThreadPool INTERNAL
//-------------------------------------------------------------------------------------------------------------------
TaskVariant ThreadPool::submit_sleepy_task(SleepyTask &&sleepy_task)
{
    // set the start time of sleepy tasks with undefined start time
    set_current_time_if_undefined(sleepy_task.wake_time.start_time);

    // if the sleepy task is awake, unwrap its internal simple task
    if (sleepy_task_is_awake(sleepy_task))
        return std::move(sleepy_task).simple_task;

    // cycle the sleepy queues 
    const std::uint16_t start_counter{m_sleepy_queue_submission_counter.fetch_add(1, std::memory_order_relaxed)};

    for (std::uint32_t i{0}; i < m_num_queues * m_num_submit_cycle_attempts; ++i)
    {
        // try to push into a queue
        if (!m_sleepy_task_queues[(i + start_counter) % m_num_queues].try_push(std::move(sleepy_task)))
            continue;

        // success
        m_waiter_manager.notify_one();
        return boost::none;
    }

    // fallback: force insert
    m_sleepy_task_queues[start_counter % m_num_queues].force_push(std::move(sleepy_task));
    m_waiter_manager.notify_one();
    return boost::none;
}
//-------------------------------------------------------------------------------------------------------------------
// ThreadPool INTERNAL
//-------------------------------------------------------------------------------------------------------------------
boost::optional<task_t> ThreadPool::try_get_simple_task_to_run(const unsigned char max_task_priority,
    const std::uint16_t worker_index)
{
    // cycle the simple queues once, from highest to lowest priority (starting at the specified max task priority)
    // - note: priority '0' is the highest priority so if the threadpool user adds a priority level, all their highest
    //   priority tasks will remain highest priority until they manually change them
    // - note: we include a 'max task priority' so a worker can choose to only work on low-priority tasks (useful for
    //   purging the queue when you have multiple contending high-priority self-extending task loops)
    task_t new_task;

    for (unsigned char priority{clamp_priority(m_max_priority_level, max_task_priority)};
        priority <= m_max_priority_level;
        ++priority)
    {
        for (std::uint16_t i{0}; i < m_num_queues; ++i)
        {
            if (m_task_queues[priority][(i + worker_index) % m_num_queues].try_pop(new_task) ==
                    TokenQueueResult::SUCCESS)
                return new_task;
        }
    }

    // failure
    return boost::none;
}
//-------------------------------------------------------------------------------------------------------------------
// ThreadPool INTERNAL
//-------------------------------------------------------------------------------------------------------------------
boost::optional<task_t> ThreadPool::try_wait_for_sleepy_task_to_run(const unsigned char max_task_priority,
    const std::uint16_t worker_index,
    const std::function<
            WaiterManager::Result(
                    const std::uint16_t,
                    const std::chrono::time_point<std::chrono::steady_clock>&,
                    const WaiterManager::ShutdownPolicy
                )
        > &custom_wait_until)
{
    // wait until we have an awake task while listening to the task notification system
    SleepingTask* sleeping_task{nullptr};
    boost::optional<task_t> final_task{};

    while (true)
    {
        // try to grab a sleepy task with the lowest waketime possible
        for (std::uint16_t i{0}; i < m_num_queues; ++i)
            m_sleepy_task_queues[(i + worker_index) % m_num_queues].try_swap(max_task_priority, sleeping_task);

        // failure: no sleepy task available
        if (!sleeping_task)
            break;

        // wait while listening
        // - when shutting down, aggressively awaken sleepy tasks (this tends to burn CPU for tasks that really
        //   do need to wait, but improves shutdown responsiveness)
        const WaiterManager::Result wait_result{
                custom_wait_until(worker_index,
                    wake_time(sleeping_task->sleepy_task.wake_time),
                    WaiterManager::ShutdownPolicy::EXIT_EARLY)
            };

        // if we stopped waiting due to a wait condition being satisfied, release our sleepy task
        if (wait_result == WaiterManager::Result::CONDITION_TRIGGERED)
        {
            // release our sleepy task
            unclaim_sleeping_task(*sleeping_task);

            // notify another worker now that our sleepy task is available again
            m_waiter_manager.notify_one();
            break;
        }

        // if our sleepy task is awake then we can extract its internal task
        if (sleepy_task_is_awake(sleeping_task->sleepy_task) || wait_result == WaiterManager::Result::SHUTTING_DOWN)
        {
            // get the task
            final_task = std::move(sleeping_task->sleepy_task).simple_task.task;

            // kill the sleepy task so it can be cleaned up
            kill_sleeping_task(*sleeping_task);

            // if we finished waiting due to something other than a timeout, notify another worker
            // - if we ended waiting due to a notification, then there is another task in the pool that can be worked
            //   on, but we are going to work on our awakened sleepy task so we need another worker to grab that new task
            // - if we ended waiting due to a shutdown, then we don't want workers to be waiting (unless on a conditional
            //   wait), so it is fine to aggressively notify in that case
            if (wait_result != WaiterManager::Result::TIMEOUT)
                m_waiter_manager.notify_one();
            break;
        }

        // try to replace our sleepy task with a simple task
        if ((final_task = try_get_simple_task_to_run(max_task_priority, worker_index)))
        {
            // release our sleepy task
            unclaim_sleeping_task(*sleeping_task);

            // notify another worker now that our sleepy task is available again
            m_waiter_manager.notify_one();
            break;
        }
    }

    return final_task;
}
//-------------------------------------------------------------------------------------------------------------------
// ThreadPool INTERNAL
//-------------------------------------------------------------------------------------------------------------------
boost::optional<task_t> ThreadPool::try_get_task_to_run(const unsigned char max_task_priority,
    const std::uint16_t worker_index,
    const std::function<
            WaiterManager::Result(
                    const std::uint16_t,
                    const std::chrono::time_point<std::chrono::steady_clock>&,
                    const WaiterManager::ShutdownPolicy
                )
        > &custom_wait_until) noexcept
{
    assert(test_threadpool_member_invariants(m_threadpool_id, m_threadpool_owner_id));

    try
    {
        // try to find a simple task
        if (auto task = try_get_simple_task_to_run(max_task_priority, worker_index))
            return task;

        // try to wait on a sleepy task
        if (auto task = try_wait_for_sleepy_task_to_run(max_task_priority, worker_index, custom_wait_until))
            return task;
    } catch (...) {}

    // failure
    return boost::none;
}
//-------------------------------------------------------------------------------------------------------------------
// ThreadPool INTERNAL
//-------------------------------------------------------------------------------------------------------------------
void ThreadPool::run_as_worker_DONT_CALL_ME()
{
    assert(test_threadpool_member_invariants(m_threadpool_id, m_threadpool_owner_id));
    const std::uint16_t worker_id{threadpool_worker_id()};
    assert(worker_id > 0);  //only call run_as_worker_DONT_CALL_ME() from subthreads of the threadpool

    // prepare custom wait-until function
    std::function<
            WaiterManager::Result(
                    const std::uint16_t,
                    const std::chrono::time_point<std::chrono::steady_clock>&,
                    const WaiterManager::ShutdownPolicy
                )
        > custom_wait_until{
            [this]
            (
                const std::uint16_t worker_id,
                const std::chrono::time_point<std::chrono::steady_clock> &timepoint,
                const WaiterManager::ShutdownPolicy shutdown_policy
            ) mutable -> WaiterManager::Result
            {
                return m_waiter_manager.wait_until(worker_id, timepoint, shutdown_policy);
            }
        };

    while (true)
    {
        // try to get the next task, then run it and immediately submit its continuation
        // - note: we don't immediately run task continuations because we want to always be pulling tasks from
        //   the bottom of the task pile
        if (auto task = this->try_get_task_to_run(0, worker_id, custom_wait_until))
        {
            this->submit(execute_task(*task));
            continue;
        }

        // we failed to get a task, so wait until some other worker submits a task and notifies us
        // - we only test the shutdown condition immediately after failing to get a task because we want the pool to
        //   continue draining tasks until it is completely empty (users should directly/manually cancel in-flight tasks
        //   if that is needed)
        //   - due to race conditions in the waiter manager, it is possible for workers to shut down even with tasks in
        //     the queues; typically, the worker that submits a task will be able to pick up that task and finish it, but
        //     as a fall-back the thread that destroys the threadpool will purge the pool of all tasks
        // - we periodically wake up to check the queues in case of race conditions around task submission (submitted
        //   tasks will always be executed eventually, but may be excessively delayed if we don't wake up here)
        if (m_waiter_manager.is_shutting_down())
            break;
        m_waiter_manager.wait_for(worker_id, m_max_wait_duration, WaiterManager::ShutdownPolicy::EXIT_EARLY);
    }
}
//-------------------------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------------------------

//-------------------------------------------------------------------------------------------------------------------
ThreadPool::ThreadPool(const unsigned char max_priority_level,
    const std::uint16_t num_managed_workers,
    const std::uint32_t max_queue_size,
    const unsigned char num_submit_cycle_attempts,
    const std::chrono::nanoseconds max_wait_duration) :
        m_threadpool_id{s_context_id_counter.fetch_add(1, std::memory_order_relaxed)},
        m_threadpool_owner_id{initialize_threadpool_owner()},
        m_max_priority_level{max_priority_level},
        m_num_queues{static_cast<uint16_t>(num_managed_workers + 1)},  //+1 to include the threadpool owner
        m_max_queue_size{max_queue_size},
        m_num_submit_cycle_attempts{num_submit_cycle_attempts},
        m_max_wait_duration{max_wait_duration},
        m_waiter_manager{m_num_queues}
{
    // create task queues
    m_task_queues = std::vector<std::vector<TokenQueue<task_t>>>{static_cast<std::size_t>(m_max_priority_level + 1)};

    for (auto &priority_queues : m_task_queues)
    {
        priority_queues = std::vector<TokenQueue<task_t>>{static_cast<std::size_t>(m_num_queues)};

        for (TokenQueue<task_t> &queue : priority_queues)
            queue.set_max_queue_size(m_max_queue_size);
    }

    // create sleepy task queues
    m_sleepy_task_queues = std::vector<SleepyTaskQueue>{m_num_queues};

    // launch workers
    // - note: we reserve worker index 0 for the threadpool owner
    m_workers.reserve(m_num_queues - 1);
    for (std::uint16_t worker_index{1}; worker_index < m_num_queues; ++worker_index)
    {
        try
        {
            m_workers.emplace_back(
                    [this, worker_index]() mutable
                    {
                        initialize_threadpool_worker_thread(this->threadpool_id(), worker_index);
                        try { this->run_as_worker_DONT_CALL_ME(); } catch (...) { /* can't do anything */ }
                    }
                );
        }
        catch (...) { /* can't do anything */ }
    }
}
//-------------------------------------------------------------------------------------------------------------------
ThreadPool::~ThreadPool()
{
    (void)test_threadpool_member_invariants;  //suppress unused warning...
    assert(test_threadpool_member_invariants(m_threadpool_id, m_threadpool_owner_id));
    assert(thread_context_id() == m_threadpool_owner_id);  //only the owner may destroy the object

    // shut down the pool
    try { this->shut_down(); } catch (...) {}

    // join all workers
    for (std::thread &worker : m_workers)
        try { worker.join(); } catch (...) {}

    // clear out any tasks lingering in the pool
    try { this->run_as_worker_DONT_CALL_ME(); } catch (...) {}

    //todo: if there was an exception above then the threadpool may hang or lead to UB, so maybe it would be best to
    //      just abort when an exception is detected
}
//-------------------------------------------------------------------------------------------------------------------
bool ThreadPool::submit(TaskVariant task) noexcept
{
    assert(test_threadpool_member_invariants(m_threadpool_id, m_threadpool_owner_id));

    try
    {
        // submit tasks until no more are returned
        // - we use a submission loop for handling the continuations of tasks that get executed within the submission code
        //   instead of calling submit() directly on those continuations to avoid blowing out the worker's call-stack on
        //   long continuation chains (only a very long series of task splits should cause a blow-out)
        do
        {
            // case: empty task
            if (!task) ; //do nothing
            // case: simple task
            else if (SimpleTask *simpletask = task.try_unwrap<SimpleTask>())
                task = this->submit_simple_task(std::move(*simpletask), false);
            // case: sleepy task
            else if (SleepyTask *sleepytask = task.try_unwrap<SleepyTask>())
                task = this->submit_sleepy_task(std::move(*sleepytask));
            // case: notification task
            // - we break here since a notification is simply a marker on the end of a task chain, and we don't want to
            //   perform sleepy queue maintenance excessively
            else if (task.try_unwrap<ScopedNotification>())
                break;  //the notification is sent at function exit when the task gets destroyed

            // maintain the sleepy queues
            this->perform_sleepy_queue_maintenance();
        } while (task);
    } catch (...) { return false; }

    return true;
}
//-------------------------------------------------------------------------------------------------------------------
std::shared_ptr<ScopedNotification> ThreadPool::get_join_token(std::shared_ptr<std::atomic<bool>> join_signal)
{
    assert(test_threadpool_member_invariants(m_threadpool_id, m_threadpool_owner_id));

    return std::make_shared<ScopedNotification>(
            [
                l_waiter_index = threadpool_worker_id(),
                this,
                l_join_signal = std::move(join_signal)
            ]() mutable
            {
                m_waiter_manager.notify_conditional_waiter(l_waiter_index,
                        [ll_join_signal = std::move(l_join_signal)]() mutable
                        {
                            if (ll_join_signal) ll_join_signal->store(true, std::memory_order_relaxed);
                        }
                    );
            }
        );
}
//-------------------------------------------------------------------------------------------------------------------
std::function<bool()> ThreadPool::get_join_condition(std::shared_ptr<std::atomic<bool>> &&join_signal)
{
    return
        [l_join_signal = std::move(join_signal)]() -> bool
        {
            return !l_join_signal || l_join_signal->load(std::memory_order_relaxed);
        };
}
//-------------------------------------------------------------------------------------------------------------------
void ThreadPool::work_while_waiting(const std::chrono::time_point<std::chrono::steady_clock> &deadline,
    const unsigned char max_task_priority)
{
    assert(test_threadpool_member_invariants(m_threadpool_id, m_threadpool_owner_id));
    const std::uint16_t worker_id{threadpool_worker_id()};

    // prepare custom wait-until function
    std::function<
            WaiterManager::Result(
                    const std::uint16_t,
                    const std::chrono::time_point<std::chrono::steady_clock>&,
                    const WaiterManager::ShutdownPolicy
                )
        > custom_wait_until{
            [this, &deadline]
            (
                const std::uint16_t worker_id,
                const std::chrono::time_point<std::chrono::steady_clock> &timepoint,
                const WaiterManager::ShutdownPolicy shutdown_policy
            ) mutable -> WaiterManager::Result
            {
                const WaiterManager::Result wait_result{
                        m_waiter_manager.wait_until(worker_id,
                            timepoint < deadline ? timepoint : deadline,  //don't wait longer than the deadline
                            shutdown_policy)
                    };

                // treat the deadline as a condition
                if (std::chrono::steady_clock::now() >= deadline)
                    return WaiterManager::Result::CONDITION_TRIGGERED;
                return wait_result;
            }
        };

    // work until the deadline
    while (std::chrono::steady_clock::now() < deadline)
    {
        // try to get the next task, then run it and immediately submit its continuation
        if (auto task = this->try_get_task_to_run(max_task_priority, worker_id, custom_wait_until))
        {
            this->submit(execute_task(*task));
            continue;
        }

        // we failed to get a task, so wait until the deadline
        const WaiterManager::Result wait_result{
                custom_wait_until(worker_id, deadline, WaiterManager::ShutdownPolicy::WAIT)
            };

        // exit immediately if the deadline condition was triggered (don't re-test it)
        if (wait_result == WaiterManager::Result::CONDITION_TRIGGERED)
            break;
    }
}
//-------------------------------------------------------------------------------------------------------------------
void ThreadPool::work_while_waiting(const std::chrono::nanoseconds &duration, const unsigned char max_task_priority)
{
    this->work_while_waiting(std::chrono::steady_clock::now() + duration, max_task_priority);
}
//-------------------------------------------------------------------------------------------------------------------
void ThreadPool::work_while_waiting(const std::function<bool()> &wait_condition_func,
    const unsigned char max_task_priority)
{
    //todo: use shared_ptr<atomic<bool>> for the signaling channel so it can be copied into a std::function
    assert(test_threadpool_member_invariants(m_threadpool_id, m_threadpool_owner_id));
    const std::uint16_t worker_id{threadpool_worker_id()};

    // prepare custom wait-until function
    std::function<
            WaiterManager::Result(
                    const std::uint16_t,
                    const std::chrono::time_point<std::chrono::steady_clock>&,
                    const WaiterManager::ShutdownPolicy
                )
        > custom_wait_until{
            [this, &wait_condition_func]
            (
                const std::uint16_t worker_id,
                const std::chrono::time_point<std::chrono::steady_clock> &timepoint,
                const WaiterManager::ShutdownPolicy shutdown_policy
            ) mutable -> WaiterManager::Result
            {
                return m_waiter_manager.conditional_wait_until(worker_id,
                    wait_condition_func,
                    timepoint,
                    shutdown_policy);
            }
        };

    // work until the wait condition is satisfied
    while (!wait_condition_func())
    {
        // try to get the next task, then run it and immediately submit its continuation
        if (auto task = this->try_get_task_to_run(max_task_priority, worker_id, custom_wait_until))
        {
            this->submit(execute_task(*task));
            continue;
        }

        // we failed to get a task, so wait until the condition is satisfied
        const WaiterManager::Result wait_result{
                custom_wait_until(worker_id,
                    std::chrono::steady_clock::now() + m_max_wait_duration,
                    WaiterManager::ShutdownPolicy::WAIT)
            };

        // exit immediately if the condition was triggered (don't re-test it)
        if (wait_result == WaiterManager::Result::CONDITION_TRIGGERED)
            break;
    }
}
//-------------------------------------------------------------------------------------------------------------------
void ThreadPool::shut_down() noexcept
{
    // shut down the waiter manager, which should notify any waiting workers
    m_waiter_manager.shut_down();
}
//-------------------------------------------------------------------------------------------------------------------
} //namespace async
