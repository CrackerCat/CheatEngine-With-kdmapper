#include "Filehandler.h"

BOOL ReadProcessMemoryFile(HANDLE hProcess, LPCVOID lpBaseAddress, LPVOID lpBuffer, DWORD nSize, uintptr_t* lpNumberOfBytesRead)
{
    return 0;
}

BOOL WriteProcessMemoryFile(HANDLE hProcess, LPCVOID lpBaseAddress, LPCVOID lpBuffer, DWORD nSize, uintptr_t* lpNumberOfBytesWritten)
{
    return 0;
}

DWORD VirtualQueryExFile(HANDLE hProcess, LPCVOID lpAddress, PMEMORY_BASIC_INFORMATION lpBuffer, uintptr_t dwLength)
{
    return 0;
}
