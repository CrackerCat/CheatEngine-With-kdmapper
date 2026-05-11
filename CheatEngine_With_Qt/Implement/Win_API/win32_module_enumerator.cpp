#include "Implement/Win_API/win32_module_enumerator.h"
#include "utils/string_conversion.h"
#include <Windows.h>
#include <TlHelp32.h>

std::vector<ModuleInfo> Win32ModuleEnumerator::enumerate(uint32_t pid)
{
    std::vector<ModuleInfo> modules;
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, pid);
    if (snap == INVALID_HANDLE_VALUE)
        return modules;

    MODULEENTRY32 me;
    me.dwSize = sizeof(me);
    if (Module32First(snap, &me))
    {
        do
        {
            ModuleInfo info;
#ifdef UNICODE
            info.name = wstring_to_utf8(me.szModule);
#else
            info.name = me.szModule;
#endif
            info.base = (uint64_t)me.modBaseAddr;
            info.size = me.modBaseSize;
            modules.push_back(info);
        } while (Module32Next(snap, &me));
    }
    CloseHandle(snap);
    return modules;
}

std::vector<ModuleInfo> Win32ModuleEnumerator::enumerateStatic(uint32_t pid)
{
    Win32ModuleEnumerator enumerator;
    return enumerator.enumerate(pid);
}