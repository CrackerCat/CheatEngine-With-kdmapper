#pragma once
#include "interface\imodule_enumerator.h"
#include <vector>
#include <cstdint>
#include "type_define\module_info.h"

// 原有的静态类现在改为接口实现，同时保留静态方法方便直接调用（兼容旧代码）
class Win32ModuleEnumerator : public IModuleEnumerator
{
public:
    std::vector<ModuleInfo> enumerate(uint32_t pid) override;
    // 保留静态接口供快速使用（内部调用上述实例方法）
    static std::vector<ModuleInfo> enumerateStatic(uint32_t pid);
};