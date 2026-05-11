#include "Factory\process_enumerator_factory.h"
#include "Implement\Win_API\win32_process_enumerator.h"   // Win32 实现

std::unique_ptr<IProcessEnumerator> ProcessEnumeratorFactory::create(MemoryBackend type)
{
    switch (type)
    {
    case MemoryBackend::Win32:
        return std::make_unique<Win32ProcessEnumerator>();
        // 未来：case MemoryBackend::DbkDriver: ...
    default:
        return nullptr;
    }
}