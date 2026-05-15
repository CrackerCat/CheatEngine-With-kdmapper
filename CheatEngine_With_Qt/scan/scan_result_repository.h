#pragma once
#include "scan\scan_data_stream_define.h"
#include "utils\adaptive_cache.h"
#include <vector>
#include <atomic>
#include <mutex>
#include <memory>

struct ScanMetadata {
	int scannedRegions = 0;          // 扫描的内存区域数量
	int scannedAddresses = 0;        // 扫描的地址总数（对齐后）
	int matchedAddresses = 0;         // 匹配的地址数（即 results.size()）
	size_t totalBytes = 0;           // 扫描的总字节数
	ScanMode scanMode = ScanMode::First; // 本次扫描模式
	ScanType scanType = ScanType::ExactValue; // 首次扫描类型
	bool isFirstUnknownScan = false; // 是否为"首次-未知初始值"扫描
	bool isCompleted = false;        // 扫描是否正常完成
	// 可以加入时间戳、进程ID等其他信息
};

/// 保存上一次扫描的结果快照
struct PreviousScanSnapshot {
	std::vector<ScanResult> results;
};

/// @brief 内存阈值：当池中元素超过此值时，使用磁盘池模式
constexpr size_t POOL_MEMORY_THRESHOLD = 500'000;

class ScanResultRepository {

public:
	/// 极速替换结果集：仅移动地址列表，无任何比对逻辑
	void replaceAllResults(std::vector<ScanResult>&& newResults);

	/// 使用池对象替换结果集（磁盘后备，不全部加载到内存）
	void replaceAllResultsFromPool(std::shared_ptr<AdaptiveCachePool<ScanResult>> pool);

	// ----- 数据查询 -----
	size_t getResultCount() const;

	const ScanResult* getResultAt(size_t index) const;

	uint64_t getAddressAtIndex(size_t index) const;

	int getCurrentGeneration() const;

	/// 返回当前结果总数（无锁，用于 UI 线程估算）
	size_t getResultCountRelaxed() const;

	/// 读取池中的一段连续数据到 vector
	std::vector<ScanResult> readPoolChunk(size_t start, size_t count) const;

	/// 批量读取地址（用于 filter 构建，减少池模式下的单次读取开销）
	std::vector<uint64_t> readAddressRange(size_t start, size_t count) const;

	std::vector<ScanResult> getResults() const;

	void clear();

	void setScanMetadata(const ScanMetadata& meta);
	const ScanMetadata& getScanMetadata() const;
	void clearScanMetadata();

	// ===== "上一次扫描" 结果快照 =====

	/// 将当前结果保存为"上一次扫描"快照，并清空当前结果
	void saveAsPreviousResults();

	/// 是否有"上一次扫描"快照
	bool hasPreviousResults() const;

	/// 交换当前结果与"上一次扫描"快照（即恢复/撤销）
	/// 返回 true 表示快照非空且操作成功
	bool swapWithPrevious();

	/// 清除上一次扫描快照
	void clearPreviousResults();

private:

	// ---- 存储模式 ----
	// 模式1: 内存存储（常规扫描，结果量 < 阈值）
	std::vector<ScanResult> m_result_data;
	// 模式2: 池存储（海量结果，结果量 >= 阈值）
	std::shared_ptr<AdaptiveCachePool<ScanResult>> m_result_pool;

	mutable std::mutex m_mutex;
	std::atomic<int> m_generation{ 0 };

	ScanMetadata m_metadata;

	// "上一次扫描" 快照
	PreviousScanSnapshot m_previousScanResultSnapshot;
	mutable std::mutex m_prevMutex;
};
