#pragma once
#include <vector>
#include <fstream>
#include <sstream>
#include <string>
#include <cmath>
#include <numeric>
#include <algorithm>
class StatValidator
{
public:
    struct TestResult
    {
        bool significant = false;
        double meanDiff = 0.0;
        double confidence = 0.0; // приблизительна€, 0..1
        size_t n = 0;
    };

    // Welch's t-test approximation Ч сравнивает две выборки 1% low FPS
    // baseline = idle/no-trigger sessions, intervention = with-trigger sessions
    static TestResult compare(const std::vector<double>& baseline, const std::vector<double>& intervention)
    {
        TestResult r;
        r.n = min(baseline.size(), intervention.size());

        // ћинимум сэмплов, чтобы вообще считать статистику осмысленной
        if (baseline.size() < 5 || intervention.size() < 5)
            return r; // significant остаЄтс€ false

        double meanB = std::accumulate(baseline.begin(), baseline.end(), 0.0) / baseline.size();
        double meanI = std::accumulate(intervention.begin(), intervention.end(), 0.0) / intervention.size();

        auto variance = [](const std::vector<double>& v, double mean)
            {
                double s = 0.0;
                for (double x : v) s += (x - mean) * (x - mean);
                return v.size() > 1 ? s / (v.size() - 1) : 0.0;
            };

        double varB = variance(baseline, meanB);
        double varI = variance(intervention, meanI);

        double se = std::sqrt(varB / baseline.size() + varI / intervention.size());
        if (se < 1e-9) return r;

        double tStat = (meanI - meanB) / se;
        r.meanDiff = meanI - meanB;

        // √руба€ аппроксимаци€: |t| > 2.0 ~ p < 0.05 при разумном n (эвристика, не точный t-test)
        r.confidence = min(1.0, std::abs(tStat) / 3.0);
        r.significant = std::abs(tStat) > 2.0;

        return r;
    }
};
class AutoTuner
{
public:
    struct HistoryEntry
    {
        double low1FpsCpu, low1FpsRam, low1FpsGpu, low1FpsIdle;
    };

    // „итает последние N записей истории дл€ данного процесса
    //static std::vector<HistoryEntry> readRecentHistory(const std::wstring& path,
    //    const std::wstring& processName, int maxEntries = 10)
    //{
    //    std::vector<HistoryEntry> result;
    //    std::wifstream f(path);
    //    if (!f.is_open()) return result;

    //    std::wstring line;
    //    std::getline(f, line); // пропускаем заголовок

    //    std::vector<std::wstring> lines;
    //    while (std::getline(f, line)) lines.push_back(line);

    //    // ЅерЄм последние maxEntries строк
    //    int start = std::max<int>(0, static_cast<int>(lines.size()) - maxEntries);
    //    for (int i = start; i < static_cast<int>(lines.size()); i++)
    //    {
    //        std::wstringstream ss(lines[i]);
    //        std::wstring token;
    //        std::vector<std::wstring> fields;
    //        while (std::getline(ss, token, L',')) fields.push_back(token);
    //        if (fields.size() < 6) continue;
    //        if (fields[1] != processName) continue;

    //        HistoryEntry e;
    //        try
    //        {
    //            e.avgFpsWith = std::stod(fields[2]);
    //            e.low1FpsWith = std::stod(fields[3]);
    //            e.avgFpsWithout = std::stod(fields[4]);
    //            e.low1FpsWithout = std::stod(fields[5]);
    //            result.push_back(e);
    //        }
    //        catch (...) {}
    //    }
    //    return result;
    //}
    static std::vector<HistoryEntry> readRecentHistory(const std::wstring& path,
        const std::wstring& processName, int maxEntries = 10)
    {
        std::vector<HistoryEntry> result;
        std::wifstream f(path);
        if (!f.is_open()) return result;

        std::wstring line;
        std::getline(f, line); // заголовок

        std::vector<std::wstring> lines;
        while (std::getline(f, line)) lines.push_back(line);

        int start = std::max<int>(0, static_cast<int>(lines.size()) - maxEntries);
        for (int i = start; i < static_cast<int>(lines.size()); i++)
        {
            std::wstringstream ss(lines[i]);
            std::wstring token;
            std::vector<std::wstring> fields;
            while (std::getline(ss, token, L',')) fields.push_back(token);
            if (fields.size() < 14) continue;
            if (fields[1] != processName) continue;

            try
            {
                HistoryEntry e;
                e.low1FpsCpu = std::stod(fields[3]);
                e.low1FpsRam = std::stod(fields[6]);
                e.low1FpsGpu = std::stod(fields[9]);
                e.low1FpsIdle = std::stod(fields[12]);
                result.push_back(e);
            }
            catch (...) {}
        }
        return result;
    }

    //static double suggestAdjustment(const std::vector<HistoryEntry>& history,
    //    double HistoryEntry::* metricField, bool higherIsWorse)
    //{
    //    // metricField указывает на low1FpsCpu/Ram/Gpu Ч сравниваем с low1FpsIdle
    //    if (history.size() < 3) return 0.0;

    //    int negativeCount = 0;
    //    for (auto& e : history)
    //    {
    //        if (e.*metricField < e.low1FpsIdle) negativeCount++; // хуже, чем при простое
    //    }
    //    double negativeRatio = static_cast<double>(negativeCount) / history.size();

    //    if (negativeRatio >= 0.7) return higherIsWorse ? -5.0 : 5.0;
    //    if (negativeRatio <= 0.2) return higherIsWorse ? 3.0 : -3.0;
    //    return 0.0;
    //}

    // ¬озвращает предложенную поправку к CPU_OVERLOAD_ON: +delta / -delta / 0
    // Ћогика ќ„≈Ќ№ консервативна€: только если систематически (>=70% записей)
    // эффект отрицателен или нулевой Ч немного повышаем порог (реже вмешиваемс€)
    //static double suggestCpuThresholdAdjustment(const std::vector<HistoryEntry>& history)
    //{
    //    if (history.size() < 3) return 0.0;

    //    int negativeCount = 0;
    //    for (auto& e : history)
    //    {
    //        if (e.low1FpsCpu < e.low1FpsIdle) // хуже, чем в состо€нии просто€
    //            negativeCount++;
    //    }
    //    double negativeRatio = static_cast<double>(negativeCount) / history.size();

    //    if (negativeRatio >= 0.7)
    //        return 5.0;   // систематически вредит -> поднимаем порог (реже срабатывать)
    //    else if (negativeRatio <= 0.2)
    //        return -3.0;  // систематически помогает -> снижаем порог (реагировать раньше)

    //    return 0.0;
    //}

    //static double suggestRamThresholdAdjustment(const std::vector<HistoryEntry>& history)
    //{
    //    if (history.size() < 3) return 0.0;

    //    int negativeCount = 0;
    //    for (auto& e : history)
    //    {
    //        if (e.low1FpsRam < e.low1FpsIdle)
    //            negativeCount++;
    //    }
    //    double negativeRatio = static_cast<double>(negativeCount) / history.size();

    //    // «нак ќЅ–ј“Ќџ… относительно CPU/GPU Ч RAM_LOW_ON это порог доли —¬ќЅќƒЌќ… пам€ти
    //    if (negativeRatio >= 0.7)
    //        return -0.05;  // систематически вредит -> снижаем порог (реже срабатывать)
    //    else if (negativeRatio <= 0.2)
    //        return 0.03;   // систематически помогает -> повышаем порог (реагировать раньше)

    //    return 0.0;
    //}

    //static double suggestGpuThresholdAdjustment(const std::vector<HistoryEntry>& history)
    //{
    //    if (history.size() < 3) return 0.0;

    //    int negativeCount = 0;
    //    for (auto& e : history)
    //    {
    //        if (e.low1FpsGpu < e.low1FpsIdle)
    //            negativeCount++;
    //    }
    //    double negativeRatio = static_cast<double>(negativeCount) / history.size();

    //    if (negativeRatio >= 0.7)
    //        return 5.0;   // систематически вредит -> поднимаем порог (реже срабатывать)
    //    else if (negativeRatio <= 0.2)
    //        return -3.0;  // систематически помогает -> снижаем порог (реагировать раньше)

    //    return 0.0;
    //}
    //static double suggestCpuThresholdAdjustment(const std::vector<HistoryEntry>& history)
    //{
    //    return suggestAdjustmentGeneric(history, &HistoryEntry::low1FpsCpu, 5.0, -3.0);
    //}
    //static double suggestRamThresholdAdjustment(const std::vector<HistoryEntry>& history)
    //{
    //    return suggestAdjustmentGeneric(history, &HistoryEntry::low1FpsRam, -0.05, 0.03);
    //}
    //static double suggestGpuThresholdAdjustment(const std::vector<HistoryEntry>& history)
    //{
    //    return suggestAdjustmentGeneric(history, &HistoryEntry::low1FpsGpu, 5.0, -3.0);
    //}
    //static double suggestAdjustmentGeneric(const std::vector<HistoryEntry>& history,
    //    double HistoryEntry::* metricField, double baseStepPositive, double baseStepNegative)
    //{
    //    if (history.size() < 5) return 0.0; // подн€ли минимум с 3 до 5

    //    std::vector<double> withMetric, idleMetric;
    //    for (auto& e : history)
    //    {
    //        withMetric.push_back(e.*metricField);
    //        idleMetric.push_back(e.low1FpsIdle);
    //    }

    //    auto result = StatValidator::compare(idleMetric, withMetric);
    //    if (!result.significant) return 0.0; // недостаточно уверенности Ч не трогаем

    //    return result.meanDiff < 0 ? baseStepPositive : baseStepNegative;
    //}
    //struct AdjustmentSuggestion { double value = 0.0; double confidence = 0.0; };
    //static AdjustmentSuggestion suggestAdjustmentGeneric(const std::vector<HistoryEntry>& history,
    //    double HistoryEntry::* metricField, double baseStepPositive, double baseStepNegative,
    //    int consecutiveSameDirection)
    //{
    //    AdjustmentSuggestion s;
    //    if (history.size() < 5) return s;

    //    std::vector<double> withMetric, idleMetric;
    //    for (auto& e : history) { withMetric.push_back(e.*metricField); idleMetric.push_back(e.low1FpsIdle); }

    //    auto result = StatValidator::compare(idleMetric, withMetric);
    //    if (!result.significant) return s;

    //    double baseStep = result.meanDiff < 0 ? baseStepPositive : baseStepNegative;
    //    s.value = adaptiveStep(baseStep, result.confidence, consecutiveSameDirection);
    //    s.confidence = result.confidence;
    //    return s;
    //}
  static double suggestCpuThresholdAdjustment(const std::vector<HistoryEntry>& history, int consecutiveSameDirection)
  {
    if (history.size() < 3) return 0.0;

    int negativeCount = 0;
    for (auto& e : history)
    {
        if (e.low1FpsCpu < e.low1FpsIdle)
            negativeCount++;
    }
    double negativeRatio = static_cast<double>(negativeCount) / history.size();

    double baseStep = 0.0;
    if (negativeRatio >= 0.7) baseStep = 5.0;
    else if (negativeRatio <= 0.2) baseStep = -3.0;
    else return 0.0;

    return adaptiveStep(baseStep, consecutiveSameDirection);
   }
  static double suggestRamThresholdAdjustment(const std::vector<HistoryEntry>& history, int consecutiveSameDirection)
  {
      if (history.size() < 3) return 0.0;
      int negativeCount = 0;
      for (auto& e : history)
      {
          if (e.low1FpsRam < e.low1FpsIdle)
              negativeCount++;
      }
      double negativeRatio = static_cast<double>(negativeCount) / history.size();

      double baseStep = 0.0;
      if (negativeRatio >= 0.7) baseStep = 5.0;
      else if (negativeRatio <= 0.2) baseStep = -3.0;
      else return 0.0;

      return adaptiveStep(baseStep, consecutiveSameDirection);
  }
  static double suggestGpuThresholdAdjustment(const std::vector<HistoryEntry>& history, int consecutiveSameDirection)
  {
      if (history.size() < 3) return 0.0;
      int negativeCount = 0;
      for (auto& e : history)
      {
          if (e.low1FpsGpu < e.low1FpsIdle)
              negativeCount++;
      }
      double negativeRatio = static_cast<double>(negativeCount) / history.size();

      double baseStep = 0.0;
      if (negativeRatio >= 0.7) baseStep = 5.0;
      else if (negativeRatio <= 0.2) baseStep = -3.0;
      else return 0.0;

      return adaptiveStep(baseStep, consecutiveSameDirection);
  }
  static double adaptiveStep(double baseStep, double consecutiveSameDirection)
  {
      double dampening = 1.0 / (1.0 + std::abs(consecutiveSameDirection) * 0.3);
      return baseStep * dampening;
  }
};