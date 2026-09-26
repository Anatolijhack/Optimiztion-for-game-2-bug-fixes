
#pragma once
#include <Windows.h>
#include <string>
#include <vector>

class NetworkOptimizer
{
private:
    std::wstring policyName = L"GameBoostQoS";
    std::wstring targetProcessName; // <-- новое поле, запоминает, для кого активна политика
    bool active = false;

    bool runPowerShell(const std::wstring& command);

public:
    // processName — имя exe без пути, например L"dota2.exe" или L"csgo.exe"
    bool optimizeForGame(const std::wstring& processName);
    bool restore();
    bool isActive() const { return active; }
    const std::wstring& currentTarget() const { return targetProcessName; }

    ~NetworkOptimizer();
};