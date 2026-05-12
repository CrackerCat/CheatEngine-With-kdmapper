#pragma once
#include "interface\imemory_region_enumerator.h"
#include "scan/scan_data_stream_define.h"
#include <Windows.h>
#include <vector>

class Win32MemoryRegionEnumerator : public IMemoryRegionEnumerator
{
public:
    std::vector<MemoryRegion> enumerate() override;
	std::vector<MemoryRegion> enumerate(const ScanRequest& req) override;
private:
    inline bool isWritable(DWORD protect) {
        return (protect & (PAGE_READWRITE | PAGE_WRITECOPY | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY));
    }

    inline bool isExecutable(DWORD protect) {
        return (protect & (PAGE_EXECUTE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY));
    }

    inline bool isWriteCopy(DWORD protect) {
        return (protect & (PAGE_WRITECOPY | PAGE_EXECUTE_WRITECOPY));
    }

    inline bool isReadable(DWORD protect) {
        return (protect & (PAGE_READONLY | PAGE_READWRITE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_WRITECOPY | PAGE_EXECUTE_WRITECOPY));
    }

    // 将 MemoryFilter::ProtectFlag 转为 Windows PAGE_xxx 掩码
    inline DWORD flagsToWinProtect(uint32_t flags) {
        DWORD result = 0;
        if (flags & MemoryFilter::ProtectNoAccess)         result |= PAGE_NOACCESS;
        if (flags & MemoryFilter::ProtectReadOnly)          result |= PAGE_READONLY;
        if (flags & MemoryFilter::ProtectReadWrite)         result |= PAGE_READWRITE;
        if (flags & MemoryFilter::ProtectWriteCopy)         result |= PAGE_WRITECOPY;
        if (flags & MemoryFilter::ProtectExecute)           result |= PAGE_EXECUTE;
        if (flags & MemoryFilter::ProtectExecuteRead)       result |= PAGE_EXECUTE_READ;
        if (flags & MemoryFilter::ProtectExecuteReadWrite)  result |= PAGE_EXECUTE_READWRITE;
        if (flags & MemoryFilter::ProtectExecuteWriteCopy)  result |= PAGE_EXECUTE_WRITECOPY;
        if (flags & MemoryFilter::ProtectGuard)             result |= PAGE_GUARD;
        return result;
    }

    // 检查某页是否具有所有所需的保护属性
    inline bool hasRequiredProtect(DWORD pageProtect, uint32_t requiredFlags) {
        if (requiredFlags == 0) return true; // 无要求
        DWORD needed = flagsToWinProtect(requiredFlags);
        return (pageProtect & needed) == needed;
    }
};