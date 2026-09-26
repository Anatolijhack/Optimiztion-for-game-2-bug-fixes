#pragma once
#include <Windows.h>
#include <Psapi.h>
class GetProcesMemory
{
public:
	GetProcesMemory() {};
	SIZE_T getProcessMemory(DWORD pid);
	void getSystemMemory(SIZE_T& total, SIZE_T& free, SIZE_T& used);
};