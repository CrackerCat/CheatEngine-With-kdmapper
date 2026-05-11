#pragma once
#include "interface\imodule_enumerator.h"
#include <memory>
#include "Factory\memory_accessor_factory.h"   // 复用 MemoryBackend 枚举

class ModuleEnumeratorFactory
{
public:
    static std::unique_ptr<IModuleEnumerator> create(MemoryBackend type);
};