#include "Factory\module_enumerator_factory.h"
#include "Implement\Win_API\win32_module_enumerator.h"    // Win32 实现

std::unique_ptr<IModuleEnumerator> ModuleEnumeratorFactory::create(MemoryBackend type)
{
    switch (type)
    {
    case MemoryBackend::Win32:
        return std::make_unique<Win32ModuleEnumerator>();
        // 未来添加 DBK：
        // case MemoryBackend::DbkDriver:
        //     return std::make_unique<DbkModuleEnumerator>();
    default:
        return nullptr;
    }
}