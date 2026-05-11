#include "Implement\Win_API\win32_memory_region_enumerator.h"
#include "process\process_manager.h"


std::vector<MemoryRegion> Win32MemoryRegionEnumerator::enumerate() {
    // 默认行为：调用带参版本，传入默认请求（扫描全部）
    ScanRequest defaultReq;
    defaultReq.onlyWritable = false;
    defaultReq.includeExecutable = true;
    return enumerate(defaultReq);
}

std::vector<MemoryRegion> Win32MemoryRegionEnumerator::enumerate(const ScanRequest& req) {
    std::vector<MemoryRegion> regions;
    uint32_t pid = ProcessManager::instance().attachedPid();
    if (pid == 0) return regions;

    // 获取进程句柄
    HANDLE hProcess = OpenProcess(PROCESS_QUERY_INFORMATION, FALSE, pid);
    if (!hProcess) return regions;

    MEMORY_BASIC_INFORMATION mbi;
    uint64_t addr = 0;

    // 如果请求指定了模块范围，设置搜索限制
    uint64_t limit = 0x7FFFFFFFFFFFFFFF; // 64位最大地址
    if (req.moduleBase != 0 && req.moduleSize != 0) {
        addr = req.moduleBase;
        limit = req.moduleBase + req.moduleSize;
    }

    // 遍历内存页
    while (addr < limit && VirtualQueryEx(hProcess, (LPCVOID)addr, &mbi, sizeof(mbi))) {
        bool keep = false;

        // 1. 必须是已提交的内存 (MEM_COMMIT)
        if (mbi.State == MEM_COMMIT) {
            // 2. 排除禁止访问和守护页
            if (!(mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD))) {

                bool writeAccess = isWritable(mbi.Protect);
                bool execAccess = isExecutable(mbi.Protect);

                // 3. 应用过滤逻辑
                keep = true;

                // 如果勾选“仅可写”，排除不可写页面
                if (req.onlyWritable && !writeAccess) {
                    keep = false;
                }

                // 如果未勾选“包含可执行”，排除纯执行页面（除非它是可写的）
                if (!req.includeExecutable && execAccess && !writeAccess) {
                    keep = false;
                }

                // 4. 内存类型过滤：CE 默认扫描 Private, Image (DLLs), 和 Mapped
                // 如果你想完全对齐 CE，通常保留这三种类型即可
                if (mbi.Type != MEM_PRIVATE && mbi.Type != MEM_IMAGE && mbi.Type != MEM_MAPPED) {
                    keep = false;
                }
            }
        }

        if (keep) {
            // 处理可能的截断（如果当前页超出了模块范围）
            uint64_t base = (uint64_t)mbi.BaseAddress;
            size_t size = mbi.RegionSize;

            if (base < addr) { // 处理起始地址在 mbi 范围内的情况
                size -= (addr - base);
                base = addr;
            }
            if (base + size > limit) {
                size = (size_t)(limit - base);
            }

            regions.push_back({ base, size });
        }

        // 移动到下一个区域
        uint64_t nextAddr = (uint64_t)mbi.BaseAddress + mbi.RegionSize;
        if (nextAddr <= addr) break; // 防止溢出死循环
        addr = nextAddr;
    }

    CloseHandle(hProcess);
    return regions;
}