#include "Implement/Win_API/win32_process_enumerator.h"
#include "utils/string_conversion.h"
#include <Windows.h>
#include <TlHelp32.h>
#include <Psapi.h>
#include <map>

std::string getProcessPath(DWORD pid)
{
    HANDLE hProcess = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, pid);

    if (!hProcess)
        return "";

    char buffer[MAX_PATH] = { 0 };

    if (GetModuleFileNameExA(hProcess, NULL, buffer, MAX_PATH))
    {
        CloseHandle(hProcess);
        return std::string(buffer);
    }

    CloseHandle(hProcess);
    return "";
}


static BOOL CALLBACK EnumWindowsProc(HWND hwnd, LPARAM lParam)
{
    if (!IsWindowVisible(hwnd))
        return TRUE;

    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);

    if (pid != 0)
    {
        auto* set = reinterpret_cast<std::set<uint32_t>*>(lParam);
        set->insert(pid);
    }

    return TRUE;
}

std::set<uint32_t> Win32ProcessEnumerator::getWindowProcessIds()
{
    std::set<uint32_t> pids;

    EnumWindows(EnumWindowsProc, (LPARAM)&pids);

    return pids;
}


std::vector<ProcessInfo> Win32ProcessEnumerator::enumerate()
{
    std::vector<ProcessInfo> list;

    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);



    auto windowPids = Win32ProcessEnumerator::getWindowProcessIds();



    std::map<uint32_t, ProcessInfo> processMap;

    if (snap == INVALID_HANDLE_VALUE)
        return list;

    PROCESSENTRY32 pe;
    pe.dwSize = sizeof(pe);

    if (Process32First(snap, &pe))
    {
        do
        {
            ProcessInfo info;
            info.pid = pe.th32ProcessID;
#ifdef UNICODE
            info.name = wstring_to_utf8(pe.szExeFile);
#else
            info.name = std::string(pe.szExeFile);
#endif
            processMap[info.pid] = info;

            

        } while (Process32Next(snap, &pe));
    }

    for (auto& [pid, info] : processMap)
    {
        if (windowPids.count(pid))
        {
            info.hasWindow = true;

            // ⭐ 只在这里获取路径（避免重复）
            info.exePath = getProcessPath(pid);
        }
    }

    for (auto& [pid, info] : processMap)
    {
        //if (info.hasWindow) // 只保留有窗口
            list.push_back(info);
    }

    CloseHandle(snap);
    return list;
}