#pragma once
#include "ProcessStruct.h"
#include <vector>

class ThreadPool;

class ProcessOptimizer
{
public:
    ProcessOptimizer(ThreadPool& pool);

    void optimize(std::vector<ProcessStruct>& processes);

private:
    ThreadPool& pool;

    void optimizeProcess(const ProcessStruct& p);
    int calculatePriority(const ProcessStruct& p);
    bool isGame(const ProcessStruct& p);
};