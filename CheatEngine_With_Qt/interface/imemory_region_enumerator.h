#pragma once
#include <vector>
#include "type_define/memory_region.h"
#include "scan/scan_data_stream_define.h"

class IMemoryRegionEnumerator
{
public:
    virtual ~IMemoryRegionEnumerator() = default;
    virtual std::vector<MemoryRegion> enumerate() = 0;
    virtual std::vector<MemoryRegion> enumerate(const ScanRequest& req) = 0;
};