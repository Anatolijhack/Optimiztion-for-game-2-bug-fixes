#include <iostream>
#include <vector>
#include <thread>
#include <chrono>
#include <algorithm>
#include "ProcessOptimizer.h"
#include "GetCPUUsage.h"
#include "GetMemory.h"
#include "GetProcessors.h"
#include "ProcessStruct.h"
#include "ThreadPool.h"
#include "CpuLimiter.h"
#include "GPUOptimiztion.h"
#include "Enqous.h"
#include "NetworkOptimizer.h"
#include "GetNetworkUsage.h"
#include "FrameTimeMonitor.h"
#include "PresentMonCapture.h"
#include "SystemStateMachine.h"
#include "ActionManager.h"
#include <fstream>
#include "HardwareProfile.h"
#include "NetworkEnvironmentDetector.h"
#include "BenchmarkSession.h"
#include "DecisionEngine.h"
#include "AutoTuner.h"
#include "ConfigStore.h"
#include "SessionHistory.h"
#include "GameProfile.h"
#include "UserGameList.h"
//bool isTargetProcess(const ProcessStruct& p)
//{
//    return _wcsicmp(p.name.c_str(), L"dota2.exe") == 0;
//}

void limitCPU(DWORD pid)
{
    HANDLE hProcess = OpenProcess(PROCESS_SET_INFORMATION, FALSE, pid);
    if (hProcess)
    {
        SetPriorityClass(hProcess, BELOW_NORMAL_PRIORITY_CLASS);
        CloseHandle(hProcess);
    }
}
double getScore(const ProcessStruct& p)
{
    double cpu = p.CPUusage;
    double mem = (double)p.memory / (1024 * 1024); // MB
    return cpu * 0.7 + mem * 0.3;
}
void lowerPriority(DWORD pid)
{
    HANDLE hProcess = OpenProcess(PROCESS_SET_INFORMATION, FALSE, pid);
    if (hProcess)
    {
        SetPriorityClass(hProcess, BELOW_NORMAL_PRIORITY_CLASS);
        CloseHandle(hProcess);
    }
}
std::vector<std::wstring> g_knownGameProcessNames = { L"cs2.exe", L"dota2.exe", L"javaw.exe" };

bool isTargetProcess(const ProcessStruct& p)
{
    for (auto& name : g_knownGameProcessNames)
    {
        if (_wcsicmp(p.name.c_str(), name.c_str()) == 0)
            return true;
    }
    return false;
}
void addUserGame(const std::wstring& processName, const std::wstring& hwFingerprint)
{
    // 1. Сохраняем в постоянный список (user_games.txt), чтобы подхватилось при следующем запуске
    if (!UserGameList::add(processName))
    {
        std::wcout << L"[USER] '" << processName << L"' is already tracked\n";
        return;
    }

    // 2. Добавляем в текущий рабочий список — isTargetProcess увидит её СРАЗУ, без перезапуска
    g_knownGameProcessNames.push_back(processName);

    // 3. Создаём (или подтверждаем существование) профиля для этой игры на ЭТОМ железе
    std::wstring profileKey = processName + L"_" + hwFingerprint;
    GameProfile newProfile = GameProfileStore::findOrCreate(profileKey);
    newProfile.profileKey = profileKey;
    newProfile.processName = processName;
    newProfile.displayName = processName; // пользователь не вводил "красивое" имя — используем имя exe
    GameProfileStore::saveOrUpdate(newProfile); // сразу сохраняем в game_profiles.csv, даже без кастомных порогов

    std::wcout << L"[USER] Added '" << processName << L"' as a tracked game (profile key: " << profileKey << L")\n";
}
bool isMemoryHeavy(const ProcessStruct& p)
{
    SIZE_T mb = p.memory / 1024 / 1024;
    return mb > 400; // 🔥 порог (можно менять)
}
//void limitCPU(DWORD pid)
//{
//    HANDLE hProcess = OpenProcess(PROCESS_SET_INFORMATION, FALSE, pid);
//    if (hProcess)
//    {
//        SetProcessAffinityMask(hProcess, 1); 
//        CloseHandle(hProcess);
//    }
//}

bool isSystemProcess(const ProcessStruct& p)
{
    return p.id < 1000 || p.name == L"System" || p.name == L"Idle";
}

std::atomic<bool> g_shutdownRequested = false;

BOOL WINAPI consoleCtrlHandler(DWORD signal)
{
    switch (signal)
    {
    case CTRL_C_EVENT:
    case CTRL_BREAK_EVENT:
    case CTRL_CLOSE_EVENT:
    case CTRL_LOGOFF_EVENT:
    case CTRL_SHUTDOWN_EVENT:
        g_shutdownRequested = true;
        while (g_shutdownRequested.load())
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        return TRUE;
    default:
        return FALSE;
    }
}
void boostPriority(DWORD pid, EcoQoS& ecoQos)
{
    HANDLE hProcess = OpenProcess(PROCESS_SET_INFORMATION, FALSE, pid);
    if (hProcess)
    {
        SetPriorityClass(hProcess, HIGH_PRIORITY_CLASS);
        ecoQos.setEcoQoS(hProcess, false); 
        CloseHandle(hProcess);
    }
}

int main() {
    if (!SetConsoleCtrlHandler(consoleCtrlHandler, TRUE))
        std::wcerr << L"[WARN] Failed to register console control handler\n";

    GetProcesors procGetter;
    GetProcesMemory memGetter;
    GetCPUUsage cpuGetter;
    CpuLimiter limiter;
    GetGPUUsage gpuGetter;
    EcoQoS ecoQos;
    GetNetworkUsage netGetter;
    NetworkOptimizer netOptimizer;
    FrameTimeMonitor frameMonitor;
    PresentMonCapture presentMonCapture;
    ABTestProtocol abTest;

    int gpuRefreshCounter = 0;
    const int GPU_REFRESH_EVERY_N_TICKS = 10;

    std::unordered_map<DWORD, time_t> lastHardLimitAction;
    std::unordered_map<DWORD, double> gpuPerProcess;
    std::unordered_map<DWORD, double> netPerProcess;

    const double NET_HEAVY_OFF = 1.0 * 1024 * 1024;
    const double NET_HEAVY_ON = 2.0 * 1024 * 1024;

    DWORD lastBoosted = 0;
    DWORD myPid = GetCurrentProcessId();

    HardwareProfile hw = HardwareProfile::detect();

    std::wcout
        << L"[HARDWARE] Detected "
        << hw.logicalCores
        << L" logical cores, "
        << hw.totalRamMB
        << L" MB RAM\n";

    std::wstring hwFingerprint =
        HardwareFingerprint::compute(
            hw.logicalCores,
            hw.totalRamMB
        );

    std::wcout
        << L"[HARDWARE] Fingerprint: "
        << hwFingerprint
        << L"\n";

    auto userGames = UserGameList::load();

    for (auto& name : userGames) {
        g_knownGameProcessNames.push_back(name);

        std::wstring profileKey =
            name + L"_" + hwFingerprint;

        if (!GameProfileStore::exists(profileKey)) {
            GameProfile p;

            // ВАЖНО: profileKey обязательно заполняем
            p.profileKey = profileKey;

            // processName — именно имя exe
            p.processName = name;

            // displayName — отображаемое имя игры
            p.displayName = name;

            GameProfileStore::saveOrUpdate(p);
        }
    }

    ConfigStore config;

    // остаётся как общий fallback, если у игры ещё нет профиля
    ThreadPool pool(hw.threadPoolSize);

    const double GPU_HEAVY_PROCESS_THRESHOLD_DEFAULT = 10.0;
    const SIZE_T RAM_HEAVY_THRESHOLD_MB =
        hw.ramHeavyThresholdMB;

    const int MAX_RESTORES_PER_TICK = 10;

    // ---- Текущий активный профиль игры — определяется динамически ----
    GameProfile activeGameProfile;
    bool profileLoaded = false;
    std::wstring profileKey;

    double baseCpuOnAtSessionStart = 0.0; // NEW

    double cpuAdjustment = 0.0;
    double ramAdjustment = 0.0;
    double gpuAdjustment = 0.0;

    std::vector<double> baselineLow1Samples;
    // NEW: 1% low FPS в "off"-блоках A/B

    std::vector<double> interventionLow1Samples;

    time_t lastAbCheckpoint = 0;
    const int AB_CHECKPOINT_INTERVAL_SEC = 1800;
    const bool ENABLE_AB_TESTING = true;

    auto finalizeAbTest = [&]()
        {
            std::wcout << L"\n[AB-TEST] finalize: entered\n" << std::flush;

            // Сохраняем имя текущей игры ДО любых изменений activeGameProfile
            const std::wstring gameLabel =
                (profileLoaded && !activeGameProfile.displayName.empty())
                ? activeGameProfile.displayName
                : L"(без игры)";

            const size_t baselineCount = baselineLow1Samples.size();
            const size_t interventionCount = interventionLow1Samples.size();

            std::wcout
                << L"[AB-TEST] game=" << gameLabel << L"\n"
                << L"[AB-TEST] baseline=" << baselineCount
                << L", intervention=" << interventionCount
                << L"\n"
                << std::flush;

            // Недостаточно данных
            if (baselineCount < 5 || interventionCount < 5)
            {
                std::wcout
                    << L"[AB-TEST] Not enough samples for "
                    << gameLabel
                    << L" (baseline="
                    << baselineCount
                    << L", intervention="
                    << interventionCount
                    << L")\n"
                    << std::flush;

                baselineLow1Samples.clear();
                interventionLow1Samples.clear();

                std::wcout
                    << L"[AB-TEST] finalize: reset done\n"
                    << std::flush;

                return;
            }

            std::wcout
                << L"[AB-TEST] Running statistical comparison...\n"
                << std::flush;

            // Выполняем сравнение
            const StatValidator::TestResult result =
                StatValidator::compare(
                    baselineLow1Samples,
                    interventionLow1Samples
                );

            std::wcout
                << L"[AB-TEST] Comparison completed\n"
                << std::flush;

            // Копируем результат в простые локальные переменные
            const double meanDiff = result.meanDiff;
            const double confidence = result.confidence;
            const bool significant = result.significant;
            const size_t n = result.n;

            // Выводим результат по частям
            std::wcout << L"[AB-TEST] Result:\n" << std::flush;

            std::wcout
                << L"  game        = "
                << gameLabel
                << L"\n"
                << std::flush;

            std::wcout
                << L"  meanDiff    = "
                << meanDiff
                << L" FPS\n"
                << std::flush;

            std::wcout
                << L"  significant = "
                << (significant ? L"YES" : L"NO")
                << L"\n"
                << std::flush;

            std::wcout
                << L"  confidence  = "
                << confidence
                << L"\n"
                << std::flush;

            std::wcout
                << L"  n           = "
                << n
                << L"\n"
                << std::flush;

            // Очистка после полного завершения обработки результата
            std::wcout
                << L"[AB-TEST] Clearing samples...\n"
                << std::flush;

            baselineLow1Samples.clear();

            std::wcout
                << L"[AB-TEST] baseline cleared\n"
                << std::flush;

            interventionLow1Samples.clear();

            std::wcout
                << L"[AB-TEST] intervention cleared\n"
                << std::flush;

            std::wcout
                << L"[AB-TEST] finalize: done\n"
                << std::flush;
        };

    // ---- Пороги — пересчитываются, как только определится targetPID/игра ----
    double cpuOn = hw.cpuOverloadOn;
    double cpuOff = hw.cpuOverloadOff;

    double ramOn = hw.ramLowOn;
    double ramOff = hw.ramLowOff;

    double gpuOn = hw.gpuOverloadOn;
    double gpuOff = hw.gpuOverloadOff;

    double gpuHeavyThreshold =
        GPU_HEAVY_PROCESS_THRESHOLD_DEFAULT;

    bool enableNetworkQosForGame = false;

    SystemStateMachine::Thresholds th{
        cpuOn,
        cpuOff,
        ramOn,
        ramOff,
        gpuOn,
        gpuOff,
        hw.stateChangeCooldownSec
    };

    SystemStateMachine stateMachine(th);

    ActionManager actionManager(
        MAX_RESTORES_PER_TICK
    );

    ActionManager::EcoQosFn ecoQosFn =
        [&ecoQos](HANDLE h, bool eco) {
        ecoQos.setEcoQoS(h, eco);
        };

    DecisionEngine decisionEngine;
    BenchmarkSession benchmark;

    const bool AUTO_START_BENCHMARK = true;

    double preActionLow1Percent = 0.0;
    time_t preActionTime = 0;

    double previousLow1Percent = 0.0;
    bool recentlyStabilized = false;

    double HARD_LIMIT_CPU_MIN =
        hw.hardLimitCpuMin;

    double HARD_LIMIT_CPU_MAX =
        hw.hardLimitCpuMax;

    const double HARD_LIMIT_SYSTEM_SHARE =
        hw.hardLimitSystemShare;

    const int HARD_LIMIT_CAP_PERCENT =
        hw.hardLimitCapPercent;

    const int HARD_LIMIT_COOLDOWN_SEC =
        hw.hardLimitCooldownSec;

    int vpnCheckCounter = 0;
    const int VPN_CHECK_EVERY_N_TICKS = 30;

    bool vpnDetected =
        NetworkEnvironmentDetector::isVpnOrTunnelActive();

    std::wcout
        << L"\nAdd a custom game? Enter process name "
        L"(e.g. stalker2.exe) or press Enter to skip: ";

    std::wstring userInput;
    std::getline(std::wcin, userInput);

    if (!userInput.empty()) {
        addUserGame(
            userInput,
            hwFingerprint
        );
        // <-- один вызов делает всё: список + профиль
    }

    while (!g_shutdownRequested.load()) {

        auto processes =
            procGetter.getAllProcesses();

        std::vector<DWORD> pids;
        pids.reserve(processes.size());

        for (const auto& p : processes)
            pids.push_back(p.id);

        cpuGetter.cleanup(processes);
        limiter.cleanup(pids);

        ULONGLONG sysTime =
            cpuGetter.getSystemTime();

        const int batchSize = 10;

        for (
            size_t i = 0;
            i < processes.size();
            i += batchSize
            ) {
            pool.push_task(
                [
                    i,
                        &processes,
                        &memGetter,
                        &cpuGetter,
                        sysTime
                ]() {
                    for (
                        size_t j = i;
                        j < i + batchSize &&
                        j < processes.size();
                        j++
                        ) {
                        auto& p = processes[j];

                        p.memory =
                            memGetter.getProcessMemory(
                                p.id
                            );

                        p.CPUusage =
                            cpuGetter.getProcessCPU(
                                p.id,
                                sysTime
                            );
                    }
                },
                        0
                        );
        }

        pool.wait();

        if (
            ++gpuRefreshCounter >=
            GPU_REFRESH_EVERY_N_TICKS
            ) {
            gpuGetter.refreshCounters();
            netGetter.refreshCounters();
            gpuRefreshCounter = 0;
        }

        if (
            ++vpnCheckCounter >=
            VPN_CHECK_EVERY_N_TICKS
            ) {
            vpnDetected =
                NetworkEnvironmentDetector::isVpnOrTunnelActive();

            vpnCheckCounter = 0;

            if (vpnDetected) {
                std::wcout
                    << L"[NETWORK] VPN/tunnel adapter detected "
                    L"-> network-based process actions disabled\n";
            }
        }

        double totalGpu3D =
            gpuGetter.collect(&gpuPerProcess);

        netGetter.collect(netPerProcess);

        double totalCPU =
            cpuGetter.getTotalCPU();

        recentlyStabilized = false;

        if (
            frameMonitor.isRunning() &&
            !actionManager.empty()
            ) {
            double currentLow1 =
                frameMonitor.get1PercentLowFPS();

            if (previousLow1Percent > 0.0) {
                recentlyStabilized =
                    (
                        currentLow1 >
                        previousLow1Percent * 1.05
                        );
            }

            previousLow1Percent =
                currentLow1;
        }

        DWORD targetPID = 0;

        ProcessStruct* worst = nullptr;
        ProcessStruct* targetCandidate = nullptr;

        for (auto& p : processes) {

            if (p.id == myPid)
                continue;

            if (isTargetProcess(p)) {

                if (
                    !targetCandidate ||
                    p.memory >
                    targetCandidate->memory
                    ) {
                    targetCandidate = &p;
                }
            }

            if (isSystemProcess(p))
                continue;

            if (
                !worst ||
                getScore(p) > getScore(*worst)
                ) {
                worst = &p;
            }
        }

        if (targetCandidate)
            targetPID =
            targetCandidate->id;

        bool gameInForeground = false;

        if (targetPID != 0) {

            HWND hForeground =
                GetForegroundWindow();

            DWORD foregroundPid = 0;

            if (hForeground) {
                GetWindowThreadProcessId(
                    hForeground,
                    &foregroundPid
                );
            }

            gameInForeground =
                (foregroundPid == targetPID);
        }

        if (
            targetPID != 0 &&
            targetPID != lastBoosted
            ) {

            if (lastBoosted != 0) {

                // была предыдущая игра —
                // корректно закрыть её мониторинг

                presentMonCapture.stop();
                frameMonitor.stop();

                if (benchmark.isRecording()) {

                    benchmark.stop();

                    benchmark.saveReport(
                        L"benchmark_report_" +
                        profileKey +
                        L".txt"
                    );

                    auto seg =
                        benchmark.computeSegmentedResult();

                    if (
                        profileLoaded &&
                        !profileKey.empty()
                        ) {

                        SessionHistory::appendSession(
                            profileKey,
                            seg.avgCpu,
                            seg.low1Cpu,
                            seg.samplesCpu,
                            seg.avgRam,
                            seg.low1Ram,
                            seg.samplesRam,
                            seg.avgGpu,
                            seg.low1Gpu,
                            seg.samplesGpu,
                            seg.avgIdle,
                            seg.low1Idle,
                            seg.samplesIdle
                        );

                        if (cpuAdjustment != 0.0) {

                            activeGameProfile
                                .metricAtLastAdjustment =
                                seg.low1Cpu;

                            activeGameProfile
                                .hasPendingEvaluation =
                                true;
                        }

                        if (ramAdjustment != 0.0) {

                            activeGameProfile
                                .metricAtLastRamAdjustment =
                                seg.low1Ram;

                            activeGameProfile
                                .hasPendingRamEvaluation =
                                true;
                        }

                        if (gpuAdjustment != 0.0) {

                            activeGameProfile
                                .metricAtLastGpuAdjustment =
                                seg.low1Gpu;

                            activeGameProfile
                                .hasPendingGpuEvaluation =
                                true;
                        }

                        activeGameProfile
                            .hasCustomCpuOn = true;

                        activeGameProfile
                            .cpuOverloadOn = cpuOn;

                        activeGameProfile
                            .hasCustomCpuOff = true;

                        activeGameProfile
                            .CpuOverloadOff = cpuOff;

                        activeGameProfile
                            .hasCustomRamOn = true;

                        activeGameProfile
                            .ramLowOn = ramOn;

                        activeGameProfile
                            .hasCustomRamOff = true;

                        activeGameProfile
                            .ramLowOff = ramOff;

                        activeGameProfile
                            .hasCustomGpuOn = true;

                        activeGameProfile
                            .gpuOverloadOn = gpuOn;

                        activeGameProfile
                            .hasCustomGpuOff = true;

                        activeGameProfile
                            .gpuOverloadOff = gpuOff;

                        GameProfileStore::saveOrUpdate(
                            activeGameProfile
                        );
                    }
                }

                limiter.releaseAll();
                lastHardLimitAction.clear();

                actionManager.restoreAllAndClear(
                    ecoQosFn
                );

                // <-- важно: вернуть приоритеты
                // фона от старой игры

                if (ENABLE_AB_TESTING) {
                    finalizeAbTest();
                }

                // сбросить A/B таймер для новой игры

                //Тут тоже
                preActionTime = 0;
                preActionLow1Percent = 0.0;
                previousLow1Percent = 0.0;
                recentlyStabilized = false;
                lastAbCheckpoint = 0;

                std::wcout
                    << L"[GAME] Switched from "
                    << activeGameProfile.displayName
                    << L" to new target\n";
            }


            // ---- Загружаем/создаём профиль ИМЕННО этой игры ----

            profileKey =
                targetCandidate->name +
                L"_" +
                hwFingerprint;

            activeGameProfile =
                GameProfileStore::findOrCreate(
                    profileKey
                );

            activeGameProfile.profileKey =
                profileKey;

            activeGameProfile.processName =
                targetCandidate->name;

            profileLoaded = true;

            // ---- Автотюнинг + сохранённые значения —
            // теперь ПРИВЯЗАНЫ к профилю игры ----

            auto history =
                AutoTuner::readRecentHistory(
                    L"session_history.csv",
                    profileKey,
                    10
                );

            double baseCpuOn =
                activeGameProfile.hasCustomCpuOn
                ? activeGameProfile.cpuOverloadOn
                : hw.cpuOverloadOn;

            activeGameProfile.previousCpuOverloadOn =
                baseCpuOn;

            cpuAdjustment =
                AutoTuner::suggestCpuThresholdAdjustment(
                    history,
                    activeGameProfile
                    .consecutiveCpuAdjustments
                );

            if (cpuAdjustment > 0.0) {

                activeGameProfile
                    .consecutiveCpuAdjustments =
                    max(
                        0,
                        activeGameProfile
                        .consecutiveCpuAdjustments
                    ) + 1;
            }
            else if (cpuAdjustment < 0.0) {

                activeGameProfile
                    .consecutiveCpuAdjustments =
                    min(
                        0,
                        activeGameProfile
                        .consecutiveCpuAdjustments
                    ) - 1;
            }
            else {

                activeGameProfile
                    .consecutiveCpuAdjustments = 0;
            }

            cpuOn =
                std::clamp(
                    baseCpuOn + cpuAdjustment,
                    20.0,
                    90.0
                );

            double baseCpuOff =
                activeGameProfile.hasCustomCpuOff
                ? activeGameProfile.CpuOverloadOff
                : hw.cpuOverloadOff;

            cpuOff =
                std::clamp(
                    baseCpuOff + cpuAdjustment,
                    10.0,
                    cpuOn - 5.0
                );

            // держим зазор гистерезиса от cpuOn

            double baseRamOn =
                activeGameProfile.hasCustomRamOn
                ? activeGameProfile.ramLowOn
                : hw.ramLowOn;

            activeGameProfile.previousRamLowOn =
                baseRamOn;

            // <-- добавить

            ramAdjustment =
                AutoTuner::suggestRamThresholdAdjustment(
                    history,
                    activeGameProfile
                    .consecutiveRamAdjustment
                );

            if (ramAdjustment > 0.0) {

                activeGameProfile
                    .consecutiveRamAdjustment =
                    max(
                        0,
                        activeGameProfile
                        .consecutiveRamAdjustment
                    ) + 1;
            }
            else if (ramAdjustment < 0.0) {

                activeGameProfile
                    .consecutiveRamAdjustment =
                    min(
                        0,
                        activeGameProfile
                        .consecutiveRamAdjustment
                    ) - 1;
            }
            else {

                activeGameProfile
                    .consecutiveRamAdjustment = 0;
            }

            ramOn =
                std::clamp(
                    baseRamOn + ramAdjustment,
                    0.10,
                    0.50
                );

            double baseRamOff =
                activeGameProfile.hasCustomRamOff
                ? activeGameProfile.ramLowOff
                : hw.ramLowOff;

            ramOff =
                std::clamp(
                    baseRamOff + ramAdjustment,
                    ramOn + 0.02,
                    0.60
                );

            // ramOff должен быть выше ramOn
            // (больше свободной памяти = "спокойно")

            double baseGpuOn =
                activeGameProfile.hasCustomGpuOn
                ? activeGameProfile.gpuOverloadOn
                : hw.gpuOverloadOn;

            activeGameProfile.previousGpuOverloadOn =
                baseGpuOn;

            // <-- добавить

            gpuAdjustment =
                AutoTuner::suggestGpuThresholdAdjustment(
                    history,
                    activeGameProfile
                    .consecutiveGpuAdjustments
                );

            if (gpuAdjustment > 0.0) {

                activeGameProfile
                    .consecutiveGpuAdjustments =
                    max(
                        0,
                        activeGameProfile
                        .consecutiveGpuAdjustments
                    ) + 1;
            }
            else if (gpuAdjustment < 0.0) {

                activeGameProfile
                    .consecutiveGpuAdjustments =
                    min(
                        0,
                        activeGameProfile
                        .consecutiveGpuAdjustments
                    ) - 1;
            }
            else {

                activeGameProfile
                    .consecutiveGpuAdjustments = 0;
            }

            gpuOn =
                std::clamp(
                    baseGpuOn + gpuAdjustment,
                    50.0,
                    100.0
                );

            double baseGpuOff =
                activeGameProfile.hasCustomGpuOff
                ? activeGameProfile.gpuOverloadOff
                : hw.gpuOverloadOff;

            gpuOff =
                std::clamp(
                    baseGpuOff + gpuAdjustment,
                    30.0,
                    gpuOn - 5.0
                );

            // зазор гистерез

            gpuHeavyThreshold =
                activeGameProfile.gpuHeavyThreshold;

            enableNetworkQosForGame =
                activeGameProfile.enableNetworkQos;

            HARD_LIMIT_CPU_MIN =
                cpuOff;

            HARD_LIMIT_CPU_MAX =
                cpuOn;

            if (
                activeGameProfile.hasPendingEvaluation &&
                !history.empty()
                ) {

                double latestMetric =
                    history.back().low1FpsCpu;

                // самая свежая запись после прошлой правки

                if (
                    latestMetric <
                    activeGameProfile
                    .metricAtLastAdjustment *
                    0.95
                    ) {

                    std::wcout
                        << L"[ROLLBACK] Previous CPU threshold "
                        L"change hurt performance -> reverting\n";

                    cpuOn = activeGameProfile.previousCpuOverloadOn;

                    // откат к чистому hw-расчёту
                    // (или храните previousCpuOverloadOn отдельно)
                }

                activeGameProfile
                    .hasPendingEvaluation = false;
            }

            if (
                activeGameProfile.hasPendingRamEvaluation &&
                !history.empty()
                ) {

                double latestMetricRam =
                    history.back().low1FpsRam;

                // NEW

                if (
                    latestMetricRam <
                    activeGameProfile
                    .metricAtLastRamAdjustment *
                    0.95
                    ) {

                    std::wcout
                        << L"[ROLLBACK] "
                        << activeGameProfile.displayName
                        << L": RAM threshold change hurt performance "
                        L"-> reverting\n";

                    ramOn =
                        activeGameProfile.previousRamLowOn;

                    activeGameProfile
                        .hasCustomRamOn = true;
                }

                activeGameProfile
                    .hasPendingRamEvaluation = false;
            }

            // --- Оценка прошлой корректировки GPU ---

            if (
                activeGameProfile.hasPendingGpuEvaluation &&
                !history.empty()
                ) {

                double latestMetricGpu =
                    history.back().low1FpsGpu;

                // NEW

                if (
                    latestMetricGpu <
                    activeGameProfile
                    .metricAtLastGpuAdjustment *
                    0.95
                    ) {

                    std::wcout
                        << L"[ROLLBACK] "
                        << activeGameProfile.displayName
                        << L": GPU threshold change hurt performance "
                        L"-> reverting\n";

                    gpuOn =
                        activeGameProfile.previousGpuOverloadOn;

                    activeGameProfile
                        .hasCustomGpuOn = true;
                }

                activeGameProfile
                    .hasPendingGpuEvaluation = false;
            }

            if (
                cpuAdjustment != 0.0 ||
                ramAdjustment != 0.0 ||
                gpuAdjustment != 0.0
                ) {

                std::wcout
                    << L"[AUTOTUNE] Profile '"
                    << activeGameProfile.displayName
                    << L"': "
                    << L"CPU "
                    << cpuAdjustment
                    << L" | RAM "
                    << ramAdjustment
                    << L" | GPU "
                    << gpuAdjustment
                    << L" (based on "
                    << history.size()
                    << L" sessions)\n";
            }

            // ---- Пересобираем state machine
            // с порогами именно этой игры ----

            SystemStateMachine::Thresholds newTh{
                cpuOn,
                cpuOff,
                ramOn,
                ramOff,
                gpuOn,
                gpuOff,
                hw.stateChangeCooldownSec
            };

            stateMachine =
                SystemStateMachine(newTh);

            std::wcout
                << L"[GAME] Detected: "
                << activeGameProfile.displayName
                << L" ("
                << activeGameProfile.processName
                << L")\n";

            std::wcout
                << L"[THRESHOLDS] CPU "
                << cpuOn
                << L"/"
                << cpuOff
                << L" | RAM "
                << ramOn
                << L"/"
                << ramOff
                << L" | GPU "
                << gpuOn
                << L"/"
                << gpuOff
                << L"\n";

            boostPriority(
                targetPID,
                ecoQos
            );

            if (!vpnDetected) {
                netOptimizer.optimizeForGame(
                    activeGameProfile.processName
                );
            }

            frameMonitor.start(targetPID);

            // сначала frameMonitor

            bool pmStarted =
                presentMonCapture.start(
                    targetPID,
                    frameMonitor
                );

            // потом PresentMon, один раз

            if (!pmStarted) {
                std::wcout
                    << L"[WARN] PresentMon failed to start "
                    L"— frame time features disabled this session\n";
            }

            lastBoosted = targetPID;

            if (AUTO_START_BENCHMARK) {

                benchmark.start();
                abTest.start();
            }

            lastAbCheckpoint = 0;

            std::wcout
                << L"[BOOST] Target process boosted"
                << (
                    netOptimizer.isActive()
                    ? L" (+ Network QoS)"
                    : L""
                    )
                << L"\n";
        }

        time_t now = time(nullptr);

        bool abTestAllowsOptimization =
            !ENABLE_AB_TESTING ||
            abTest.shouldOptimizerBeActive(now);

        if (frameMonitor.isRunning() && gameInForeground) {

            double currentLow1ForAB =
                frameMonitor.get1PercentLowFPS();

            if (currentLow1ForAB > 0.0) {

                if (abTestAllowsOptimization)
                    interventionLow1Samples.push_back(
                        currentLow1ForAB
                    );
                else
                    baselineLow1Samples.push_back(
                        currentLow1ForAB
                    );
            }

            // NEW: промежуточный чекпоинт —
            // не ждём смены игры/шатдауна

            if (lastAbCheckpoint == 0) {

                lastAbCheckpoint = now;
            }
            else if (
                now - lastAbCheckpoint >=
                AB_CHECKPOINT_INTERVAL_SEC
                ) {

                finalizeAbTest();

                // выведет [AB-TEST] и очистит векторы

                lastAbCheckpoint = now;
            }
        }

        DWORD gpuWorstPid = 0;
        double gpuWorstUsage = 0.0;

        if (gpuGetter.isAvailable()) {

            for (
                auto& [pid, usage] :
                gpuPerProcess
                ) {

                if (
                    pid == targetPID ||
                    pid == myPid
                    )
                    continue;

                if (usage > gpuWorstUsage) {

                    gpuWorstUsage = usage;
                    gpuWorstPid = pid;
                }
            }
        }

        DWORD netWorstPid = 0;
        double netWorstBytes = 0.0;

        if (netGetter.isAvailable()) {

            for (
                auto& [pid, bytes] :
                netPerProcess
                ) {

                if (
                    pid == targetPID ||
                    pid == myPid
                    )
                    continue;

                if (bytes > netWorstBytes) {

                    netWorstBytes = bytes;
                    netWorstPid = pid;
                }
            }
        }

        SIZE_T totalRAM;
        SIZE_T freeRAM;
        SIZE_T usedRAM;

        memGetter.getSystemMemory(
            totalRAM,
            freeRAM,
            usedRAM
        );

        double freePercent =
            (double)freeRAM /
            totalRAM;

        stateMachine.update(
            totalCPU,
            freePercent,
            totalGpu3D,
            gpuGetter.isAvailable(),
            now
        );

        if (
            worst &&
            worst->id != targetPID
            ) {

            if (
                worst->CPUusage >
                HARD_LIMIT_CPU_MIN &&
                worst->CPUusage <
                HARD_LIMIT_CPU_MAX &&
                worst->CPUusage >
                totalCPU *
                HARD_LIMIT_SYSTEM_SHARE &&
                !stateMachine.isGpuOverloaded()
                ) {

                auto it =
                    lastHardLimitAction.find(
                        worst->id
                    );

                if (
                    it ==
                    lastHardLimitAction.end() ||
                    now - it->second >
                    HARD_LIMIT_COOLDOWN_SEC
                    ) {

                    limiter.limit(
                        worst->id,
                        HARD_LIMIT_CAP_PERCENT
                    );

                    lastHardLimitAction[
                        worst->id
                    ] = now;

                    std::wcout
                        << L"[LIMIT] Hard limited: "
                        << worst->name
                        << std::endl;
                }
            }
        }

        if (stateMachine.isCpuOverloaded()) {
            std::wcout
                << L"[GLOBAL] System overloaded "
                L"-> background limited\n";
        }

        if (stateMachine.isRamLow()) {
            std::wcout
                << L"[RAM] Low free memory "
                L"-> heavy processes limited\n";
        }

        if (stateMachine.isGpuOverloaded()) {
            std::wcout
                << L"[GPU] GPU near saturation "
                L"-> CPU/RAM optimization deprioritized\n";
        }

        if (recentlyStabilized) {
            std::wcout
                << L"[STABILIZING] Frame time improving "
                L"-> suppressing new lowers\n";
        }

        actionManager.beginTick();

        DecisionContext ctx{
            stateMachine.isCpuOverloaded(),
            stateMachine.isRamLow(),
            stateMachine.isGpuOverloaded(),
            vpnDetected,
            targetPID,
            myPid,
            gpuWorstPid,
            gpuWorstUsage,
            gpuHeavyThreshold,
            netWorstPid,
            netWorstBytes,
            NET_HEAVY_ON,
            NET_HEAVY_OFF,
            RAM_HEAVY_THRESHOLD_MB,
            recentlyStabilized
        };

        for (auto& p : processes) {

            if (isSystemProcess(p))
                continue;

            if (
                p.name.find(L"PresentMon") !=
                std::wstring::npos
                )
                continue;

            bool isCurrentlyLowered =
                actionManager.isLowered(p.id);

            LowerReason reason;

            bool shouldBeLowered =
                decisionEngine.shouldLower(
                    p,
                    ctx,
                    isCurrentlyLowered,
                    reason
                );

            if (!abTestAllowsOptimization)
                shouldBeLowered = false;

            if (
                shouldBeLowered &&
                !isCurrentlyLowered
                ) {

                if (
                    actionManager.applyLower(
                        p.id,
                        p.name,
                        reason,
                        ecoQosFn,
                        now
                    )
                    ) {

                    if (
                        preActionTime == 0 &&
                        frameMonitor.isRunning() &&
                        gameInForeground
                        ) {

                        preActionLow1Percent =
                            frameMonitor.get1PercentLowFPS();

                        preActionTime = now;
                    }
                }
            }
            else if (
                !shouldBeLowered &&
                isCurrentlyLowered
                ) {

                actionManager.tryRestore(
                    p.id,
                    p.name,
                    ecoQosFn
                );
            }
        }

        actionManager.cleanupDead(pids);

        for (
            auto it = lastHardLimitAction.begin();
            it != lastHardLimitAction.end();
            ) {

            bool stillAlive =
                std::find(
                    pids.begin(),
                    pids.end(),
                    it->first
                ) != pids.end();

            it =
                stillAlive
                ? std::next(it)
                : lastHardLimitAction.erase(it);
        }

        bool currentAbBlock =
            !ENABLE_AB_TESTING ||
            abTest.shouldOptimizerBeActive(now);

        if (
            benchmark.isRecording() &&
            frameMonitor.isRunning() &&
            gameInForeground
            ) {

            benchmark.record(
                frameMonitor.getAverageFrameTime(),
                currentAbBlock,
                gameInForeground,
                stateMachine.isCpuOverloaded(),
                stateMachine.isRamLow(),
                stateMachine.isGpuOverloaded()
            );
        }

        if (
            preActionTime != 0 &&
            now - preActionTime >= 10
            ) {

            double currentLow1 =
                frameMonitor.get1PercentLowFPS();

            double improvement =
                currentLow1 -
                preActionLow1Percent;

            std::wofstream effectLog(
                "optimization_effect.log",
                std::ios::app
            );

            effectLog
                << L"Action at "
                << preActionTime
                << L": 1% low before="
                << preActionLow1Percent
                << L", after="
                << currentLow1
                << L", delta="
                << improvement
                << std::endl;

            preActionTime = 0;
        }

        std::wcout
            << L"\nTotal RAM: "
            << (totalRAM / 1024 / 1024)
            << L" MB";

        std::wcout
            << L"\nUsed RAM: "
            << (usedRAM / 1024 / 1024)
            << L" MB";

        std::wcout
            << L"\nFree RAM: "
            << (freeRAM / 1024 / 1024)
            << L" MB";

        std::wcout
            << L"\nTotal CPU: "
            << totalCPU
            << L"%\n\n";

        if (gpuGetter.isAvailable()) {

            std::wcout
                << L"\nGPU 3D Usage: "
                << totalGpu3D
                << L"%";

            if (targetPID != 0) {

                auto it =
                    gpuPerProcess.find(
                        targetPID
                    );

                double targetGpu =
                    (
                        it != gpuPerProcess.end()
                        )
                    ? it->second
                    : 0.0;

                std::wcout
                    << L" | Target process GPU: "
                    << targetGpu
                    << L"%";
            }
        }
        else {

            std::wcout
                << L"\nGPU metrics: unavailable on this system";
        }

        if (
            netGetter.isAvailable() &&
            netWorstPid != 0
            ) {

            std::wcout
                << L"\nHeaviest network user PID "
                << netWorstPid
                << L": "
                << (
                    netWorstBytes /
                    1024.0 /
                    1024.0
                    )
                << L" MB/s";
        }

        std::wcout
            << L"\nTarget PID: "
            << targetPID;

        if (targetPID == 0) {

            std::wcout
                << L" (target process not found!)";
        }
        else if (profileLoaded) {

            std::wcout
                << L" ("
                << activeGameProfile.displayName
                << L")";
        }

        if (frameMonitor.isRunning()) {

            std::wcout
                << L"\nFPS avg: "
                << frameMonitor.getAverageFPS()
                << L" | 1% low: "
                << frameMonitor.get1PercentLowFPS()
                << L" | 0.1% low: "
                << frameMonitor.get01PercentLowFPS()
                << (
                    gameInForeground
                    ? L""
                    : L" [NOT FOREGROUND]"
                    );
        }

        if (ENABLE_AB_TESTING) {

            std::wcout
                << L"\n[AB-TEST] Current block: "
                << (
                    abTest.isCurrentBlockOn()
                    ? L"OPTIMIZER ON"
                    : L"OPTIMIZER OFF"
                    );
        }

        for (
            int i = 0;
            i < 10 &&
            !g_shutdownRequested.load();
            i++
            ) {

            std::this_thread::sleep_for(
                std::chrono::milliseconds(100)
            );
        }
    }

    std::wcout
        << L"\n[SHUTDOWN] Restoring process priorities "
        L"and releasing CPU limits...\n";

    actionManager.restoreAllAndClear(
        ecoQosFn
    );

    finalizeAbTest();

    if (benchmark.isRecording()) {

        benchmark.stop();

        benchmark.saveReport(
            L"benchmark_report.txt"
        );

        auto seg =
            benchmark.computeSegmentedResult();

        if (profileLoaded) {

            SessionHistory::appendSession(
                profileKey,
                seg.avgCpu,
                seg.low1Cpu,
                seg.samplesCpu,
                seg.avgRam,
                seg.low1Ram,
                seg.samplesRam,
                seg.avgGpu,
                seg.low1Gpu,
                seg.samplesGpu,
                seg.avgIdle,
                seg.low1Idle,
                seg.samplesIdle
            );

            if (cpuAdjustment != 0.0) {

                activeGameProfile
                    .metricAtLastAdjustment =
                    seg.low1Cpu;

                // метрика ЭТОЙ сессии с новым порогом

                activeGameProfile
                    .hasPendingEvaluation =
                    true;

                // проверим в следующий раз
            }

            if (ramAdjustment != 0.0) {
                // NEW

                activeGameProfile
                    .metricAtLastRamAdjustment =
                    seg.low1Ram;

                activeGameProfile
                    .hasPendingRamEvaluation =
                    true;
            }

            if (gpuAdjustment != 0.0) {
                // NEW

                activeGameProfile
                    .metricAtLastGpuAdjustment =
                    seg.low1Gpu;

                activeGameProfile
                    .hasPendingGpuEvaluation =
                    true;
            }

            // ---- Сохраняем итоговые пороги
            // ИМЕННО в профиль этой игры ----

            activeGameProfile
                .hasCustomCpuOn = true;

            activeGameProfile
                .cpuOverloadOn = cpuOn;

            activeGameProfile
                .hasCustomCpuOff = true;

            activeGameProfile
                .CpuOverloadOff = cpuOff;

            activeGameProfile
                .hasCustomRamOn = true;

            activeGameProfile
                .ramLowOn = ramOn;

            activeGameProfile
                .hasCustomRamOff = true;

            activeGameProfile
                .ramLowOff = ramOff;

            activeGameProfile
                .hasCustomGpuOn = true;

            activeGameProfile
                .gpuOverloadOn = gpuOn;

            activeGameProfile
                .hasCustomGpuOff = true;

            activeGameProfile
                .gpuOverloadOff = gpuOff;

            GameProfileStore::saveOrUpdate(
                activeGameProfile
            );
        }

        std::wcout
            << L"[BENCHMARK] Report saved to benchmark_report.txt\n";
    }

    presentMonCapture.stop();
    frameMonitor.stop();

    limiter.releaseAll();

    netOptimizer.restore();

    pool.shutdown();

    std::wcout
        << L"[SHUTDOWN] Cleanup complete.\n";

    g_shutdownRequested = false;

    return 0;
}