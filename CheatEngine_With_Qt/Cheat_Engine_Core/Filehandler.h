#pragma once


#ifndef FILEHANDLER_H
#define FILEHANDLER_H

//#include <minwindef.h>
//#include <Windows.h>


#include <string>
#include <wtypes.h>
//#include <sstream> // Add this include for std::stringstream

using namespace std;

BOOL ReadProcessMemoryFile(HANDLE hProcess, LPCVOID lpBaseAddress, LPVOID lpBuffer, DWORD nSize, uintptr_t* lpNumberOfBytesRead);
BOOL WriteProcessMemoryFile(HANDLE hProcess, LPCVOID lpBaseAddress, LPCVOID lpBuffer, DWORD nSize, uintptr_t* lpNumberOfBytesWritten);
DWORD VirtualQueryExFile(HANDLE hProcess, LPCVOID lpAddress, PMEMORY_BASIC_INFORMATION lpBuffer, uintptr_t dwLength);

class FileHandler
{
public:
    string filename;
    fstream* filedata; // Change TMemorystream to stringstream
    uintptr_t* filebaseaddress;

    
    
    FileHandler();
    ~FileHandler();
};

#endif // !FILEHANDLER_H