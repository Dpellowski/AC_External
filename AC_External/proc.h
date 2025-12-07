#pragma once
#include <windows.h> // Ensure this header is included for Windows API functions  
#include <TlHelp32.h> // Ensure this header is included for CreateToolhelp32Snapshot and related structures  
#include <vector>

DWORD GetProcId(const wchar_t* procName);

uintptr_t GetModuleBaseAddress(DWORD procId, const wchar_t* modName);

uintptr_t FindDMAAddy(HANDLE hProc, uintptr_t ptr, std::vector<unsigned int> offsets);