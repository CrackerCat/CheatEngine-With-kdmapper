#pragma once
#include "interface\iprocess_memory_snapshot.h"
#include "Factory\memory_accessor_factory.h"
#include "type_define\memory_region.h"
#include <memory>
#include <vector>

class ProcessMemorySnapshotFactory {
public:
	// 专门用于创建一个“空壳”实现或基于现有文件的实现（如果需要）
	// 但通常快照是在 Manager 中由数据填充的，因此我们提供一个辅助创建方法
	static std::unique_ptr<IProcessMemorySnapshot> create(
		MemoryBackend type,
		const std::string& path,
		std::map<uint64_t, size_t> index
	);
};