#pragma once
#include <Windows.h>

struct HardwareProfile
{
    int logicalCores;
    SIZE_T totalRamMB;

    double cpuOverloadOn, cpuOverloadOff;
    double ramLowOn, ramLowOff;
    double gpuOverloadOn, gpuOverloadOff;
    double hardLimitCpuMin, hardLimitCpuMax, hardLimitSystemShare;
    int hardLimitCapPercent, hardLimitCooldownSec;
    SIZE_T ramHeavyThresholdMB;
    int stateChangeCooldownSec;
    int threadPoolSize;

    static HardwareProfile detect()
    {
        SYSTEM_INFO sysInfo;
        GetSystemInfo(&sysInfo);
        int cores = static_cast<int>(sysInfo.dwNumberOfProcessors);

        MEMORYSTATUSEX mem{};
        mem.dwLength = sizeof(mem);
        GlobalMemoryStatusEx(&mem);
        SIZE_T ramMB = static_cast<SIZE_T>(mem.ullTotalPhys / 1024 / 1024);

        HardwareProfile p;
        p.logicalCores = cores;
        p.totalRamMB = ramMB;

        // ---- Категория по числу потоков ----
        bool lowCoreCount = cores <= 4;   // слабые ноуты, старые Pentium/Core2
        bool highCoreCount = cores >= 8;  // современные многопоточные CPU

        // ---- Категория по RAM ----
        bool lowRam = ramMB <= 4096;      // 2-4 ГБ
        bool mediumRam = ramMB > 4096 && ramMB <= 12288; // 6-12 ГБ
        // >12288 считаем "высокий RAM"

        if (lowCoreCount)
        {
            p.cpuOverloadOn = 55.0;
            p.cpuOverloadOff = 35.0;
            p.hardLimitCpuMin = 45.0;
            p.hardLimitCpuMax = 90.0;
            p.hardLimitSystemShare = 0.20;
            p.hardLimitCapPercent = 15;
            p.hardLimitCooldownSec = 8;
            p.stateChangeCooldownSec = 20;
            p.threadPoolSize = 2;
        }
        else if (highCoreCount)
        {
            p.cpuOverloadOn = 50.0;   // было 75.0
            p.cpuOverloadOff = 30.0;  // было 60.0
            p.hardLimitCpuMin = 35.0; // было 50.0 — тоже снижаем пропорционально
            p.hardLimitCpuMax = 90.0; // было 95.0, немного снизил про запас
            p.hardLimitSystemShare = 0.25; // было 0.30, чуть чувствительнее
            p.hardLimitCapPercent = 20;
            p.hardLimitCooldownSec = 5;
            p.stateChangeCooldownSec = 15;
            p.threadPoolSize = 4; // <-- см. отдельное замечание ниже
        }
        else // средний диапазон, 5-7 потоков
        {
            p.cpuOverloadOn = 55.0;   // было 65.0
            p.cpuOverloadOff = 35.0;  // было 45.0
            p.hardLimitCpuMin = 40.0; // было 48.0
            p.hardLimitCpuMax = 90.0; // было 92.0
            p.hardLimitSystemShare = 0.22; // было 0.25
            p.hardLimitCapPercent = 18;
            p.hardLimitCooldownSec = 6;
            p.stateChangeCooldownSec = 18;
            p.threadPoolSize = 3;
        }


        if (lowRam)
        {
            p.ramLowOn = 0.40;
            p.ramLowOff = 0.55;
            p.ramHeavyThresholdMB = 150;
        }
        else if (mediumRam)
        {
            p.ramLowOn = 0.30;
            p.ramLowOff = 0.42;
            p.ramHeavyThresholdMB = 250;
        }
        else
        {
            p.ramLowOn = 0.25;
            p.ramLowOff = 0.35;
            p.ramHeavyThresholdMB = 300;
        }

        // GPU-пороги не зависят от CPU/RAM категории — единые
        p.gpuOverloadOn = 90.0;
        p.gpuOverloadOff = 40.0;

        return p;
    }
};