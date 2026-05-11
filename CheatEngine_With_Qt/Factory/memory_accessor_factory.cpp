#include "Factory\memory_accessor_factory.h"
#include "Implement/Win_API/win32_memory_accessor.h"

std::unique_ptr<IMemoryAccessor> MemoryAccessorFactory::create(MemoryBackend type)
{
    switch (type) {
    case MemoryBackend::Win32:
        return std::make_unique<Win32MemoryAccessor>();
        // 未来在这里添加 DBK 驱动创建
        // case MemoryBackend::DbkDriver:
        //     return std::make_unique<DbkMemoryAccessor>();
    default:
        return nullptr;
    }
}