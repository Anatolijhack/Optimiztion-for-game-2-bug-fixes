#pragma once

//#include <Windows.h>
//#include <Pdh.h>
//#include <PdhMsg.h>
//#include <vector>
//#include <string>
//#pragma comment(lib, "pdh.lib")
//
//class GetGPUUsage
//{
//private:
//    PDH_HQUERY query = nullptr;
//    std::vector<PDH_HCOUNTER> engineCounters;
//
//public:
//    GetGPUUsage()
//    {
//        PdhOpenQuery(nullptr, 0, &query);
//
//        // Собираем список всех GPU Engine инстансов (3D, Copy, VideoDecode и т.д.)
//        DWORD counterListSize = 0, instanceListSize = 0;
//        PdhEnumObjectItemsW(nullptr, nullptr, L"GPU Engine",
//            nullptr, &counterListSize, nullptr, &instanceListSize,
//            PERF_DETAIL_WIZARD, 0);
//
//        std::vector<wchar_t> counterList(counterListSize);
//        std::vector<wchar_t> instanceList(instanceListSize);
//
//        if (PdhEnumObjectItemsW(nullptr, nullptr, L"GPU Engine",
//            counterList.data(), &counterListSize,
//            instanceList.data(), &instanceListSize,
//            PERF_DETAIL_WIZARD, 0) == ERROR_SUCCESS)
//        {
//            for (wchar_t* instance = instanceList.data(); *instance; instance += wcslen(instance) + 1)
//            {
//                std::wstring path = L"\\GPU Engine(" + std::wstring(instance) + L")\\Utilization Percentage";
//                PDH_HCOUNTER counter;
//                if (PdhAddCounterW(query, path.c_str(), 0, &counter) == ERROR_SUCCESS)
//                    engineCounters.push_back(counter);
//            }
//        }
//    }
//
//  
//    double getTotalGpuUsage()
//    {
//        PdhCollectQueryData(query);
//
//        double total = 0.0;
//        for (auto& counter : engineCounters)
//        {
//            PDH_FMT_COUNTERVALUE value;
//            if (PdhGetFormattedCounterValue(counter, PDH_FMT_DOUBLE, nullptr, &value) == ERROR_SUCCESS)
//                total += value.doubleValue;
//        }
//        return total;
//    }
//
//    ~GetGPUUsage()
//    {
//        if (query) PdhCloseQuery(query);
//    }
//};

#include <Windows.h>
#include <Pdh.h>
#include <PdhMsg.h>
#include <vector>
#include <string>
#include <unordered_map>
#pragma comment(lib, "pdh.lib")

class GetGPUUsage
{
private:
    PDH_HQUERY query = nullptr;

    // Один счётчик 3D-движка -> к какому PID он относится
    struct CounterEntry
    {
        PDH_HCOUNTER counter;
        DWORD pid;
    };
    std::vector<CounterEntry> counters;

    bool available = false; // false, если "GPU Engine" категория недоступна (старые системы)

    // Извлекает pid из имени инстанса вида
    // "pid_1234_luid_0x...phys_0_eng_2_engtype_3D"
    static DWORD extractPid(const std::wstring& instanceName)
    {
        size_t pos = instanceName.find(L"pid_");
        if (pos == std::wstring::npos) return 0;

        pos += 4; // пропускаем "pid_"
        size_t end = instanceName.find(L'_', pos);
        if (end == std::wstring::npos) return 0;

        std::wstring pidStr = instanceName.substr(pos, end - pos);
        return static_cast<DWORD>(_wtoi(pidStr.c_str()));
    }

public:
    GetGPUUsage()
    {
        if (PdhOpenQuery(nullptr, 0, &query) != ERROR_SUCCESS)
            return;

        refreshCounters();
        available = !counters.empty(); // если счётчиков нет — категория недоступна
    }

    // Пересобирает список 3D-счётчиков заново. Вызывать периодически
    // (например, раз в 5-10 тиков), а не каждый тик — enumeration
    // сам по себе не бесплатная операция, и список процессов
    // и так уже обновляется вашим GetProcesors каждый тик.
    void refreshCounters()
    {
        if (!query) return;

        // Закрываем старые счётчики перед пересборкой
        for (auto& entry : counters)
            PdhRemoveCounter(entry.counter);
        counters.clear();

        DWORD counterListSize = 0, instanceListSize = 0;
        PdhEnumObjectItemsW(nullptr, nullptr, L"GPU Engine",
            nullptr, &counterListSize, nullptr, &instanceListSize,
            PERF_DETAIL_WIZARD, 0);

        if (instanceListSize == 0)
        {
            available = false;
            return; // категория недоступна на этой системе
        }

        std::vector<wchar_t> counterList(counterListSize);
        std::vector<wchar_t> instanceList(instanceListSize);

        if (PdhEnumObjectItemsW(nullptr, nullptr, L"GPU Engine",
            counterList.data(), &counterListSize,
            instanceList.data(), &instanceListSize,
            PERF_DETAIL_WIZARD, 0) != ERROR_SUCCESS)
        {
            available = false;
            return;
        }

        for (wchar_t* instance = instanceList.data(); *instance; instance += wcslen(instance) + 1)
        {
            std::wstring instanceName(instance);

            if (instanceName.find(L"engtype_3D") == std::wstring::npos)
                continue;

            std::wstring path = L"\\GPU Engine(" + instanceName + L")\\Utilization Percentage";
            PDH_HCOUNTER counter;
            if (PdhAddCounterW(query, path.c_str(), 0, &counter) == ERROR_SUCCESS)
            {
                DWORD pid = extractPid(instanceName);
                counters.push_back({ counter, pid });
            }
        }

        available = !counters.empty();
    }

    // Один сбор данных за тик. Возвращает общую 3D-загрузку и
    // заполняет perProcess картой pid -> суммарная 3D-загрузка этого pid
    // (процесс может иметь несколько 3D-инстансов, например на многодисплейных
    // или гибридных GPU-системах — они суммируются).
    double collect(std::unordered_map<DWORD, double>* perProcess = nullptr)
    {
        if (!available || !query) return 0.0;

        if (PdhCollectQueryData(query) != ERROR_SUCCESS)
            return 0.0;

        double total = 0.0;
        if (perProcess) perProcess->clear();

        for (auto& entry : counters)
        {
            PDH_FMT_COUNTERVALUE value;
            if (PdhGetFormattedCounterValue(entry.counter, PDH_FMT_DOUBLE, nullptr, &value) == ERROR_SUCCESS)
            {
                total += value.doubleValue;
                if (perProcess && entry.pid != 0)
                    (*perProcess)[entry.pid] += value.doubleValue;
            }
        }

        return total > 100.0 ? 100.0 : total;
    }

    bool isAvailable() const { return available; }

    ~GetGPUUsage()
    {
        if (query) PdhCloseQuery(query);
    }
};