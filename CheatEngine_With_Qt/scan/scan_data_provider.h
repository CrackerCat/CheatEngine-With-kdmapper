// scan_data_provider.h
#pragma once
#include "value_provider_interface.h"
#include "scan_snapshot.h"
#include <memory>
#include <string>

class ScanDataProvider : public IValueProvider {
public:
	// 构造函数接收快照（可为 nullptr）和当前显示类型
	ScanDataProvider(std::shared_ptr<ScanSnapshot> firstSnapshot,
		std::shared_ptr<ScanSnapshot> prevSnapshot,
		ScanDataType displayType);

	// 更新快照（用于扫描完成后更新显示）
	void updateSnapshots(std::shared_ptr<ScanSnapshot> firstSnapshot,
		std::shared_ptr<ScanSnapshot> prevSnapshot);

	void setDisplayType(ScanDataType type) {
		m_displayType = type;
	}
	ScanDataType getDisplayType() { return m_displayType; }

	// IValueProvider 接口实现
	   // IValueProvider 接口实现
	std::string getCurrentValue(uint64_t address, ScanDataType type) const override;
	std::string getPreviousValue(uint64_t address, ScanDataType type) const override;
	std::string getFirstValue(uint64_t address, ScanDataType type) const override;
	std::string getAddressDisplay(uint64_t address) const override;
	bool isModuleBase(uint64_t address) const override;

private:
	std::string readValueFromSnapshot(uint64_t address, ScanDataType type,
		const std::shared_ptr<ScanSnapshot>& snapshot) const;
	std::string readCurrentFromMemory(uint64_t address, ScanDataType type) const;

	std::shared_ptr<ScanSnapshot> m_firstSnapshot;
	std::shared_ptr<ScanSnapshot> m_prevSnapshot;
	ScanDataType m_displayType;  // 用于格式化，但接口允许指定 type，优先使用传入 type
};