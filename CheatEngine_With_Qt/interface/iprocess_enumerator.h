#pragma once
#include <vector>
#include "type_define\process_info.h"

class IProcessEnumerator {
public:
    virtual ~IProcessEnumerator() = default;
    virtual std::vector<ProcessInfo> enumerate() = 0;
};