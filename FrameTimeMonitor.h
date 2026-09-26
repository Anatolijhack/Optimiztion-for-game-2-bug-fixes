#pragma once
#include <Windows.h>
#include <deque>
#include <vector>
#include <mutex>
#include <algorithm>
#include <numeric>

class FrameTimeMonitor
{
private:
    mutable std::mutex mtx;

    DWORD targetPid = 0;
    bool running = false;

    // Последние frame times в миллисекундах.
    std::deque<double> frameTimes;

    size_t maxSamples = 2000;

    double averageFrameTime = 0.0;
    double low1Percent = 0.0;
    double low01Percent = 0.0;

private:
    void calculateStats()
    {
        if (frameTimes.empty())
        {
            averageFrameTime = 0.0;
            low1Percent = 0.0;
            low01Percent = 0.0;
            return;
        }

        std::vector<double> samples(
            frameTimes.begin(),
            frameTimes.end()
        );

        // Средний frame time.
        averageFrameTime =
            std::accumulate(
                samples.begin(),
                samples.end(),
                0.0
            ) / samples.size();

        // Сортируем от быстрого кадра к медленному.
        std::sort(samples.begin(), samples.end());

        // Худшие 1%.
        size_t count1 =
            std::max<size_t>(1, samples.size() / 100);

        double sum1 = 0.0;

        for (size_t i = samples.size() - count1;
            i < samples.size();
            ++i)
        {
            sum1 += samples[i];
        }

        low1Percent = sum1 / count1;

        // Худшие 0.1%.
        size_t count01 =
            std::max<size_t>(1, samples.size() / 1000);

        double sum01 = 0.0;

        for (size_t i = samples.size() - count01;
            i < samples.size();
            ++i)
        {
            sum01 += samples[i];
        }

        low01Percent = sum01 / count01;
    }

public:
    FrameTimeMonitor() = default;

    bool start(DWORD pid)
    {
        std::lock_guard<std::mutex> lock(mtx);

        targetPid = pid;
        running = true;

        frameTimes.clear();

        averageFrameTime = 0.0;
        low1Percent = 0.0;
        low01Percent = 0.0;

        /*
            Здесь запускается PresentMon/ETW capture,
            привязанный к targetPid.

            Например:

                StartPresentMonSession(pid);

            Реализацию ETW/PresetMon мы подключим
            следующим слоем.
        */

        return true;
    }

    void stop()
    {
        std::lock_guard<std::mutex> lock(mtx);

        running = false;
        targetPid = 0;

        frameTimes.clear();

        averageFrameTime = 0.0;
        low1Percent = 0.0;
        low01Percent = 0.0;
    }

    /*
        Вызывается PresentMon callback'ом при каждом новом Present.

        frameTimeMs — длительность кадра.
    */
    void onFrame(double frameTimeMs)
    {
        if (frameTimeMs <= 0.0 ||
            frameTimeMs > 1000.0)
            return;

        std::lock_guard<std::mutex> lock(mtx);

        if (!running)
            return;

        frameTimes.push_back(frameTimeMs);

        if (frameTimes.size() > maxSamples)
            frameTimes.pop_front();

        calculateStats();
    }

    double getAverageFrameTime() const
    {
        std::lock_guard<std::mutex> lock(mtx);
        return averageFrameTime;
    }

    double get1PercentFrameTime() const
    {
        std::lock_guard<std::mutex> lock(mtx);
        return low1Percent;
    }

    double get01PercentFrameTime() const
    {
        std::lock_guard<std::mutex> lock(mtx);
        return low01Percent;
    }

    double getAverageFPS() const
    {
        std::lock_guard<std::mutex> lock(mtx);

        if (averageFrameTime <= 0.0)
            return 0.0;

        return 1000.0 / averageFrameTime;
    }

    double get1PercentLowFPS() const
    {
        std::lock_guard<std::mutex> lock(mtx);

        if (low1Percent <= 0.0)
            return 0.0;

        return 1000.0 / low1Percent;
    }

    double get01PercentLowFPS() const
    {
        std::lock_guard<std::mutex> lock(mtx);

        if (low01Percent <= 0.0)
            return 0.0;

        return 1000.0 / low01Percent;
    }
    bool isRunning() const
    {
        std::lock_guard<std::mutex> lock(mtx);
        return running;
    }

    DWORD getTargetPid() const
    {
        std::lock_guard<std::mutex> lock(mtx);
        return targetPid;
    }
};