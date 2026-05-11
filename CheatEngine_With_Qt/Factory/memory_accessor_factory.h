#pragma once
#include "interface/imemory_accessor.h"
#include <memory>

enum class MemoryBackend
{
    Win32,
    //Add Dbk Driver
};

class MemoryAccessorFactory
{
public:
    static std::unique_ptr<IMemoryAccessor> create(MemoryBackend type);
};