#pragma once
#pragma once
#include <Windows.h>
#include <processthreadsapi.h>

// Ќекоторые старые Windows SDK (до 10.0.17763) могут не иметь этих
// определений в заголовках Ч подстраховываемс€ ручным объ€влением,
// если компил€тор их не находит. «начени€ соответствуют официальному
// Windows SDK и не мен€ютс€ между верси€ми.
#ifndef PROCESS_POWER_THROTTLING_CURRENT_VERSION
#define PROCESS_POWER_THROTTLING_CURRENT_VERSION 1
#endif

#ifndef PROCESS_POWER_THROTTLING_EXECUTION_SPEED
#define PROCESS_POWER_THROTTLING_EXECUTION_SPEED 0x1
#endif

#ifndef ProcessPowerThrottling
// PROCESS_INFORMATION_CLASS enum value дл€ EcoQoS Ч если SDK его не знает,
// объ€вл€ем вручную. «начение 4 соответствует официальному SDK.
#define ProcessPowerThrottling_Manual 4
#endif

#ifndef _PROCESS_POWER_THROTTLING_STATE_DEFINED
#define _PROCESS_POWER_THROTTLING_STATE_DEFINED
typedef struct _PROCESS_POWER_THROTTLING_STATE_MANUAL
{
    ULONG Version;
    ULONG ControlMask;
    ULONG StateMask;
} PROCESS_POWER_THROTTLING_STATE_MANUAL;
#endif

class EcoQoS
{
private:
    using SetProcessInformationFn = BOOL(WINAPI*)(HANDLE, int, LPVOID, DWORD);
    SetProcessInformationFn pSetProcessInformation = nullptr;
    bool available = false;

public:
    EcoQoS()
    {
        HMODULE hKernel32 = GetModuleHandleW(L"kernel32.dll");
        if (hKernel32)
        {
            pSetProcessInformation = reinterpret_cast<SetProcessInformationFn>(
                GetProcAddress(hKernel32, "SetProcessInformation"));
        }
        available = (pSetProcessInformation != nullptr);
    }

    bool isAvailable() const { return available; }

    // enableEco = true  -> просим Windows не тратить турбо-буст на этот процесс
    //                      (энергоэффективный режим, похоже по духу на IDLE_PRIORITY_CLASS,
    //                      но действует на уровне QoS/буста, а не очереди планировщика)
    // enableEco = false -> €вно снимаем троттлинг, сигнал "нужна полна€ производительность"
    bool setEcoQoS(HANDLE hProcess, bool enableEco)
    {
        if (!available || !hProcess) return false;

        PROCESS_POWER_THROTTLING_STATE_MANUAL state = {};
        state.Version = PROCESS_POWER_THROTTLING_CURRENT_VERSION;
        state.ControlMask = PROCESS_POWER_THROTTLING_EXECUTION_SPEED;
        state.StateMask = enableEco ? PROCESS_POWER_THROTTLING_EXECUTION_SPEED : 0;

        int infoClass =
#ifdef ProcessPowerThrottling
            ProcessPowerThrottling;
#else
            ProcessPowerThrottling_Manual;
#endif

        return pSetProcessInformation(hProcess, infoClass, &state, sizeof(state)) != FALSE;
    }
};