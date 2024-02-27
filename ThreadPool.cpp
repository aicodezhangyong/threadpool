#include "ThreadPool.h"

ThreadPool::ThreadPool(size_t threads) : thread_count(threads), stop(false), idle_running_count(0), task_running_count(0), task_complete_count(0), delayed_tasks()
{
    start_run_time = std::chrono::system_clock::now();
    for(size_t i = 0; i<threads; ++i)
    {
        workers.emplace_back(
            [this]
            {
                bool bTaskIde = false;
                for(;;)
                {
                    std::function<void()> task = NULL;
                    {
                        bTaskIde = false;
                        std::unique_lock<std::mutex> lock(this->queue_mutex);
                        this->condition.wait(lock,
                            [this]{ return this->stop || !this->tasks.empty() || !this->_priority_tasks.empty() || !this->priority_tasks.empty() || (!this->idle_tasks.empty() && !check_idle_running()); });
                        if(this->stop)
                            return;
                        if(!this->_priority_tasks.empty()){
                            task = *this->_priority_tasks.front();
                            this->_priority_tasks.pop();
                        } else if(!this->priority_tasks.empty()){
                            task = std::move(this->priority_tasks.front());
                            this->priority_tasks.pop();
                        } else if(!this->tasks.empty()){
                            task = std::move(this->tasks.front());
                            this->tasks.pop();
                        } else {
                            if(!check_idle_running()){
                                task = std::move(this->idle_tasks.front());
                                this->idle_tasks.pop();
                                bTaskIde = true;
                            }
                        }
                    }
                    if(task){
                        if (bTaskIde) {
                            idle_running_count++;
                        }
                        task_running_count++;
                        task();
                        task_complete_count++;
                        task_running_count--;
                        if(bTaskIde){
                            idle_running_count--;
                        }
                    }
                }
            }
        );
    }

    workers.emplace_back(
        [this]
        {
            long long nmillisecond = 1;
            for(;;)
            {
                std::unique_lock<std::mutex> lock(this->delayed_queue_mutex);
                this->delayed_condition.wait_until(lock, std::chrono::steady_clock::now() + std::chrono::milliseconds(nmillisecond),
                    [this]{ return this->stop || (!this->delayed_tasks.empty() && delayed_first <= std::chrono::system_clock::now()); });
                if(this->stop)
                    return;
                std::chrono::milliseconds minInterval(10000);
                if(!this->delayed_tasks.empty()){
                    auto nextTask = delayed_tasks.top();
                    if (nextTask.first <= std::chrono::system_clock::now()) {
                        TaskPtr task = std::make_shared<std::function<void()>>(std::bind(this->delayed_tasks.top().second));
                        this->_enqueue_priority(task);
                        this->delayed_tasks.pop();
                    }
                    if (!this->delayed_tasks.empty()) {
                        delayed_first = delayed_tasks.top().first;
                        std::chrono::milliseconds ms = std::chrono::duration_cast<std::chrono::milliseconds>(delayed_first - std::chrono::system_clock::now());
                        if (ms.count() > 0) {
                            minInterval = (minInterval > ms) ? ms : minInterval;
                        }
                    }
                }
                if(!this->timer_tasks.empty()){
                    auto now = std::chrono::system_clock::now();
                    for (auto& task : timer_tasks) {
                        std::chrono::milliseconds ms = std::chrono::duration_cast<std::chrono::milliseconds>(now - task.lastRunTime);
                        if (ms >= task.interval) {
                            task.lastRunTime = now;
                            this->_enqueue_priority(task.callback);
                        } else {
                            minInterval = (minInterval > ms) ? ms : minInterval;
                        }
                    }
                }
                nmillisecond = minInterval.count();
            }
        }
    );
}

ThreadPool::~ThreadPool()
{
    {
        std::unique_lock<std::mutex> lock(queue_mutex);
        stop = true;
    }
    condition.notify_all();
    delayed_condition.notify_all();
    for(std::thread &worker: workers)
        worker.join();
}

bool ThreadPool::check_idle_running()
{
    bool result = false;
    if((task_running_count-idle_running_count) > 0)
    {
        result = true;
    }
    else if(thread_count>1)
    {
         result = (idle_running_count >= (thread_count/2));
    }
    return result;
}

int ThreadPool::get_session_id()
{
    static std::atomic<long> auto_session_id;
    if(auto_session_id>=0x7FFFFFFF)
        auto_session_id = 1;
    return auto_session_id++;
}
