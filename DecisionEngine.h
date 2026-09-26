#pragma once
#include <vector>
#include <functional>
#include <string>
#include "ProcessStruct.h"



struct DecisionContext
{
    bool systemOverloaded;
    bool ramLow;
    bool gpuOverloaded;
    bool vpnDetected;
    DWORD targetPID;
    DWORD myPid;
    DWORD gpuWorstPid;
    double gpuWorstUsage;
    double gpuHeavyThreshold;
    DWORD netWorstPid;
    double netWorstBytes;
    double netHeavyOn;
    double netHeavyOff;
    SIZE_T ramHeavyThresholdMB;
    bool recentlyStabilized;
};

struct Rule
{
    LowerReason reason;
    std::function<bool(const ProcessStruct&, const DecisionContext&, bool isCurrentlyLowered)> condition;
};

class DecisionEngine
{
private:
    std::vector<Rule> rules;

public:
    DecisionEngine()
    {
        // Порядок важен — первое совпавшее правило определяет reason в логе
        rules.push_back({
            LowerReason::CPU,
            [](const ProcessStruct&, const DecisionContext& ctx, bool) {
                return ctx.systemOverloaded;
            }
            });

        rules.push_back({
            LowerReason::GPU,
            [](const ProcessStruct& p, const DecisionContext& ctx, bool) {
                return ctx.gpuOverloaded &&
                       p.id == ctx.gpuWorstPid &&
                       ctx.gpuWorstUsage > ctx.gpuHeavyThreshold;
            }
            });

        rules.push_back({
            LowerReason::NET,
            [](const ProcessStruct& p, const DecisionContext& ctx, bool isCurrentlyLowered) {
                if (ctx.vpnDetected) return false; // защита от VPN остаётся здесь же
                double threshold = isCurrentlyLowered ? ctx.netHeavyOff : ctx.netHeavyOn;
                return p.id == ctx.netWorstPid && ctx.netWorstBytes > threshold;
            }
            });

        rules.push_back({
            LowerReason::RAM,
            [](const ProcessStruct& p, const DecisionContext& ctx, bool) {
                SIZE_T mb = p.memory / 1024 / 1024;
                return ctx.ramLow && mb > ctx.ramHeavyThresholdMB;
            }
            });
    }

    // Возвращает true, если хотя бы одно правило требует понижения,
    // и через outReason сообщает, какое именно (первое совпавшее)
    bool shouldLower(const ProcessStruct& p, const DecisionContext& ctx,
        bool isCurrentlyLowered, LowerReason& outReason) const
    {
        if (p.id == ctx.targetPID || p.id == ctx.myPid) return false;
        if (ctx.recentlyStabilized && !isCurrentlyLowered) return false; // гасящий фактор

        for (const auto& rule : rules)
        {
            if (rule.condition(p, ctx, isCurrentlyLowered))
            {
                outReason = rule.reason;
                return true;
            }
        }
        return false;
    }

    // Позволяет добавлять новые правила извне без правки этого класса
    void addRule(LowerReason reason, std::function<bool(const ProcessStruct&, const DecisionContext&, bool)> cond)
    {
        rules.push_back({ reason, cond });
    }
};