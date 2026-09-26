#include "CpuLimiter.h"

void CpuLimiter::limit(DWORD pid, int cpuPercent)
{
    std::lock_guard<std::mutex> lock(mtx);   // <-- в начале функции
    if (jobMap.find(pid) != jobMap.end())
        return;

    HANDLE hProcess = OpenProcess(PROCESS_SET_QUOTA | PROCESS_TERMINATE, FALSE, pid);
    if (!hProcess)
        return;

    HANDLE hJob = CreateJobObject(nullptr, nullptr);
    if (!hJob)
    {
        CloseHandle(hProcess);
        return;
    }

    JOBOBJECT_CPU_RATE_CONTROL_INFORMATION info = {};
    info.ControlFlags = JOB_OBJECT_CPU_RATE_CONTROL_ENABLE |
        JOB_OBJECT_CPU_RATE_CONTROL_HARD_CAP;
    info.CpuRate = cpuPercent * 100;

    if (!SetInformationJobObject(hJob, JobObjectCpuRateControlInformation, &info, sizeof(info)) ||
        !AssignProcessToJobObject(hJob, hProcess))
    {
        // не получилось настроить/привязать — не оставляем висячий Job-хендл
        CloseHandle(hJob);
        CloseHandle(hProcess);
        return;
    }

    jobMap[pid] = hJob;
    CloseHandle(hProcess); // хендл процесса больше не нужен, Job уже привязан
}

void CpuLimiter::cleanup(const std::vector<DWORD>& activePids)
{
    std::lock_guard<std::mutex> lock(mtx);   // <-- в начале функции
    std::set<DWORD> current(activePids.begin(), activePids.end());

    for (auto it = jobMap.begin(); it != jobMap.end(); )
    {
        if (current.find(it->first) == current.end())
        {
            CloseHandle(it->second);   // <-- вот чего не хватало
            it = jobMap.erase(it);
        }
        else
        {
            ++it;
        }
    }
}
void CpuLimiter::release(DWORD pid)
{
    std::lock_guard<std::mutex> lock(mtx);

    auto it = jobMap.find(pid);
    if (it == jobMap.end())
        return; // не лимитирован — нечего снимать

    // "Снятие лимита" = поднимаем CpuRate до 100% (10000 в единицах API).
    // Сам Job-объект и хендл остаются привязаны к процессу — это нормально,
    // отвязать процесс от Job'а без его завершения Windows не позволяет.
    JOBOBJECT_CPU_RATE_CONTROL_INFORMATION info = {};
    info.ControlFlags = JOB_OBJECT_CPU_RATE_CONTROL_ENABLE |
        JOB_OBJECT_CPU_RATE_CONTROL_HARD_CAP;
    info.CpuRate = 100 * 100; // 100%

    SetInformationJobObject(it->second, JobObjectCpuRateControlInformation, &info, sizeof(info));

    // Хендл и запись оставляем в jobMap — процесс всё ещё "в системе",
    // просто без ограничения. Если хотите полностью забыть о процессе,
    // используйте cleanup() после его завершения, как и раньше.
}

void CpuLimiter::releaseAll()
{
    std::lock_guard<std::mutex> lock(mtx);

    JOBOBJECT_CPU_RATE_CONTROL_INFORMATION info = {};
    info.ControlFlags = JOB_OBJECT_CPU_RATE_CONTROL_ENABLE |
        JOB_OBJECT_CPU_RATE_CONTROL_HARD_CAP;
    info.CpuRate = 100 * 100;

    for (auto& [pid, hJob] : jobMap)
        SetInformationJobObject(hJob, JobObjectCpuRateControlInformation, &info, sizeof(info));
}
CpuLimiter::~CpuLimiter()
{
    std::lock_guard<std::mutex> lock(mtx);   // <-- в начале функции
    for (auto& [pid, hJob] : jobMap)
        CloseHandle(hJob);
}