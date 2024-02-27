# threadpool
用C++11封装线程池、支持投递定时任务、投递延迟任务、投递优先任务、投递普通任务、投递idle任务

可以填线程数
ThreadPool thrPool = new ThreadPool(4);

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


