#include "scan_data_provider.h"
#include "scan_result_formatter.h"
#include "process_manager.h"
#include <vector>
#include <string>

ScanDataProvider::ScanDataProvider(std::shared_ptr<ScanSnapshot> first,
	std::shared_ptr<ScanSnapshot> prev,
	ScanDataType type)
	: m_firstSnapshot(first), m_prevSnapshot(prev), m_displayType(type) {
}

void ScanDataProvider::updateSnapshots(std::shared_ptr<ScanSnapshot> first,
	std::shared_ptr<ScanSnapshot> prev) {
	m_firstSnapshot = first;
	m_prevSnapshot = prev;
}

bool ScanDataProvider::isModuleBase(uint64_t address) const {
	std::string dummy;
	bool isBase = false;
	ProcessManager::instance().resolveAddress(address, dummy, isBase);
	return isBase;
}

std::string ScanDataProvider::getCurrentValue(uint64_t address, ScanDataType type) const {
	return readCurrentFromMemory(address, type);
}

std::string ScanDataProvider::getPreviousValue(uint64_t address, ScanDataType type) const {
	return readValueFromSnapshot(address, type, m_prevSnapshot);
}

std::string ScanDataProvider::getFirstValue(uint64_t address, ScanDataType type) const {
	return readValueFromSnapshot(address, type, m_firstSnapshot);
}

std::string ScanDataProvider::getAddressDisplay(uint64_t address) const {
	std::string display;
	bool isBase = false;
	ProcessManager::instance().resolveAddress(address, display, isBase);
	// 调用者可根据需要处理 isBase，这里只返回字符串
	return display;
}

std::string ScanDataProvider::readValueFromSnapshot(uint64_t address, ScanDataType type,
	const std::shared_ptr<ScanSnapshot>& snapshot) const {
	if (!snapshot) return "---";
	size_t size = scanDataTypeSize(type);
	if (size == 0) {
		// 字符串或字节数组
		const size_t maxRead = (type == ScanDataType::ByteArray) ? 32 : 64;
		std::vector<uint8_t> buf(maxRead);
		if (!snapshot->readData(address, buf.data(), buf.size()))
			return "---";
		if (isStringType(type)) {
			std::string str(reinterpret_cast<char*>(buf.data()), strnlen(reinterpret_cast<char*>(buf.data()), maxRead));
			return ScanResultFormatter::formatString(str, type);
		}
		else if (isByteArrayType(type)) {
			return ScanResultFormatter::formatByteArray(buf.data(), maxRead);
		}
		return "---";
	}
	uint64_t raw = 0;
	if (!snapshot->readData(address, reinterpret_cast<uint8_t*>(&raw), size))
		return "---";
	return ScanResultFormatter::formatValue(raw, type);
}

std::string ScanDataProvider::readCurrentFromMemory(uint64_t address, ScanDataType type) const {
	auto mem = ProcessManager::instance().memory();
	if (!mem) return "---";
	size_t size = scanDataTypeSize(type);
	if (size == 0) {
		const size_t maxRead = (type == ScanDataType::ByteArray) ? 32 : 64;
		std::vector<uint8_t> buf(maxRead);
		if (!mem->read(address, buf.data(), buf.size()))
			return "---";
		if (isStringType(type)) {
			std::string str(reinterpret_cast<char*>(buf.data()), strnlen(reinterpret_cast<char*>(buf.data()), maxRead));
			return ScanResultFormatter::formatString(str, type);
		}
		else if (isByteArrayType(type)) {
			return ScanResultFormatter::formatByteArray(buf.data(), maxRead);
		}
		return "---";
	}
	uint64_t raw = 0;
	if (!mem->read(address, &raw, size))
		return "---";
	return ScanResultFormatter::formatValue(raw, type);
}