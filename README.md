# threadpool
用C++11封装线程池、支持投递定时任务、投递延迟任务、投递优先任务、投递普通任务、投递idle任务

可以填线程数
ThreadPool thrPool(4);

投递普通任务
session_ptr pSession;
auto result = std::shared_ptr<Buffer> buffer = std::make_shared<Buffer>();
thrPool.enqueue([](session_ptr pSession, shared_ptr<Buffer> buffer) {
	//实现自己任务代码
	if()
	{
		return 1;
	}
	else
	{
		return 0;
	}
},pSession, buffer);
通过result.get()来获取返回值，但是调用result.get()任务没有结束会阻塞等待.
也可以用非阻塞方法来获取
std::future_status status;
do {
	status = result.wait_for(std::chrono::milliseconds(1));
	if (status == std::future_status::ready) {
		int return_val = result.get();
		std::cout << "return_val Result: " << return_val << std::endl;
	} else if (status == std::future_status::timeout) {
		std::cout << "Operation still not ready..." << std::endl;
	} else if (status == std::future_status::deferred) {
		std::cout << "Operation deferred..." << std::endl;
	}
} while (status != std::future_status::ready);


投递优先任务
auto result = thrPool.enqueue_priority([](session_ptr pSession, shared_ptr<Buffer> buffer) 
... ...


投递idle任务
auto result = thrPool.enqueue_idle([](session_ptr pSession, shared_ptr<Buffer> buffer) 
... ...

投递延迟任务
1000毫秒后触发该任务
thrPool.enqueue_delayed(1000,[](session_ptr pSession, shared_ptr<Buffer> buffer) 
... ...


投递定时任务
每2000毫秒触发一次任务，返回一个定时器唯一ID
int timer_id = thrPool.enqueue_timer(2000,[](session_ptr pSession, shared_ptr<Buffer> buffer) 
... ...

关闭定时任务
thrPool.stop_enqueue_timer(timer_id);
关闭所有定时任务
thrPool.stop_all_enqueue_timer();


