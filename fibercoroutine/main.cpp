#include "fibercoroutine.h"
#include <windows.h>
#include <iostream>
#include <string>

#pragma comment(linker, "/subsystem:console /entry:wWinMainCRTStartup")

int WINAPI wWinMain(HINSTANCE, HINSTANCE, LPWSTR, int)
{
	{
		FiberScheduler fibers;
		FiberScheduler fibers2;
		fibers.EnqueueFiber([&] {
			for (int i = 0; i < 3; i++)
			{
				std::cout << "fiber1=" << i << std::endl;
				fibers.YieldFiberForClock(-500'000'0);
			}
		});
		fibers.EnqueueFiber([&] {
			for (int i = 0; i < 3; i++)
			{
				std::cout << "fiber2=" << i << std::endl;
				fibers.YieldFiberForClock(-500'000'0);
			}
		});
		fibers.EnqueueFiber([&] {
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
			if (++i > 20)
			{
				break;
			}
		}
	}
	std::string str;
	getline(std::cin, str);
	return 0;
}