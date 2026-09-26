#pragma once
#include <Windows.h>
#include <vector>
#include <tlhelp32.h>
#include <string>
#include "ProcessStruct.h"
class GetProcesors
{
public:
	std::vector<ProcessStruct> getAllProcesses();
};