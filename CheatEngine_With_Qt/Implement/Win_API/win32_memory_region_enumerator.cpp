#include "Implement\Win_API\win32_memory_region_enumerator.h"
#include "process\process_manager.h"


std::vector<MemoryRegion> Win32MemoryRegionEnumerator::enumerate() {
    ScanRequest defaultReq;
    // 默认：扫描全部已提交的可读私有/映像/映射内存（CE 标准行为）
    defaultReq.memFilter.stateFilter   = MemoryFilter::Commit;
    defaultReq.memFilter.typeFilter    = MemoryFilter::TypePrivate | MemoryFilter::TypeImage | MemoryFilter::TypeMapped;
    defaultReq.memFilter.accessFilter  = MemoryFilter::AccessRead | MemoryFilter::AccessWrite;
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
        const auto& mf = req.memFilter;
        // 下一块区域地址的快捷计算
        auto nextBlock = [&]() -> uint64_t {
            return (uint64_t)mbi.BaseAddress + mbi.RegionSize;
        };

        // ---- 阶段 1：状态 (State) 过滤 ----
        bool stateOk = false;
        if (mf.stateFilter & MemoryFilter::Commit)  stateOk = stateOk || (mbi.State == MEM_COMMIT);
        if (mf.stateFilter & MemoryFilter::Reserve) stateOk = stateOk || (mbi.State == MEM_RESERVE);
        if (mf.stateFilter & MemoryFilter::Free)    stateOk = stateOk || (mbi.State == MEM_FREE);
        if (!stateOk) { addr = nextBlock(); continue; }

        // ---- 阶段 2：类型 (Type) 过滤 ----
        bool typeOk = false;
        if (mf.typeFilter & MemoryFilter::TypePrivate) typeOk = typeOk || (mbi.Type == MEM_PRIVATE);
        if (mf.typeFilter & MemoryFilter::TypeImage)   typeOk = typeOk || (mbi.Type == MEM_IMAGE);
        if (mf.typeFilter & MemoryFilter::TypeMapped)  typeOk = typeOk || (mbi.Type == MEM_MAPPED);
        if (!typeOk) { addr = nextBlock(); continue; }

        // ---- 阶段 3：抽象访问权限 (Access) 过滤 ----
        bool canRead    = isReadable(mbi.Protect) && mbi.State == MEM_COMMIT;
        bool canWrite   = isWritable(mbi.Protect);
        bool canExecute = isExecutable(mbi.Protect);

        // 包容性过滤：勾选的属性中，只要满足任意一个就通过
        // Writable/Executable/CopyOnWrite 之间是 OR 关系
        bool passWritable    = !mf.Writable    || canWrite;                    // 不要求可写，或确实可写
        bool passExecutable  = !mf.Executable  || canExecute;                  // 不要求可执行，或确实可执行
        bool passCopyOnWrite = !mf.CopyOnWrite || isWriteCopy(mbi.Protect);    // 不要求COW，或确实是COW
        if (!passWritable && !passExecutable && !passCopyOnWrite) {
            addr = nextBlock(); continue;   // 三项都不满足才排除
        }

        // 位掩码访问检查（与快捷开关一起决定最终保留哪些页面）
        if (mf.accessFilter & MemoryFilter::AccessRead && !canRead)     { addr = nextBlock(); continue; }
        if (mf.accessFilter & MemoryFilter::AccessWrite && !canWrite)    { addr = nextBlock(); continue; }
        if (mf.accessFilter & MemoryFilter::AccessExecute && !canExecute){ addr = nextBlock(); continue; }

        // ---- 阶段 4：具体保护属性 (Protect) 过滤 ----
        if (mf.protectFilter != 0) {
            if (!hasRequiredProtect(mbi.Protect, mf.protectFilter)) {
                addr = nextBlock(); continue;
            }
        }

        // ---- 所有过滤条件通过，记录此区域 ----
        {
            uint64_t base = (uint64_t)mbi.BaseAddress;
            size_t size = mbi.RegionSize;

            if (base < addr) {
                size -= (size_t)(addr - base);
                base = addr;
            }
            if (base + size > limit) {
                size = (size_t)(limit - base);
            }

            regions.push_back({ base, size });
        }

        // 移动到下一个区域
        addr = nextBlock();
    }

    CloseHandle(hProcess);
    return regions;
}