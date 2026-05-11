#pragma once
#include "interface\imemory_region_enumerator.h"
#include <memory>
#include "Factory\memory_accessor_factory.h" 



class MemoryRegionEnumeratorFactory
{
public:
    static std::unique_ptr<IMemoryRegionEnumerator> create(MemoryBackend type);
};