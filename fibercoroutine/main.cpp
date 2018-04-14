#include "fibercoroutine.h"
#include <windows.h>
#include <iostream>

#pragma comment(linker, "/subsystem:console /entry:wWinMainCRTStartup")

FiberScheduler fibers;

int WINAPI wWinMain(HINSTANCE, HINSTANCE, LPWSTR, int)
{
	fibers.EnqueueFiber([] {
		for (int i = 0; i < 3; i++)
		{
			std::cout << "fiber1=" << i << std::endl;
			fibers.YieldFiberForClock(-500'000'0);
		}
	});
	fibers.EnqueueFiber([] {
		for (int i = 0; i < 3; i++)
		{
			std::cout << "fiber2=" << i << std::endl;
			fibers.YieldFiberForClock(-500'000'0);
		}
	});
	fibers.EnqueueFiber([] {
		for (int i = 0; i < 3; i++)
		{
			std::cout << "fiber3=" << i << std::endl;
			fibers.YieldFiberForClock(-500'000'0);
		}
	});
	while (!fibers.IsEmpty())
	{
		//std::cout << "scheduling next" << std::endl;
		//fibers.CleanDeadToNext();
		fibers.RunFibers();
		static int i = 0;
		Sleep(50);
		if (++i > 10)
		{
			break;
		}
	}

	WCHAR wc;
	DWORD nret;
	ReadConsole(GetStdHandle(STD_INPUT_HANDLE), &wc, 1, &nret, NULL);
	return 0;
}