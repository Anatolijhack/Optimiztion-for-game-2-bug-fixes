#include "GetCPUUsage.h"
#include <set>

ULONGLONG GetCPUUsage::getProcessTime(DWORD pid)
{
    HANDLE hProcess = OpenProcess(PROCESS_QUERY_INFORMATION, FALSE, pid);

    if (!hProcess)
    {
        return 0;
    }

    FILETIME creation, exit, kernel, user;

    if (!GetProcessTimes(hProcess, &creation, &exit, &kernel, &user))
    {
        CloseHandle(hProcess);
        return 0;
    }
    ULARGE_INTEGER k, u;
    k.LowPart = kernel.dwLowDateTime;
    k.HighPart = kernel.dwHighDateTime;
    
    u.LowPart = user.dwLowDateTime;
    u.HighPart = user.dwHighDateTime;

    CloseHandle(hProcess);
    return k.QuadPart + u.QuadPart;
}

ULONGLONG GetCPUUsage::getSystemTime()
{
    FILETIME idle, kernel, user;

    GetSystemTimes(&idle, &kernel, &user);

    ULARGE_INTEGER k, u;
    k.LowPart = kernel.dwLowDateTime;
    k.HighPart = kernel.dwHighDateTime;

    u.LowPart = user.dwLowDateTime;
    u.HighPart = user.dwHighDateTime;

    return k.QuadPart + u.QuadPart;
}

double GetCPUUsage::getProcessCPU(DWORD pid, ULONGLONG currentSys)
{
    std::lock_guard<std::mutex> lock(mtx);

    ULONGLONG currentProc = getProcessTime(pid);

    ULONGLONG prevProc = lastProcTime[pid];
    ULONGLONG prevSys = lastSysTimeMap[pid];

    lastProcTime[pid] = currentProc;
    lastSysTimeMap[pid] = currentSys;

    if (prevProc == 0 || prevSys == 0)
        return 0;

    ULONGLONG deltaProc = currentProc - prevProc;
    ULONGLONG deltaSys = currentSys - prevSys;

    if (deltaSys == 0)
        return 0;

    SYSTEM_INFO sysInfo;
    GetSystemInfo(&sysInfo);

    return (double)deltaProc / deltaSys * sysInfo.dwNumberOfProcessors * 100.0;
}
double GetCPUUsage::getTotalCPU()
{
    FILETIME idle, kernel, user;

    if (!GetSystemTimes(&idle, &kernel, &user))
        return 0;

    ULARGE_INTEGER i, k, u;

    i.LowPart = idle.dwLowDateTime;
    i.HighPart = idle.dwHighDateTime;

    k.LowPart = kernel.dwLowDateTime;
    k.HighPart = kernel.dwHighDateTime;

    u.LowPart = user.dwLowDateTime;
    u.HighPart = user.dwHighDateTime;

    ULONGLONG idleTime = i.QuadPart;
    ULONGLONG kernelTime = k.QuadPart;
    ULONGLONG userTime = u.QuadPart;

    ULONGLONG deltaIdle = idleTime - lastIdle;
    ULONGLONG deltaKernel = kernelTime - lastKernel;
    ULONGLONG deltaUser = userTime - lastUser;

    lastIdle = idleTime;
    lastKernel = kernelTime;
    lastUser = userTime;

    ULONGLONG total = deltaKernel + deltaUser;

    if (total == 0)
        return 0;

    double cpu = (double)(total - deltaIdle) / total * 100.0;

    return cpu;
}

void GetCPUUsage::cleanup(const std::vector<ProcessStruct>& processes)
{
    std::lock_guard<std::mutex> lock(mtx);

    std::set<DWORD> currentPids;

    for (const auto& p : processes)
        currentPids.insert(p.id);

    for (auto it = lastProcTime.begin(); it != lastProcTime.end(); )
    {
        if (currentPids.find(it->first) == currentPids.end())
        {
            lastSysTimeMap.erase(it->first); // 🔥 ВАЖНО
            it = lastProcTime.erase(it);
        }
        else
            ++it;
    }
}
