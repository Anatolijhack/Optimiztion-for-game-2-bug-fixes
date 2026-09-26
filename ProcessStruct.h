#pragma once
#include <Windows.h>
#include <string>
struct ProcessStruct
{
	DWORD id; 
	SIZE_T memory;
	double CPUusage;
	std::wstring name;
};

enum class LowerReason { CPU, RAM, GPU, NET };