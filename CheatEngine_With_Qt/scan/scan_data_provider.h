// scan_data_provider.h
#pragma once
#include "interface\iscan_value_provider.h"
#include "process\process_memory_snapshot_manager.h"
#include <memory>
#include <string>
#include <functional>

class ScanDataProvider : public IScanValueProvider {
public:
	// 构造函数接收快照（可为 nullptr）和当前显示类型
	ScanDataProvider(ProcessMemorySnapshotManager* processSnapshotManager,
		ScanDataType displayType);

	void setDisplayType(ScanDataType type) { m_displayType = type; }
	ScanDataType getDisplayType() const { return m_displayType; }

	   // IScanValueProvider 接口实现
	std::string getCurrentValue(uint64_t address, ScanDataType type) const override;
	std::string getPreviousValue(uint64_t address, ScanDataType type) const override;
	std::string getFirstValue(uint64_t address, ScanDataType type) const override;
	std::string getAddressDisplay(uint64_t address) const override;
	bool isModuleBase(uint64_t address) const override;

	// ★ Hex 显示模式控制（仅整数类型生效）
	void setHexDisplay(bool on) override { m_hexDisplay = on; }
	bool isHexDisplay() const override { return m_hexDisplay; }

private:
    template<typename ReaderFunc>
	std::string readAndFormatGeneric(uint64_t address, ScanDataType type, ReaderFunc&& readFn) const;
	
	
	std::string readValueFromSnapshot(uint64_t address, ScanDataType type, const std::shared_ptr<IProcessMemorySnapshot>& snapshot) const;

	

	ProcessMemorySnapshotManager* m_processSnapshotManager; // 持有管理快照访问的指针（不拥有所有权）
	ScanDataType m_displayType;  // 用于格式化，但接口允许指定 type，优先使用传入 type
	bool m_hexDisplay = false;   // Hex 显示模式（仅整数类型生效）
};
