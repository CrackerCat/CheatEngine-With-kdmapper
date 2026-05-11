#pragma once
#include "interface\iprocess_enumerator.h"
#include <memory>
#include "Factory\memory_accessor_factory.h"   // 复用 MemoryBackend

class ProcessEnumeratorFactory
{
public:
    static std::unique_ptr<IProcessEnumerator> create(MemoryBackend type);
};