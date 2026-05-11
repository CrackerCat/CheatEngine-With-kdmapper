#pragma once
#include "interface\iprocess_enumerator.h"
#include <vector>
#include <set>
#include "type_define\process_info.h"

class Win32ProcessEnumerator : public IProcessEnumerator
{
public:
    std::vector<ProcessInfo> enumerate() override;
private:
    static std::set<uint32_t> getWindowProcessIds();
};