#pragma once
#include <thread>
#include <atomic>
#include <mutex>
#include <queue>
#include <vector>
#include <functional>
#include <chrono>
using namespace std::chrono_literals;

class ThreadPool {
public:
	using Task = std::function<void()>;//制定Task为无返回值函数类型 
	ThreadPool() {
		threads_.reserve(1024);
		thread_flags_.reserve(1024);
		//申请1024个线程及其信息的空间 
		AddThreads(max_threads_);//放入最大线程数 
	}

	void Schedule(Task t) {
		std::lock_guard<std::mutex> lk(dispatch_mutex_);
		tasks_.push(t);//在要执行的方法vector中放入一个方法 
	}

	void Terminate() {
		global_terminate_.store(true);//设置原子变量为true 
		for (auto& t : threads_) {
			t.join();//等待线程结束并回收资源 
		}
	}
	
	// sizeof(ThreadInfo) <= alignof(ThreadInfo)
	struct ThreadInfo {
		int should_end : 8;
		int is_busy : 8;
	};

	struct Status {
		const std::vector<ThreadInfo> threads;
		int num_threads;
		int num_pending_tasks;
	};

	Status GetStatus() {
		return {
			thread_flags_,
			static_cast<int>(threads_.size()),
			static_cast<int>(tasks_.size())
		};
	}

	void Timeout(std::function<void()>, std::chrono::seconds secs) {

	}

private:
	//该方法用于促使第i个线程运转 
	void RunThread(int i) {
		while(!global_terminate_.load() && !thread_flags_[i].should_end) {

			Task task;
			{
				std::lock_guard<std::mutex> lk(dispatch_mutex_);//开启智能锁 
				if (!tasks_.empty()) {//方法池里面不是空的就取一个方法出来 
					task = tasks_.front();//获得堆积的方法中靠前的那一个 
					tasks_.pop();//弹出一个方法 
				}
			}
			//如果方法已经为空，则休息一阵 
			if (!task) {
				std::this_thread::sleep_for(1s);
			}//否则执行该方法，并在该方法执行后视为执行完成 
			else {
				thread_flags_[i].is_busy = true;

				task();

				thread_flags_[i].is_busy = false;
			}
		}
	}

	void AddThreads(const int n) {
		const auto begin = static_cast<uint32_t>(threads_.size());
		for (auto i = begin; i < begin + n; ++i) {
			thread_flags_.emplace_back(ThreadInfo{false, false});
			threads_.push_back(std::thread([this, i] {
				this->RunThread(i);
			}));
		}
	}
	struct TimedTask {
		using ClockBase = std::chrono::steady_clock;
		std::chrono::time_point<ClockBase> end;
		Task t;
		bool operator< (const TimedTask& r) {
			return end > r.end;
		}
		void Init(Task t, std::chrono::seconds span) {
			end = ClockBase::now() + span;
			t = t;
		}
		bool IsTimeout() {
			if (ClockBase::now() >= end) {
				return true;
			}
			return false;
		}
		
	};

	int max_threads_ = std::thread::hardware_concurrency();
	int max_buffers_ = 1024;

	std::mutex dispatch_mutex_;//设置互斥锁 

	std::atomic<int> global_terminate_ = false;//封装一个原子int类型的数字 
	std::vector<ThreadInfo> thread_flags_;//封装一系列的线程信息变量 
	std::queue<Task> tasks_;//封装一数列的方法 
	std::priority_queue<TimedTask> timed_task_;

	std::vector<std::thread> threads_;//封装线程 
	//升序数列
	std::priority_queue<int, std::vector<int>, std::greater<int>> free_threads_; // min heap
};

