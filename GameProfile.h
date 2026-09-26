#pragma once
#include <string>
#include <vector>
#include <fstream>
#include <sstream>
#include <Windows.h>
class HardwareFingerprint
{
public:
    static std::wstring compute(int logicalCores, SIZE_T totalRamMB)
    {
        // √руппируем в "бакеты", чтобы небольшие различи€ (14205 vs 14200 ћЅ)
        // не создавали новый профиль каждый раз
        int coreBucket = logicalCores <= 4 ? 4 : (logicalCores <= 8 ? 8 : 16);
        int ramBucketGB = static_cast<int>((totalRamMB / 1024 + 1) / 4) * 4; // округление до 4 √Ѕ

        std::wstringstream ss;
        ss << L"hw" << coreBucket << L"c_" << ramBucketGB << L"gb";
        return ss.str();
    }
};
class RollbackManager
{
public:
    struct AdjustmentRecord
    {
        double previousValue;
        double newValue;
        double metricBeforeChange; // низкий 1% low ƒќ применени€ новой поправки
        bool pendingEvaluation = true;
    };

    // ѕеред применением новой поправки Ч запомнить, откуда откатыватьс€
    static AdjustmentRecord beginAdjustment(double previousValue, double newValue, double currentMetric)
    {
        return { previousValue, newValue, currentMetric, true };
    }

    // ѕосле накоплени€ новых данных — новым порогом Ч решить, откатывать или нет
    static double evaluateAndMaybeRollback(const AdjustmentRecord& record, double newMetric,
        const std::wstring& gameName, const std::wstring& paramName)
    {
        // ≈сли новый порог дал результат ’”∆≈, чем было до изменени€ Ч откатываемс€
        if (newMetric < record.metricBeforeChange * 0.95) // допуск 5% на шум
        {
            std::wcout << L"[ROLLBACK] " << gameName << L"/" << paramName
                << L": new value " << record.newValue << L" performed worse ("
                << newMetric << L" < " << record.metricBeforeChange
                << L") -> reverting to " << record.previousValue << L"\n";
            return record.previousValue;
        }
        return record.newValue; // подтверждаем изменение
    }
};
struct GameProfile
{
    int consecutiveCpuAdjustments = 0;
    int consecutiveRamAdjustment = 0;
    int consecutiveGpuAdjustments = 0;
    double previousCpuOverloadOn = 0.0;
    double metricAtLastAdjustment = 0.0;
    double metricAtLastRamAdjustment = 0.0;
    double metricAtLastGpuAdjustment = 0.0;
    bool hasPendingEvaluation = false;
    bool hasPendingRamEvaluation = false;
    bool hasPendingGpuEvaluation = false;
    double previousRamLowOn = 0.0;
    double previousGpuOverloadOn = 0.0;
    std::wstring profileKey;
    std::wstring processName;       // "cs2.exe"
    std::wstring displayName;       // "Counter-Strike 2" (дл€ вывода/GUI позже)

    // ѕороги Ч либо кастомные (заданы вручную/подстроены), либо не заданы (используем hw + config как раньше)
    bool hasCustomCpuOn = false;
    double cpuOverloadOn = 0.0;

    bool hasCustomRamOn = false;
    double ramLowOn = 0.0;

    bool hasCustomGpuOn = false;
    double gpuOverloadOn = 0.0;

    bool hasCustomCpuOff = false;
    double CpuOverloadOff = 0.0;

    bool hasCustomRamOff = false;
    double ramLowOff = 0.0;

    bool hasCustomGpuOff = false;
    double gpuOverloadOff = 0.0;
    // —пецифичные дл€ игры особенности
    bool enableNetworkQos = false;   // одни игры (шутеры) выигрывают от QoS больше, другие нет
    double gpuHeavyThreshold = 10.0; // можно тюнить индивидуально под требовательность игры
};

class GameProfileStore
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

    static std::wstring getProfilesPath()
    {
        return getExeDirectory() + L"game_profiles.csv";
    }

public:
    // ¬строенные профили дл€ попул€рных игр Ч известные, разумные значени€
    // на основе того, что вы уже тестировали (CS2/CS:GO, Dota 2 и т.д.)
    static std::vector<GameProfile> getBuiltInProfiles()
    {
        std::vector<GameProfile> profiles;

        GameProfile cs2;
        cs2.processName = L"cs2.exe";
        cs2.displayName = L"Counter-Strike 2";
        cs2.enableNetworkQos = false; // разбирали: DSCP рискован без €вного тестировани€ сети
        cs2.gpuHeavyThreshold = 10.0;
        profiles.push_back(cs2);

        GameProfile dota2;
        dota2.processName = L"dota2.exe";
        dota2.displayName = L"Dota 2";
        dota2.enableNetworkQos = false;
        dota2.gpuHeavyThreshold = 10.0;
        profiles.push_back(dota2);

        GameProfile minecraft;
        minecraft.processName = L"javaw.exe"; // напомню Ч тут нужен доп. фильтр по пам€ти,
        // разбирали это в самом начале разговора
        minecraft.displayName = L"Minecraft";
        minecraft.enableNetworkQos = false;
        minecraft.gpuHeavyThreshold = 8.0;
        profiles.push_back(minecraft);

        return profiles;
    }

    // »щет профиль по имени процесса среди встроенных + пользовательских (сохранЄнных)
    static GameProfile findOrCreate(const std::wstring& processName)
    {
        // —начала провер€ем сохранЄнные пользовательские (могли быть подстроены AutoTuner'ом)
        auto saved = loadAll();
        for (auto& p : saved)
        {
            if (p.processName == processName)
                return p;
        }

        // «атем встроенные дефолты
        auto builtIn = getBuiltInProfiles();
        for (auto& p : builtIn)
        {
            if (p.processName == processName)
                return p;
        }

        // Ќичего не найдено Ч создаЄм пустой профиль с этим именем,
        // все пороги останутс€ "не заданы" (будет использован чистый HardwareProfile)
        GameProfile fallback;
        fallback.processName = processName;
        fallback.displayName = processName;
        return fallback;
    }

    static std::vector<GameProfile> loadAll()
    {
        std::vector<GameProfile> result;
        std::wifstream f(getProfilesPath());
        if (!f.is_open()) return result;
        std::wstring line;
        std::getline(f, line); // заголовок
        while (std::getline(f, line))
        {
            std::wstringstream ss(line);
            std::wstring token;
            std::vector<std::wstring> fields;
            while (std::getline(ss, token, L',')) fields.push_back(token);
            if (fields.size() < 27) continue; // было 17

            try
            {
                GameProfile p;

                p.profileKey = fields[0];
                p.processName = fields[1];
                p.displayName = fields[2];

                p.hasCustomCpuOn = (fields[3] == L"1");
                p.cpuOverloadOn = std::stod(fields[4]);

                p.hasCustomRamOn = (fields[5] == L"1");
                p.ramLowOn = std::stod(fields[6]);

                p.hasCustomGpuOn = (fields[7] == L"1");
                p.gpuOverloadOn = std::stod(fields[8]);

                p.hasCustomCpuOff = (fields[9] == L"1");
                p.CpuOverloadOff = std::stod(fields[10]);

                p.hasCustomRamOff = (fields[11] == L"1");
                p.ramLowOff = std::stod(fields[12]);

                p.hasCustomGpuOff = (fields[13] == L"1");
                p.gpuOverloadOff = std::stod(fields[14]);

                p.consecutiveCpuAdjustments = std::stoi(fields[15]);
                p.consecutiveRamAdjustment = std::stoi(fields[16]);
                p.consecutiveGpuAdjustments = std::stoi(fields[17]);

                // NEW: персистентный откат Ч CPU
                p.hasPendingEvaluation = (fields[18] == L"1");
                p.previousCpuOverloadOn = std::stod(fields[19]);
                p.metricAtLastAdjustment = std::stod(fields[20]);

                // NEW: персистентный откат Ч RAM
                p.hasPendingRamEvaluation = (fields[21] == L"1");
                p.previousRamLowOn = std::stod(fields[22]);
                p.metricAtLastRamAdjustment = std::stod(fields[23]);

                // NEW: персистентный откат Ч GPU
                p.hasPendingGpuEvaluation = (fields[24] == L"1");
                p.previousGpuOverloadOn = std::stod(fields[25]);
                p.metricAtLastGpuAdjustment = std::stod(fields[26]);

                result.push_back(p);
            }
            catch (...) {}
        }
        return result;
    }

    static void saveOrUpdate(const GameProfile& profile)
    {
        auto all = loadAll();
        bool found = false;
        for (auto& p : all)
        {
            if (p.profileKey == profile.profileKey)
            {
                p = profile;
                found = true;
                break;
            }
        }
        if (!found) all.push_back(profile);

        std::wofstream f(getProfilesPath(), std::ios::trunc);
        f << L"profileKey,processName,displayName,"
            L"hasCustomCpuOn,cpuOverloadOn,hasCustomRamOn,ramLowOn,hasCustomGpuOn,gpuOverloadOn,"
            L"hasCustomCpuOff,cpuOverloadOff,hasCustomRamOff,ramLowOff,hasCustomGpuOff,gpuOverloadOff,"
            L"consecutiveCpuAdjustments,consecutiveRamAdjustment,consecutiveGpuAdjustments,"
            L"hasPendingEvaluation,previousCpuOverloadOn,metricAtLastAdjustment,"
            L"hasPendingRamEvaluation,previousRamLowOn,metricAtLastRamAdjustment,"
            L"hasPendingGpuEvaluation,previousGpuOverloadOn,metricAtLastGpuAdjustment\n";

        for (auto& p : all)
        {
            f << p.profileKey<< L"," << p.processName << L"," << p.displayName << L","
                << (p.hasCustomCpuOn ? L"1" : L"0") << L"," << p.cpuOverloadOn << L","
                << (p.hasCustomRamOn ? L"1" : L"0") << L"," << p.ramLowOn << L","
                << (p.hasCustomGpuOn ? L"1" : L"0") << L"," << p.gpuOverloadOn << L","
                << (p.hasCustomCpuOff ? L"1" : L"0") << L"," << p.CpuOverloadOff << L","
                << (p.hasCustomRamOff ? L"1" : L"0") << L"," << p.ramLowOff << L","
                << (p.hasCustomGpuOff ? L"1" : L"0") << L"," << p.gpuOverloadOff << L","
                << p.consecutiveCpuAdjustments << L","
                << p.consecutiveRamAdjustment << L","
                << p.consecutiveGpuAdjustments << L","
                << (p.hasPendingEvaluation ? L"1" : L"0") << L","
                << p.previousCpuOverloadOn << L","
                << p.metricAtLastAdjustment << L","
                << (p.hasPendingRamEvaluation ? L"1" : L"0") << L","
                << p.previousRamLowOn << L","
                << p.metricAtLastRamAdjustment << L","
                << (p.hasPendingGpuEvaluation ? L"1" : L"0") << L","
                << p.previousGpuOverloadOn << L","
                << p.metricAtLastGpuAdjustment << L"\n";
        }
    }

    static bool exists(const std::wstring& processName)
    {
        auto saved = loadAll();
        for (auto& p : saved)
            if (p.processName == processName)
                return true;
        return false;
    }
};