#pragma once

#include <functional>
#include <type_traits>
#include <list>
#include <memory>

class FiberScheduler
{
public:
	virtual ~FiberScheduler();
	FiberScheduler();
	// [scheduler接口]
	void CleanDeadToNext(); // 清理结束的fiber到下一个有效项
	void RunFibers(); // 运行下一个fiber
	bool IsEmpty();
	// [fiber接口]
	void YieldFiber(); // 返回scheduler
	void YieldFiberForClock(long long pos_filetime_neg_100ns); // 定时
	// [scheduler&fiber接口]
	void EnqueueFiber(std::function<void()> fn); // 添加新的fiber
private:
	struct PrivateImpl_;
	std::unique_ptr<PrivateImpl_> impl_;
};

////////////////////////////////////////

#include <cassert>
#include <windows.h>

struct FiberScheduler::PrivateImpl_
{
	struct FiberData
	{
		std::function<void()> startFunction;
		HANDLE scheduler;
		HANDLE handle;
		bool finished;
		HANDLE waittimer;
	};
	typedef std::list<FiberData> FiberList;
	typedef FiberList::iterator FiberIter;
	// 数据项
	HANDLE scheduler;
	FiberList fiberqueue; // 要求地址不失效，这里用list
	FiberIter currentiter = fiberqueue.end(); // 从空list的尾部开始
	static void CALLBACK DefFiberProc(void *); // 用于被系统调用的静态函数
};

inline void CALLBACK FiberScheduler::PrivateImpl_::DefFiberProc(void *p)
{
	FiberData *pdata = (FiberData *)p;
	// 运行函数对象
	pdata->startFunction();
	// 已完成，通知scheduler删除该fiber
	pdata->finished = true;
	SwitchToFiber(pdata->scheduler);
	// 永远不可能再被调度，否则就是出现了bug
	assert(("shall never arrive this", false));
}

inline FiberScheduler::~FiberScheduler()
{
	// 安全销毁调度器
	for (auto &refdata : impl_->fiberqueue)
	{
		if (!refdata.finished)
		{
			// 存在未完成的项
			assert(refdata.handle != nullptr);
			// 发出调试警告
			char buf[1024] = "";
			snprintf(buf, 1024, "WARNING: fiber %p terminated by ~FiberScheduler().\n", refdata.handle);
			OutputDebugStringA(buf);
			// 非正常销毁fiber
			DeleteFiber(refdata.handle);
			refdata.handle = nullptr;
			refdata.finished = true;
			if (refdata.waittimer)
			{
				CloseHandle(refdata.waittimer);
				refdata.waittimer = nullptr;
			}
		}
		else
		{
			// 存在已完成的项（已提前销毁资源）
			assert(refdata.handle == nullptr);
			assert(refdata.waittimer == nullptr);
			// 发出调试警告
			OutputDebugStringA("WARNING: fiber finished but not cleaned.\n");
		}
	}
}

inline FiberScheduler::FiberScheduler()
	: impl_(std::make_unique<PrivateImpl_>())
{
	// 初始化scheduler本身的fiber
	impl_->scheduler = ConvertThreadToFiberEx(nullptr, FIBER_FLAG_FLOAT_SWITCH);
}

inline void FiberScheduler::YieldFiber()
{
	// 返回scheduler
	assert(GetCurrentFiber() != impl_->scheduler);
	SwitchToFiber(impl_->scheduler);
}

inline void FiberScheduler::YieldFiberForClock(long long pos_filetime_neg_100ns)
{
	assert(GetCurrentFiber() != impl_->scheduler);
	HANDLE timer = CreateWaitableTimer(nullptr, true, nullptr);
	LARGE_INTEGER time_li;
	time_li.QuadPart = pos_filetime_neg_100ns;
	SetWaitableTimer(timer, &time_li, 0, nullptr, nullptr, false);
	PrivateImpl_::FiberData *pdata = (PrivateImpl_::FiberData *)GetFiberData();
	pdata->waittimer = timer;
	SwitchToFiber(impl_->scheduler);
}

inline void FiberScheduler::CleanDeadToNext()
{
	auto &queue = impl_->fiberqueue;
	auto &curriter = impl_->currentiter;
	// 循环执行直到下一个有效项
	while (queue.begin() != queue.end())
	{
		if (curriter == queue.end())
			curriter = queue.begin();
		auto thisiter = curriter;
		auto testnext = thisiter;
		// 试图跳到下一个
		if (++testnext == queue.end())
			testnext = queue.begin();
		// 是否为最后一个？
		bool last = testnext == thisiter;
		// 如果遇到未完成则终止清理
		if (!thisiter->finished)
			break;
		// 试图销毁这一个项
		assert(thisiter->handle == nullptr);
		assert(thisiter->waittimer == nullptr);
		queue.erase(thisiter);
		if (last)
		{
			// 如果遇到最后一个
			curriter = queue.end();
			break;
		}
		else
		{
			// 更新当前迭代器
			curriter = testnext;
		}
	}
}

inline void FiberScheduler::RunFibers()
{
	assert(GetCurrentFiber() == impl_->scheduler);
	auto &queue = impl_->fiberqueue;
	auto &curriter = impl_->currentiter;
	if (queue.begin() != queue.end()) // 仅执行一次
	{
		if (curriter == queue.end())
			curriter = queue.begin();
		auto thisiter = curriter;
		// 试图销毁这一个项
		if (!thisiter->finished)
		{
			// 存在未完成的项
			assert(thisiter->handle != nullptr);
			// 试图执行
			if (!thisiter->waittimer ||
				WaitForSingleObject(thisiter->waittimer, 0) != WAIT_TIMEOUT)
			{
				if (thisiter->waittimer)
				{
					CloseHandle(thisiter->waittimer);
					thisiter->waittimer = nullptr;
				}
				SwitchToFiber(thisiter->handle);
			}
			if (thisiter->finished)
			{
				// 执行完成，立即销毁fiber
				assert(thisiter->waittimer == nullptr);
				DeleteFiber(thisiter->handle);
				thisiter->handle = nullptr;
			}
		}
		else
		{
			// 存在已完成的项（已提前销毁fiber）
			assert(thisiter->handle == nullptr);
			assert(thisiter->waittimer == nullptr);
			// 发出调试警告
			OutputDebugStringA("WARNING: fiber finished but not cleaned.\n");
		}
		// 跳到下一个
		if (++curriter == queue.end())
			curriter = queue.begin();
	}
}

inline void FiberScheduler::EnqueueFiber(std::function<void()> fn)
{
	// 追加新的未完成的项
	PrivateImpl_::FiberData data = { fn, impl_->scheduler, nullptr, false, nullptr };
	// 先插入
	auto thisiter = impl_->fiberqueue.insert(impl_->currentiter, data);
	auto pdata = &*thisiter;
	// 再创建fiber并修改list上的handle
	pdata->handle = CreateFiberEx(0, 0, FIBER_FLAG_FLOAT_SWITCH,
		PrivateImpl_::DefFiberProc, pdata);
}

inline bool FiberScheduler::IsEmpty()
{
	return impl_->fiberqueue.empty();
}

