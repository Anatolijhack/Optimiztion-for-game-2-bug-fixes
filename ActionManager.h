#pragma once
#include <Windows.h>
#include <unordered_map>
#include <string>
#include <iostream>
#include <functional>
#include "ProcessStruct.h"

class ActionManager
{
private:
    
    std::unordered_map<DWORD, DWORD> originalPriority;
    std::unordered_map<DWORD, time_t> lastAction;
    int maxRestoresPerTick;

    static const wchar_t* reasonToString(LowerReason r)
    {
        switch (r)
        {
        case LowerReason::CPU: return L" (CPU)";
        case LowerReason::GPU: return L" (GPU)";
        case LowerReason::NET: return L" (NET)";
        default: return L" (RAM)";
        }
    }

public:
    using EcoQosFn = std::function<void(HANDLE, bool)>;
    explicit ActionManager(int maxRestoresPerTick_) : maxRestoresPerTick(maxRestoresPerTick_) {}

    bool isLowered(DWORD pid) const { return originalPriority.find(pid) != originalPriority.end(); }

    bool applyLower(DWORD pid, const std::wstring& name, LowerReason reason,
        EcoQosFn setEco, time_t now)
    {
        if (isLowered(pid)) return false;

        HANDLE h = OpenProcess(PROCESS_SET_INFORMATION | PROCESS_QUERY_INFORMATION, FALSE, pid);
        if (!h) return false;

        DWORD current = GetPriorityClass(h);
        originalPriority[pid] = (current != IDLE_PRIORITY_CLASS) ? current : NORMAL_PRIORITY_CLASS;
        SetPriorityClass(h, IDLE_PRIORITY_CLASS);
        if (setEco) setEco(h, true);
        CloseHandle(h);

        lastAction[pid] = now;
        std::wcout << L"[LOWER] " << name << reasonToString(reason) << std::endl;
        return true;
    }

    void restoreAllAndClear(EcoQosFn setEco)
    {
        for (auto& [pid, prio] : originalPriority)
        {
            HANDLE h = OpenProcess(PROCESS_SET_INFORMATION, FALSE, pid);
            if (h)
            {
                SetPriorityClass(h, prio);
                if (setEco) setEco(h, false);
                CloseHandle(h);
            }
        }
        originalPriority.clear();
        lastAction.clear();
    }
    
    void beginTick() { restoredThisTick = 0; }

    bool tryRestore(DWORD pid, const std::wstring& name, EcoQosFn setEco)
    {
        if (!isLowered(pid)) return false;
        if (restoredThisTick >= maxRestoresPerTick) return false;

        HANDLE h = OpenProcess(PROCESS_SET_INFORMATION, FALSE, pid);
        if (h)
        {
            SetPriorityClass(h, originalPriority[pid]);
            if (setEco) setEco(h, false);
            CloseHandle(h);
            std::wcout << L"[RESTORE] " << name << std::endl;
            restoredThisTick++;
        }
        originalPriority.erase(pid);
        lastAction.erase(pid);
        return true;
    }

    void cleanupDead(const std::vector<DWORD>& alivePids)
    {
        auto sweep = [&](auto& map)
            {
                for (auto it = map.begin(); it != map.end(); )
                {
                    bool alive = std::find(alivePids.begin(), alivePids.end(), it->first) != alivePids.end();
                    it = alive ? std::next(it) : map.erase(it);
                }
            };
        sweep(originalPriority);
        sweep(lastAction);
    }

    bool empty() const { return originalPriority.empty(); }

private:
    int restoredThisTick = 0;
};