#include "GetMemory.h"

SIZE_T GetProcesMemory::getProcessMemory(DWORD pid)
{
    HANDLE hProcess = OpenProcess(
        PROCESS_QUERY_INFORMATION | PROCESS_VM_READ,
        FALSE,
        pid
    );

    if (!hProcess)
        return 0;

    PROCESS_MEMORY_COUNTERS_EX pmc;

    if (GetProcessMemoryInfo(
        hProcess,
        (PROCESS_MEMORY_COUNTERS*)&pmc,
        sizeof(pmc)))
    {
        CloseHandle(hProcess);
        return pmc.PrivateUsage; // 🔥 лучше
    }

    CloseHandle(hProcess);
    return 0;
}
void GetProcesMemory::getSystemMemory(SIZE_T& total, SIZE_T& free, SIZE_T& used)
{
    MEMORYSTATUSEX mem;
    mem.dwLength = sizeof(mem);

    if (GlobalMemoryStatusEx(&mem))
    {
        total = mem.ullTotalPhys; // всего RAM
        free = mem.ullAvailPhys; // свободно
        used = total - free;     // используется
    }
    else
    {
        total = free = used = 0;
    }
}