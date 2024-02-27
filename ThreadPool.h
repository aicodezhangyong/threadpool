#include <iostream>
#include <queue>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <future>
#include <functional>
#include <stdexcept>
#include <atomic>
#include <vector>
#include <chrono>
#include <ctime>
#include <time.h>

using TaskPtr = std::shared_ptr<std::function<void()>>;

struct CompareTimePoint {
  bool operator()(
    const std::pair<std::chrono::time_point<std::chrono::system_clock, std::chrono::system_clock::duration>, std::function<void()>>& lhs,
    const std::pair<std::chrono::time_point<std::chrono::system_clock, std::chrono::system_clock::duration>, std::function<void()>>& rhs)
  {
    return lhs.first > rhs.first;
  }
};

struct TaskTimer {
    int id;
    std::chrono::milliseconds interval;
    TaskPtr callback;
    std::chrono::system_clock::time_point lastRunTime;
    template<class F, class... Args>
    TaskTimer(int id, std::chrono::milliseconds interval, F&& f, Args&&... args)
        :id(id), interval(interval), lastRunTime(std::chrono::system_clock::now()) {
        callback = std::make_shared<std::function<void()>>(
            std::bind(std::forward<F>(f), std::forward<Args>(args)...)
        );
    }
};

class ThreadPool {
public:
    ThreadPool(size_t threads);
    ~ThreadPool();

    template<class F, class... Args>
    auto enqueue(F&& f, Args&&... args)
        -> std::future<typename std::result_of<F(Args...)>::type>
    {
        using return_type = typename std::result_of<F(Args...)>::type;

        auto task = std::make_shared< std::packaged_task<return_type()> >(
            std::bind(std::forward<F>(f), std::forward<Args>(args)...)
        );
        
        std::future<return_type> res = task->get_future();
        {
            std::unique_lock<std::mutex> lock(queue_mutex);
            if(stop)
                throw std::runtime_error("enqueue on stopped ThreadPool");

            tasks.emplace([task](){ (*task)(); });
        }
        condition.notify_one();
        return res;
    }

    template<class F, class... Args>
    auto enqueue_priority(F&& f, Args&&... args)
        -> std::future<typename std::result_of<F(Args...)>::type>
    {
        using return_type = typename std::result_of<F(Args...)>::type;

        auto task = std::make_shared< std::packaged_task<return_type()> >(
            std::bind(std::forward<F>(f), std::forward<Args>(args)...)
        );
        
        std::future<return_type> res = task->get_future();
        {
            std::unique_lock<std::mutex> lock(queue_mutex);
            if(stop)
                throw std::runtime_error("enqueue on stopped ThreadPool");

            priority_tasks.emplace([task](){ (*task)(); });
        }
        condition.notify_one();
        return res;
    }

    template<class F, class... Args>
    auto enqueue_idle(F&& f, Args&&... args)
        -> std::future<typename std::result_of<F(Args...)>::type>
    {
        using return_type = typename std::result_of<F(Args...)>::type;

        auto task = std::make_shared< std::packaged_task<return_type()> >(
            std::bind(std::forward<F>(f), std::forward<Args>(args)...)
        );
        
        std::future<return_type> res = task->get_future();
        {
            std::unique_lock<std::mutex> lock(queue_mutex);
            if(stop)
                throw std::runtime_error("enqueue on stopped ThreadPool");
            idle_tasks.emplace([task](){ (*task)(); });
        }
        condition.notify_one();
        return res;
    }

    template<class F, class... Args>
    void enqueue_delayed(int delay_milliseconds, F&& f, Args&&... args)
    {
        using return_type = typename std::result_of<F(Args...)>::type;
        auto task = std::make_shared< std::packaged_task<return_type()> >(
            std::bind(std::forward<F>(f), std::forward<Args>(args)...)
        );
        {
            std::unique_lock<std::mutex> lock(delayed_queue_mutex);
            if(stop)
                throw std::runtime_error("enqueue on stopped ThreadPool");
            delayed_tasks.push(std::make_pair(std::chrono::system_clock::now() + std::chrono::milliseconds(delay_milliseconds), [task](){ (*task)(); }));
            delayed_first = delayed_tasks.top().first;
        }
        delayed_condition.notify_one();
    }

    template<class F, class... Args>
    int enqueue_timer(int interval, F&& f, Args&&... args)
    {
        int timer_id = -1;
        using return_type = typename std::result_of<F(Args...)>::type;
        {
            std::unique_lock<std::mutex> lock(delayed_queue_mutex);
            if(stop)
                throw std::runtime_error("enqueue on stopped ThreadPool");
            timer_id = get_session_id();
            timer_tasks.emplace_back(timer_id, std::chrono::milliseconds(interval), std::forward<F>(f), std::forward<Args>(args)...);
        }
        delayed_condition.notify_one();
        return timer_id;
    }

    void stop_enqueue_timer(int id)
    {
        std::unique_lock<std::mutex> lock(delayed_queue_mutex);
        timer_tasks.erase(std::remove_if(timer_tasks.begin(), timer_tasks.end(),
            [id](auto& task) {
                return task.id == id;
            }), timer_tasks.end());
    }

    void stop_all_enqueue_timer()
    {
        std::unique_lock<std::mutex> lock(delayed_queue_mutex);
        timer_tasks.clear();
    }

    void print()
    {
        auto now = std::chrono::system_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::seconds>(now - start_run_time).count();
        std::time_t currentTime_t = std::chrono::system_clock::to_time_t(now);
        std::cout <<"current time:"<< std::ctime(&currentTime_t);
        std::cout <<"run time:"<< duration <<std::endl;;
        std::cout <<"_priority task size:"<<_priority_tasks.size()<<std::endl;
        std::cout<<"priority task size:"<<priority_tasks.size()<<std::endl;
        std::cout<<"task size:"<<tasks.size()<<std::endl;
        std::cout<<"idle task size:"<<idle_tasks.size()<<std::endl;
        std::cout<<"timer task size:"<<timer_tasks.size()<<std::endl;
        std::cout<<"idle_running_count:"<<idle_running_count<<std::endl;
        std::cout<<"task_running_count:"<<task_running_count<<std::endl;
        std::cout<<"task_complete_count:"<<task_complete_count<<std::endl;
        std::cout <<"task_throughput:" << task_complete_count/ duration <<"/s"<< std::endl;
    }

private:
    auto _enqueue_priority(TaskPtr task)
    {
        {
            std::unique_lock<std::mutex> lock(queue_mutex);
            if (stop)
                throw std::runtime_error("enqueue on stopped ThreadPool");
            _priority_tasks.emplace(task);
        }
        condition.notify_one();
    }

    bool check_idle_running();
    int  get_session_id();

private:
    std::vector< std::thread > workers;
    std::queue< std::function<void()> > tasks;
    std::queue< std::function<void()> > priority_tasks;
    std::queue< TaskPtr > _priority_tasks;
    //std::queue< std::function<void()> > _priority_tasks;
    std::queue< std::function<void()> > idle_tasks;
    std::mutex queue_mutex;
    std::condition_variable condition;

    std::chrono::time_point<std::chrono::system_clock, std::chrono::system_clock::duration> start_run_time;

    std::chrono::time_point<std::chrono::system_clock, std::chrono::system_clock::duration> delayed_first;
    std::priority_queue<std::pair< std::chrono::time_point<std::chrono::system_clock, std::chrono::system_clock::duration>, std::function<void()>>,
    std::vector<std::pair< std::chrono::time_point<std::chrono::system_clock, std::chrono::system_clock::duration>, std::function<void()>>>, CompareTimePoint> delayed_tasks;

    std::vector<TaskTimer> timer_tasks;

    std::mutex delayed_queue_mutex;
    std::condition_variable delayed_condition;

    std::atomic_size_t idle_running_count;
    std::atomic_size_t task_running_count;
    std::atomic_long task_complete_count;
    bool stop;
    size_t  thread_count;
};


