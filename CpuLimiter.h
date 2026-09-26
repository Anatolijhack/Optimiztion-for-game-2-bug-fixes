#pragma once
#include <Windows.h>
#include <unordered_map>
#include <vector>
#include <set>
#include <mutex>

class CpuLimiter
{
private:
    std::unordered_map<DWORD, HANDLE> jobMap;
    std::mutex mtx;
public:
    ~CpuLimiter();               
    void limit(DWORD pid, int cpuPercent);
    void release(DWORD pid);                    // <-- новый метод: снять лимит (CpuRate = 100%)
    void releaseAll();                          // <-- снять лимиты со всех разом (для graceful shutdown)
    void cleanup(const std::vector<DWORD>& activePids);
};