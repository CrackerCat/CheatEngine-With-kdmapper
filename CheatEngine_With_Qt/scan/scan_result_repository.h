#pragma once
#include "scan\scan_data_stream_define.h"
#include <vector>
#include <atomic>
#include <mutex>


struct ScanMetadata {
	int scannedRegions = 0;          // 扫描的内存区域数量
	int scannedAddresses = 0;        // 扫描的地址总数（对齐后）
	int matchedAddresses = 0;         // 匹配的地址数（即 results.size()）
	size_t totalBytes = 0;           // 扫描的总字节数
	ScanMode scanMode = ScanMode::First; // 本次扫描模式
	ScanType scanType = ScanType::ExactValue; // 首次扫描类型
	bool isFirstUnknownScan = false; // 是否为“首次-未知初始值”扫描
	bool isCompleted = false;        // 扫描是否正常完成
	// 可以加入时间戳、进程ID等其他信息
};

class ScanResultRepository {

public:
	/// 极速替换结果集：仅移动地址列表，无任何比对逻辑
	void replaceAllResults(std::vector<ScanResult>&& newResults);

	// ----- 数据查询 -----
	size_t getResultCount() const;

	const ScanResult* getResultAt(size_t index) const;

	uint64_t getAddressAtIndex(size_t index) const;

	int getCurrentGeneration() const;


	std::vector<ScanResult> getResults() const;

	void clear();

	void setScanMetadata(const ScanMetadata& meta);
	const ScanMetadata& getScanMetadata() const;
	void clearScanMetadata();

private:

	std::vector<ScanResult> m_result_data;
	mutable std::mutex m_mutex;
	std::atomic<int> m_generation{ 0 };

	ScanMetadata m_metadata; // 也需要用 mutex 保护，或与 m_data 同步更新
};