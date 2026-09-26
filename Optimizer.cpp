#include "ProcessOptimizer.h"
#include "ThreadPool.h"
#include <Windows.h>

ProcessOptimizer::ProcessOptimizer(ThreadPool& p)
    : pool(p) {}

bool ProcessOptimizer::isGame(const ProcessStruct& p)
{
    return (p.name.find(L"csgo.exe") != std::wstring::npos ||
        p.name.find(L"dota2.exe") != std::wstring::npos ||
        p.name.find(L"javaw.exe") != std::wstring::npos);
}

int ProcessOptimizer::calculatePriority(const ProcessStruct& p)
{
    if (isGame(p)) return 100;

    if (p.CPUusage > 25.0) return 50;
    if (p.memory > 500 * 1024 * 1024) return 40;

    return 10;
}

void ProcessOptimizer::optimizeProcess(const ProcessStruct& p)
{
    if (p.id < 1000) return; // не трогаем системные

    HANDLE h = OpenProcess(PROCESS_SET_INFORMATION, FALSE, p.id);
    if (!h) return;

    if (isGame(p))
    {
        SetPriorityClass(h, HIGH_PRIORITY_CLASS);
    }
    else if (p.CPUusage > 25.0)
    {
        SetPriorityClass(h, BELOW_NORMAL_PRIORITY_CLASS);
    }

    CloseHandle(h);
}

void ProcessOptimizer::optimize(std::vector<ProcessStruct>& processes)
{
    for (auto& p : processes)
    {
        int pr = calculatePriority(p);

        pool.push_task([this, p]()
            {
                optimizeProcess(p);
            }, pr);
    }
}