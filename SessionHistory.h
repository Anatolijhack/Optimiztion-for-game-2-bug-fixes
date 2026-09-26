#pragma once
#include <Windows.h>
#include <fstream>
#include <string>
#include <ctime>

class SessionHistory
{
private:
    static std::wstring getExeDirectory()
    {
        wchar_t path[MAX_PATH];
        GetModuleFileNameW(nullptr, path, MAX_PATH);
        std::wstring fullPath(path);
        size_t pos = fullPath.find_last_of(L"\\/");
        return (pos != std::wstring::npos) ? fullPath.substr(0, pos + 1) : L"";
    }

public:
    static void appendSession(const std::wstring& targetProcessName,
        double avgCpu, double low1Cpu, size_t samplesCpu,
        double avgRam, double low1Ram, size_t samplesRam,
        double avgGpu, double low1Gpu, size_t samplesGpu,
        double avgIdle, double low1Idle, size_t samplesIdle)
    {
        std::wstring path = getExeDirectory() + L"session_history.csv";
        bool fileExists = (GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES);

        std::wofstream f(path, std::ios::app);
        if (!fileExists)
        {
            f << L"timestamp,process,"
                << L"avg_fps_cpu,low1_fps_cpu,samples_cpu,"
                << L"avg_fps_ram,low1_fps_ram,samples_ram,"
                << L"avg_fps_gpu,low1_fps_gpu,samples_gpu,"
                << L"avg_fps_idle,low1_fps_idle,samples_idle\n";
        }

        time_t now = time(nullptr);
        auto toFps = [](double ms) { return ms > 0 ? 1000.0 / ms : 0.0; };

        f << now << L"," << targetProcessName << L","
            << toFps(avgCpu) << L"," << toFps(low1Cpu) << L"," << samplesCpu << L","
            << toFps(avgRam) << L"," << toFps(low1Ram) << L"," << samplesRam << L","
            << toFps(avgGpu) << L"," << toFps(low1Gpu) << L"," << samplesGpu << L","
            << toFps(avgIdle) << L"," << toFps(low1Idle) << L"," << samplesIdle << L"\n";
    }
};