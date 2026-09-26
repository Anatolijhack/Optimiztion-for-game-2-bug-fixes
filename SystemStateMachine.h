#pragma once
#include <Windows.h>
#include <ctime>

class SystemStateMachine
{
public:
    struct Thresholds
    {
        double cpuOn, cpuOff;
        double ramOnPercent, ramOffPercent; // äîëÿ ÑÂÎÁÎÄÍÎÉ ïàìÿòè
        double gpuOn, gpuOff;
        int stateChangeCooldownSec;
    };

private:
    Thresholds t;
    bool cpuOverloaded = false;
    bool ramLow = false;
    bool gpuOverloaded = false;
    time_t lastCpuStateChange = 0;

public:
    explicit SystemStateMachine(const Thresholds& thresholds) : t(thresholds) {}

    void update(double totalCPU, double freeRamPercent, double totalGpu3D, bool gpuAvailable, time_t now)
    {
        if (!cpuOverloaded && totalCPU > t.cpuOn &&
            (now - lastCpuStateChange > t.stateChangeCooldownSec))
        {
            cpuOverloaded = true;
            lastCpuStateChange = now;
        }
        else if (cpuOverloaded && totalCPU < t.cpuOff &&
            (now - lastCpuStateChange > t.stateChangeCooldownSec))
        {
            cpuOverloaded = false;
            lastCpuStateChange = now;
        }

        if (!ramLow && freeRamPercent < t.ramOnPercent)
            ramLow = true;
        else if (ramLow && freeRamPercent > t.ramOffPercent)
            ramLow = false;

        if (gpuAvailable)
        {
            if (!gpuOverloaded && totalGpu3D > t.gpuOn)
                gpuOverloaded = true;
            else if (gpuOverloaded && totalGpu3D < t.gpuOff)
                gpuOverloaded = false;
        }
    }

    bool isCpuOverloaded() const { return cpuOverloaded; }
    bool isRamLow() const { return ramLow; }
    bool isGpuOverloaded() const { return gpuOverloaded; }
};