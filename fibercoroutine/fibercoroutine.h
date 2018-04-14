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
	// [scheduler�ӿ�]
	void CleanDeadToNext(); // ���������fiber����һ����Ч��
	void RunFibers(); // ������һ��fiber
	bool IsEmpty();
	// [fiber�ӿ�]
	void YieldFiber(); // ����scheduler
	void YieldFiberForClock(long long pos_filetime_neg_100ns); // ��ʱ
	// [scheduler&fiber�ӿ�]
	void EnqueueFiber(std::function<void()> fn); // ����µ�fiber
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
	// ������
	HANDLE scheduler;
	FiberList fiberqueue; // Ҫ���ַ��ʧЧ��������list
	FiberIter currentiter = fiberqueue.end(); // �ӿ�list��β����ʼ
	static void CALLBACK DefFiberProc(void *); // ���ڱ�ϵͳ���õľ�̬����
};

inline void CALLBACK FiberScheduler::PrivateImpl_::DefFiberProc(void *p)
{
	FiberData *pdata = (FiberData *)p;
	// ���к�������
	pdata->startFunction();
	// ����ɣ�֪ͨschedulerɾ����fiber
	pdata->finished = true;
	SwitchToFiber(pdata->scheduler);
	// ��Զ�������ٱ����ȣ�������ǳ�����bug
	assert(("shall never arrive this", false));
}

inline FiberScheduler::~FiberScheduler()
{
	// ��ȫ���ٵ�����
	for (auto &refdata : impl_->fiberqueue)
	{
		if (!refdata.finished)
		{
			// ����δ��ɵ���
			assert(refdata.handle != nullptr);
			// �������Ծ���
			char buf[1024] = "";
			snprintf(buf, 1024, "WARNING: fiber %p terminated by ~FiberScheduler().\n", refdata.handle);
			OutputDebugStringA(buf);
			// ����������fiber
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
			// ��������ɵ������ǰ������Դ��
			assert(refdata.handle == nullptr);
			assert(refdata.waittimer == nullptr);
			// �������Ծ���
			OutputDebugStringA("WARNING: fiber finished but not cleaned.\n");
		}
	}
}

inline FiberScheduler::FiberScheduler()
	: impl_(std::make_unique<PrivateImpl_>())
{
	// ��ʼ��scheduler�����fiber
	impl_->scheduler = ConvertThreadToFiberEx(nullptr, FIBER_FLAG_FLOAT_SWITCH);
}

inline void FiberScheduler::YieldFiber()
{
	// ����scheduler
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
	// ѭ��ִ��ֱ����һ����Ч��
	while (queue.begin() != queue.end())
	{
		if (curriter == queue.end())
			curriter = queue.begin();
		auto thisiter = curriter;
		auto testnext = thisiter;
		// ��ͼ������һ��
		if (++testnext == queue.end())
			testnext = queue.begin();
		// �Ƿ�Ϊ���һ����
		bool last = testnext == thisiter;
		// �������δ�������ֹ����
		if (!thisiter->finished)
			break;
		// ��ͼ������һ����
		assert(thisiter->handle == nullptr);
		assert(thisiter->waittimer == nullptr);
		queue.erase(thisiter);
		if (last)
		{
			// ����������һ��
			curriter = queue.end();
			break;
		}
		else
		{
			// ���µ�ǰ������
			curriter = testnext;
		}
	}
}

inline void FiberScheduler::RunFibers()
{
	assert(GetCurrentFiber() == impl_->scheduler);
	auto &queue = impl_->fiberqueue;
	auto &curriter = impl_->currentiter;
	if (queue.begin() != queue.end()) // ��ִ��һ��
	{
		if (curriter == queue.end())
			curriter = queue.begin();
		auto thisiter = curriter;
		// ��ͼ������һ����
		if (!thisiter->finished)
		{
			// ����δ��ɵ���
			assert(thisiter->handle != nullptr);
			// ��ͼִ��
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
				// ִ����ɣ���������fiber
				assert(thisiter->waittimer == nullptr);
				DeleteFiber(thisiter->handle);
				thisiter->handle = nullptr;
			}
		}
		else
		{
			// ��������ɵ������ǰ����fiber��
			assert(thisiter->handle == nullptr);
			assert(thisiter->waittimer == nullptr);
			// �������Ծ���
			OutputDebugStringA("WARNING: fiber finished but not cleaned.\n");
		}
		// ������һ��
		if (++curriter == queue.end())
			curriter = queue.begin();
	}
}

inline void FiberScheduler::EnqueueFiber(std::function<void()> fn)
{
	// ׷���µ�δ��ɵ���
	PrivateImpl_::FiberData data = { fn, impl_->scheduler, nullptr, false, nullptr };
	// �Ȳ���
	auto thisiter = impl_->fiberqueue.insert(impl_->currentiter, data);
	auto pdata = &*thisiter;
	// �ٴ���fiber���޸�list�ϵ�handle
	pdata->handle = CreateFiberEx(0, 0, FIBER_FLAG_FLOAT_SWITCH,
		PrivateImpl_::DefFiberProc, pdata);
}

inline bool FiberScheduler::IsEmpty()
{
	return impl_->fiberqueue.empty();
}

