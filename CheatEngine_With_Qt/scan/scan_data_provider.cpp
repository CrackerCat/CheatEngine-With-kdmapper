#include "scan\scan_data_provider.h"
#include "utils\encoding_formatter.h"
#include "process\process_manager.h"

ScanDataProvider::ScanDataProvider(ProcessMemorySnapshotManager* processSnapshotManager, ScanDataType type)
    : m_displayType(type), m_processSnapshotManager(processSnapshotManager) {}

bool ScanDataProvider::isModuleBase(uint64_t address) const {
    std::string dummy; bool isBase = false;
    ProcessManager::instance().resolveAddress(address, dummy, isBase);
    return isBase;
}


// 抽象出的泛型私有核心读取函数
template<typename ReaderFunc>
std::string ScanDataProvider::readAndFormatGeneric(uint64_t address, ScanDataType type, ReaderFunc&& readFn) const {
	size_t size = scanDataTypeSize(type);
	
	// ── 1. 处理变长类型（字符串、字节数组，其 size 返回 0） ──
	if (size == 0) {
		const size_t maxRead = (type == ScanDataType::ByteArray) ? 32 : 64;
		std::vector<uint8_t> buf(maxRead);
		
		// 通过传入的闭包执行底层读取
		if (!readFn(address, buf.data(), buf.size())) 
			return "---";

		if (isStringType(type)) {
			if (type == ScanDataType::Utf16String) {
				// UTF-16 LE 文本转换机制
				const uint16_t* u16 = reinterpret_cast<const uint16_t*>(buf.data());
				size_t u16len = maxRead / 2;
				size_t realLen = 0;
				while (realLen < u16len && u16[realLen] != 0) ++realLen;
				return EncodingFormatter::formatUtf16String(u16, realLen);
			} else {
				// Ascii / Utf8 文本转换机制
				std::string str(reinterpret_cast<char*>(buf.data()), strnlen(reinterpret_cast<char*>(buf.data()), maxRead));
				return EncodingFormatter::formatString(str, type);
			}
		}
		else if (isByteArrayType(type)) {
			return EncodingFormatter::formatByteArray(buf.data(), maxRead);
		}
		return "---";
	}

	// ── 2. 处理定长基础数值类型（Int8 ~ Float64） ──
	uint64_t raw = 0;
	if (!readFn(address, reinterpret_cast<uint8_t*>(&raw), size))
		return "---";
		
	return EncodingFormatter::formatValue(raw, type, m_hexDisplay);
}

std::string ScanDataProvider::getCurrentValue(uint64_t address, ScanDataType type) const {
    return readAndFormatGeneric(address, type, [](uint64_t addr, uint8_t* dst, size_t sz) {
        auto mem = ProcessManager::instance().memory();
        return mem ? mem->read(addr, dst, sz) : false;
    });
}

std::string ScanDataProvider::readValueFromSnapshot(uint64_t address, ScanDataType type,
    const std::shared_ptr<IProcessMemorySnapshot>& snapshot) const 
{
    if (!snapshot) return "---";
    return readAndFormatGeneric(address, type, [&snapshot](uint64_t addr, uint8_t* dst, size_t sz) {
        return snapshot->readData(addr, dst, sz);
    });
}

std::string ScanDataProvider::getPreviousValue(uint64_t address, ScanDataType type) const {
    return readValueFromSnapshot(address, type, m_processSnapshotManager->getPreviousProcessMemeorySnapshot());
}

std::string ScanDataProvider::getFirstValue(uint64_t address, ScanDataType type) const {
    return readValueFromSnapshot(address, type, m_processSnapshotManager->getFirstProcessMemeorySnapshot());
}

std::string ScanDataProvider::getAddressDisplay(uint64_t address) const {
    std::string display; bool isBase = false;
    ProcessManager::instance().resolveAddress(address, display, isBase);
    return display;
}