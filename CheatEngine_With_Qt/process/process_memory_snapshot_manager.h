#pragma once
#include "iprocess_memory_snapshot.h"
#include "memory_accessor_factory.h"
#include "memory_region.h"
#include <memory>
#include <vector>


class ProcessMemorySnapshotManager {
public:

	explicit ProcessMemorySnapshotManager(MemoryBackend backend);
	// 创建新快照并返回实体
	std::shared_ptr<IProcessMemorySnapshot> createSnapshot(const std::vector<MemoryRegion>& regions);

	// 管理快照状态切换（First, Previous, Current）
	void setFirstSnapshot(std::shared_ptr<IProcessMemorySnapshot> snapshot) { m_first = snapshot; };
	void setPreviousSnapshot(std::shared_ptr<IProcessMemorySnapshot> snapshot) { m_prev = snapshot; };

	std::shared_ptr<IProcessMemorySnapshot> getFirstProcessMemeorySnapshot() const { return m_first; }
	std::shared_ptr<IProcessMemorySnapshot> getPreviousProcessMemeorySnapshot() const { return m_prev; }

	void clear(); // 处理临时文件的清理


private:
	std::shared_ptr<IProcessMemorySnapshot> m_first;
	std::shared_ptr<IProcessMemorySnapshot> m_prev;

	MemoryBackend  m_backend;
};