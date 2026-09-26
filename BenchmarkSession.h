#pragma once
#include <vector>
#include <fstream>
#include <ctime>
#include <numeric>
#include <algorithm>

struct BenchmarkSample
{
    time_t timestamp;
    double frameTimeMs;
    bool optimizerActive;
    bool gameInForeground;
    bool cpuOverloaded;
    bool ramLow;
    bool gpuOverloaded;
};

class BenchmarkSession
{
private:
    std::vector<BenchmarkSample> samples;
    bool recording = false;

public:
    void start() { recording = true; samples.clear(); }
    void stop() { recording = false; }
    bool isRecording() const { return recording; }

    void record(double frameTimeMs, bool optimizerActive, bool gameInForeground,
        bool cpuOverloaded, bool ramLow, bool gpuOverloaded)
    {
        if (!recording) return;

        if (frameTimeMs <= 0.0 || frameTimeMs > 1000.0)
            return;
        samples.push_back({ time(nullptr), frameTimeMs, optimizerActive,
            gameInForeground, cpuOverloaded, ramLow, gpuOverloaded });
    }

    static double percentileLow(std::vector<double>& values, double percentile)
    {
        if (values.empty()) return 0.0;
        std::sort(values.begin(), values.end());
        size_t count = std::max<size_t>(1, static_cast<size_t>(values.size() * percentile));
        double sum = 0.0;
        for (size_t i = values.size() - count; i < values.size(); i++) sum += values[i];
        return sum / count;
    }

    struct SegmentStats
    {
        size_t count = 0;
        double avgMs = 0.0, minMs = 0.0, maxMs = 0.0, low1Ms = 0.0;
    };

    static SegmentStats computeStats(std::vector<double> values)
    {
        SegmentStats s;
        s.count = values.size();
        if (values.empty()) return s;

        s.avgMs = std::accumulate(values.begin(), values.end(), 0.0) / values.size();
        s.minMs = *std::min_element(values.begin(), values.end());
        s.maxMs = *std::max_element(values.begin(), values.end());
        s.low1Ms = percentileLow(values, 0.01);
        return s;
    }

    // ---- ƒобавлено: агрегированный результат дл€ SessionHistory/AutoTuner ----
    struct Result
    {
        double avgWithOptimizer = 0.0, low1WithOptimizer = 0.0;
        double avgWithoutOptimizer = 0.0, low1WithoutOptimizer = 0.0;
        size_t samplesWith = 0, samplesWithout = 0;
    };

    Result computeResult() const
    {
        std::vector<double> withOpt, withoutOpt;

        for (auto& s : samples)
        {
            if (!s.gameInForeground) continue;
            if (s.optimizerActive) withOpt.push_back(s.frameTimeMs);
            else withoutOpt.push_back(s.frameTimeMs);
        }

        Result r;
        r.samplesWith = withOpt.size();
        r.samplesWithout = withoutOpt.size();

        auto withStats = computeStats(withOpt);
        auto withoutStats = computeStats(withoutOpt);

        r.avgWithOptimizer = withStats.avgMs;
        r.low1WithOptimizer = withStats.low1Ms;
        r.avgWithoutOptimizer = withoutStats.avgMs;
        r.low1WithoutOptimizer = withoutStats.low1Ms;

        return r;
    }

    struct SegmentedResult
    {
        double avgCpu = 0.0, low1Cpu = 0.0;
        double avgRam = 0.0, low1Ram = 0.0;
        double avgGpu = 0.0, low1Gpu = 0.0;
        double avgIdle = 0.0, low1Idle = 0.0; // "чистый" baseline без каких-либо триггеров
        size_t samplesCpu = 0, samplesRam = 0, samplesGpu = 0, samplesIdle = 0;
    };

    SegmentedResult computeSegmentedResult() const
    {
        std::vector<double> cpuVals, ramVals, gpuVals, idleVals;

        for (auto& s : samples)
        {
            if (!s.gameInForeground) continue;

            if (s.cpuOverloaded) cpuVals.push_back(s.frameTimeMs);
            if (s.ramLow) ramVals.push_back(s.frameTimeMs);
            if (s.gpuOverloaded) gpuVals.push_back(s.frameTimeMs);
            if (!s.cpuOverloaded && !s.ramLow && !s.gpuOverloaded)
                idleVals.push_back(s.frameTimeMs);
        }

        SegmentedResult r;
        r.samplesCpu = cpuVals.size();
        r.samplesRam = ramVals.size();
        r.samplesGpu = gpuVals.size();
        r.samplesIdle = idleVals.size();

        auto cpuStats = computeStats(cpuVals);
        auto ramStats = computeStats(ramVals);
        auto gpuStats = computeStats(gpuVals);
        auto idleStats = computeStats(idleVals);

        r.avgCpu = cpuStats.avgMs;   r.low1Cpu = cpuStats.low1Ms;
        r.avgRam = ramStats.avgMs;   r.low1Ram = ramStats.low1Ms;
        r.avgGpu = gpuStats.avgMs;   r.low1Gpu = gpuStats.low1Ms;
        r.avgIdle = idleStats.avgMs; r.low1Idle = idleStats.low1Ms;

        return r;
    }
    // ---- конец вставки ----

    void saveReport(const std::wstring& path) const
    {
        std::vector<double> withOpt, withoutOpt;
        std::vector<double> cpuState, ramState, gpuState, idleState;
        size_t excludedNotForeground = 0;

        for (auto& s : samples)
        {
            if (!s.gameInForeground) { excludedNotForeground++; continue; }

            if (s.optimizerActive) withOpt.push_back(s.frameTimeMs);
            else withoutOpt.push_back(s.frameTimeMs);

            if (s.cpuOverloaded) cpuState.push_back(s.frameTimeMs);
            if (s.ramLow) ramState.push_back(s.frameTimeMs);
            if (s.gpuOverloaded) gpuState.push_back(s.frameTimeMs);
            if (!s.cpuOverloaded && !s.ramLow && !s.gpuOverloaded && !s.optimizerActive)
                idleState.push_back(s.frameTimeMs);
        }

        auto printSegment = [](std::wofstream& f, const wchar_t* label, const SegmentStats& s)
            {
                f << label << L": " << s.count << L" samples\n";
                if (s.count == 0) { f << L"  (no data)\n\n"; return; }
                f << L"  Avg: " << s.avgMs << L" ms (" << (1000.0 / s.avgMs) << L" FPS)\n";
                f << L"  Min: " << s.minMs << L" ms | Max: " << s.maxMs << L" ms\n";
                f << L"  1% low: " << s.low1Ms << L" ms (" << (1000.0 / s.low1Ms) << L" FPS)\n\n";
            };

        std::wofstream f(path);
        f << L"Benchmark Report\n================\n\n";
        f << L"Total samples: " << samples.size()
            << L" | Excluded (not foreground): " << excludedNotForeground << L"\n\n";

        printSegment(f, L"WITH optimizer action", computeStats(withOpt));
        printSegment(f, L"WITHOUT optimizer action", computeStats(withoutOpt));
        printSegment(f, L"During CPU overload", computeStats(cpuState));
        printSegment(f, L"During RAM low", computeStats(ramState));
        printSegment(f, L"During GPU saturation", computeStats(gpuState));
        printSegment(f, L"Fully idle (no triggers at all)", computeStats(idleState));

        if (withoutOpt.empty())
        {
            f << L"NOTE: no 'without optimizer' baseline was captured this session Ч "
                << L"the optimizer was active continuously. Comparison requires a session "
                << L"where triggers do not fire (e.g. lower thresholds temporarily, "
                << L"or close background apps causing constant RAM/CPU pressure).\n";
        }
    }
};
class ABTestProtocol
{
private:
    bool currentlyOn = true;
    time_t blockStartTime = 0;
    const int blockDurationSec = 60; // мен€ть режим каждую минуту

public:
    void start() { blockStartTime = time(nullptr); currentlyOn = true; }

    // ¬ызывать каждый тик Ч решает, должен ли оптимизатор действовать ѕ–яћќ —≈…„ј—
    bool shouldOptimizerBeActive(time_t now)
    {
        if (now - blockStartTime >= blockDurationSec)
        {
            currentlyOn = !currentlyOn; // переключаем блок
            blockStartTime = now;
        }
        return currentlyOn;
    }

    bool isCurrentBlockOn() const { return currentlyOn; }
};