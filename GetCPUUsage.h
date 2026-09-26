#pragma once
#include <Windows.h>
#include <map>
#include "ProcessStruct.h"
#include <vector>
#include <mutex>

class GetCPUUsage
{
private:
	std::mutex mtx;
	std::map<DWORD, ULONGLONG> lastProcTime;
	std::map<DWORD, ULONGLONG> lastSysTimeMap;
	ULONGLONG lastIdle = 0;
	ULONGLONG lastKernel = 0;
	ULONGLONG lastUser = 0;
	std::mutex cpu_mtx;

public:
	GetCPUUsage() {};
	ULONGLONG getProcessTime(DWORD pid);
	ULONGLONG getSystemTime();
	double getProcessCPU(DWORD pid, ULONGLONG currentSys);
	double getTotalCPU();
	void cleanup(const std::vector<ProcessStruct>& processes);
};