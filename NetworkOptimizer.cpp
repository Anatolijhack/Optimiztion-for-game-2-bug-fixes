#include "NetworkOptimizer.h"
#include <vector>

bool NetworkOptimizer::runPowerShell(const std::wstring& command)
{
    std::wstring fullCommand =
        L"powershell.exe -NoProfile -NonInteractive -ExecutionPolicy Bypass "
        L"-Command \"" + command + L"\"";

    STARTUPINFOW si{};
    PROCESS_INFORMATION pi{};

    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;

    std::vector<wchar_t> buffer(fullCommand.begin(), fullCommand.end());
    buffer.push_back(L'\0');

    BOOL result = CreateProcessW(
        nullptr, buffer.data(), nullptr, nullptr, FALSE,
        CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi);

    if (!result)
        return false;

    WaitForSingleObject(pi.hProcess, INFINITE);

    DWORD exitCode = 1;
    GetExitCodeProcess(pi.hProcess, &exitCode);

    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);

    return exitCode == 0;
}

bool NetworkOptimizer::optimizeForGame(const std::wstring& processName)
{
    // Если политика уже активна для ЭТОГО ЖЕ процесса — ничего не делаем
    if (active && targetProcessName == processName)
        return true;

    // Если активна для ДРУГОГО процесса (например, цель сменилась
    // между сессиями) — сначала снимаем старую
    if (active)
        restore();

    std::wstring command =
        L"Remove-NetQosPolicy "
        L"-Name '" + policyName + L"' "
        L"-Confirm:$false "
        L"-ErrorAction SilentlyContinue; "

        L"New-NetQosPolicy "
        L"-Name '" + policyName + L"' "
        L"-AppPathNameMatchCondition '" + processName + L"' "  // <-- теперь параметр
        L"-IPProtocolMatchCondition Both "
        L"-DSCPAction 46 "
        L"-PolicyStore LocalMachine "
        L"-Confirm:$false "
        L"-ErrorAction Stop";

    if (!runPowerShell(command))
        return false;

    active = true;
    targetProcessName = processName;
    return true;
}

bool NetworkOptimizer::restore()
{
    if (!active)
        return true;

    std::wstring command =
        L"Remove-NetQosPolicy "
        L"-Name '" + policyName + L"' "
        L"-PolicyStore LocalMachine "
        L"-Confirm:$false "
        L"-ErrorAction SilentlyContinue";

    bool result = runPowerShell(command);

    if (result)
    {
        active = false;
        targetProcessName.clear();
    }

    return result;
}

NetworkOptimizer::~NetworkOptimizer()
{
    restore();
}