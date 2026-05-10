#pragma once
#include <string>
#include "scan_data_stream_define.h"

class IScanValueProvider {
public:
	virtual ~IScanValueProvider() = default;

	// 获取当前内存中的值（实时），返回格式化后的字符串（UTF-8）
	virtual std::string getCurrentValue(uint64_t address, ScanDataType type) const = 0;

	// 获取上一次扫描快照中的值
	virtual std::string getPreviousValue(uint64_t address, ScanDataType type) const = 0;

	// 获取首次扫描快照中的值
	virtual std::string getFirstValue(uint64_t address, ScanDataType type) const = 0;

    // 获取地址的显示字符串（带模块名等，可选）
	virtual std::string getAddressDisplay(uint64_t address) const = 0;

	virtual bool isModuleBase(uint64_t address) const = 0;
};