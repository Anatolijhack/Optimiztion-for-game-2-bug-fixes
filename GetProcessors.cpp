#include "GetProcessors.h"

std::vector<ProcessStruct> GetProcesors::getAllProcesses()
{
    std::vector<ProcessStruct> processes;

    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);

    if (snapshot == INVALID_HANDLE_VALUE)
        return processes;

    PROCESSENTRY32W pe;
    pe.dwSize = sizeof(PROCESSENTRY32W);

    if (Process32FirstW(snapshot, &pe))
    {
        do
        {
            ProcessStruct p;

            p.id = pe.th32ProcessID;
            p.name = pe.szExeFile;

            processes.push_back(p);

        } while (Process32NextW(snapshot, &pe));
    }

    CloseHandle(snapshot);
    return processes;
}
