#pragma once
#include <Windows.h>
#include <Pdh.h>
#include <PdhMsg.h>
#include <vector>
#include <string>
#include <unordered_map>
#pragma comment(lib, "pdh.lib")

class GetNetworkUsage
{
private:
    PDH_HQUERY query = nullptr;

    struct CounterEntry
    {
        PDH_HCOUNTER counter;
        DWORD pid;
    };
    std::vector<CounterEntry> counters;
    bool available = false;

    // Имя инстанса категории "Process" выглядит как "chrome#3", "csgo",
    // "javaw#1" и т.д. — без PID напрямую. Поэтому берём PID через
    // отдельный счётчик "ID Process" того же инстанса.
    static DWORD getPidForInstance(PDH_HQUERY q, const std::wstring& instanceName)
    {
        std::wstring path = L"\\Process(" + instanceName + L")\\ID Process";
        PDH_HCOUNTER counter;
        DWORD pid = 0;

        if (PdhAddCounterW(q, path.c_str(), 0, &counter) == ERROR_SUCCESS)
        {
            PdhCollectQueryData(q);
            PDH_FMT_COUNTERVALUE value;
            if (PdhGetFormattedCounterValue(counter, PDH_FMT_LONG, nullptr, &value) == ERROR_SUCCESS)
                pid = static_cast<DWORD>(value.longValue);
            PdhRemoveCounter(counter);
        }
        return pid;
    }

public:
    GetNetworkUsage()
    {
        if (PdhOpenQuery(nullptr, 0, &query) != ERROR_SUCCESS)
            return;

        refreshCounters();
        available = !counters.empty();
    }

    // Пересобирает список процессов и их IO-счётчиков.
    // Вызывать периодически (раз в 10 тиков), не каждый тик.
    void refreshCounters()
    {
        if (!query) return;

        for (auto& entry : counters)
            PdhRemoveCounter(entry.counter);
        counters.clear();

        DWORD counterListSize = 0, instanceListSize = 0;
        PdhEnumObjectItemsW(nullptr, nullptr, L"Process",
            nullptr, &counterListSize, nullptr, &instanceListSize,
            PERF_DETAIL_WIZARD, 0);

        if (instanceListSize == 0)
        {
            available = false;
            return;
        }

        std::vector<wchar_t> counterList(counterListSize);
        std::vector<wchar_t> instanceList(instanceListSize);

        if (PdhEnumObjectItemsW(nullptr, nullptr, L"Process",
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
            if (instanceName == L"_Total" || instanceName == L"Idle") continue;

            // "IO Data Bytes/sec" включает дисковый + сетевой I/O суммарно
            // на большинстве систем это доминирует сетевой трафик для
            // большинства обычных приложений (не идеально точно, но
            // достаточно для выявления "кто активно передаёт данные")
            std::wstring path = L"\\Process(" + instanceName + L")\\IO Data Bytes/sec";
            PDH_HCOUNTER counter;
            if (PdhAddCounterW(query, path.c_str(), 0, &counter) == ERROR_SUCCESS)
            {
                DWORD pid = getPidForInstance(query, instanceName);
                if (pid != 0)
                    counters.push_back({ counter, pid });
            }
        }

        available = !counters.empty();
    }

    // Возвращает карту pid -> байт/сек (IO Data, дисковый + сетевой суммарно)
    void collect(std::unordered_map<DWORD, double>& perProcessBytesPerSec)
    {
        perProcessBytesPerSec.clear();
        if (!available || !query) return;

        if (PdhCollectQueryData(query) != ERROR_SUCCESS)
            return;

        for (auto& entry : counters)
        {
            PDH_FMT_COUNTERVALUE value;
            if (PdhGetFormattedCounterValue(entry.counter, PDH_FMT_DOUBLE, nullptr, &value) == ERROR_SUCCESS)
                perProcessBytesPerSec[entry.pid] += value.doubleValue;
        }
    }

    bool isAvailable() const { return available; }

    ~GetNetworkUsage()
    {
        if (query) PdhCloseQuery(query);
    }
};