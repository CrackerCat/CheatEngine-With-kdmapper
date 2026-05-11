#include "Factory/memory_region_enumerator_factory.h"
#include "Implement/Win_API/win32_memory_region_enumerator.h"
#include <memory>

std::unique_ptr<IMemoryRegionEnumerator> MemoryRegionEnumeratorFactory::create(MemoryBackend type)
{
    switch (type)
    {
    case MemoryBackend::Win32:
        return std::make_unique<Win32MemoryRegionEnumerator>();
        // 未来：case MemoryBackend::DbkDriver: ...
    default:
        return nullptr;
    }
}