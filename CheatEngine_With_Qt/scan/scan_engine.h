#pragma once
#include "scan\scan_data_stream_define.h"
#include "utils\adaptive_cache.h"
#include "process\process_memory_snapshot_manager.h"
#include "scan\scan_simd_accelerate.h"
#include "scan\scan_result_repository.h"
#include "process\process_manager.h"

#include <atomic>
#include <memory>
#include <vector>
#include <algorithm>
#include <cctype>
#include <cwctype>   // 必须包含，用于 towlower


class ScanEngine {
public:

	struct ScanReport {
		std::shared_ptr<AdaptiveCachePool<ScanResult>> results; // 匹配结果
		//ScanMetadata metadata;                                   // 扫描元数据
		ScanDataType dataType;                                       // 数据类型（用于结果展示）
	};

	ScanEngine() = delete;
	ScanEngine(ProcessMemorySnapshotManager* processSnapshotManager);

	~ScanEngine() = default;

	// 唯一外部入口
	ScanReport execute(const ScanRequest& request, const std::vector<ScanResult>& prevResults);

	void cancel() { m_cancel.store(true, std::memory_order_release); }

	void clear()
	{
		cancel();
		m_progress.store(0);
		m_totalItems.store(0);
	}

	bool isCancelled() const { return m_cancel.load(std::memory_order_acquire); }
	int progress() const { return m_progress.load(std::memory_order_relaxed); }
	int totalItems() const
	{
		return m_totalItems.load();
	}


private:

	// 获取进程内存页列表，每页固定 4KB（或系统页面大小）
	inline	std::vector<std::pair<uint64_t, size_t>> getReadablePages() {
		std::vector<std::pair<uint64_t, size_t>> pages;
		auto regions = ProcessManager::instance().getMemoryRegions();
		const size_t pageSize = 0x1000; // 4KB

		for (const auto& reg : regions) {
			for (uint64_t addr = reg.base; addr < reg.base + reg.size; addr += pageSize) {
				size_t size = std::min(pageSize, reg.size - (addr - reg.base));
				pages.emplace_back(addr, size);
			}
		}
		return pages;
	}

	inline bool compareByteInsensitive(uint8_t a, uint8_t b) {
		return std::tolower(static_cast<int>(a)) == std::tolower(static_cast<int>(b));
	}

	// 辅助：UTF-16 不区分大小写比较（简化版，仅处理基础平面字符）
	inline bool compareUtf16Insensitive(uint16_t a, uint16_t b) {
		return std::towlower(a) == std::towlower(b);
	}

	template <typename T>
	void dispatchScan(const ScanRequest& req, const std::vector<ScanResult>& prevResults,
		std::shared_ptr<AdaptiveCachePool<ScanResult>> outCache);

	template <typename T>
	void taskFirstScan(const ScanRequest& req, MemoryRegion region,
		std::shared_ptr<AdaptiveCachePool<ScanResult>> outCache);

	template <typename T>
	void taskNextScan(const ScanRequest& req, 
		const std::vector<ScanResult>& oldBatch,
        std::shared_ptr<IProcessMemorySnapshot> currentSnapshot,
        std::shared_ptr<IProcessMemorySnapshot> previousSnapshot,
		std::shared_ptr<AdaptiveCachePool<ScanResult>> outCache);

	// 特殊类型匹配算法

	void performStringSearch(const std::vector<uint8_t>& buf, uint64_t base, const StringParams& p, ScanDataType type, std::vector<uint64_t>& matched);
	void performAobSearch(const std::vector<uint8_t>& buf, uint64_t base, const AobParams& p, std::vector<uint64_t>& matched);

	mutable std::mutex m_statsMutex;
	std::atomic<bool> m_cancel{ false };
	std::atomic<int>  m_progress{ 0 };
	std::atomic<int>  m_totalItems{ 0 };

	std::atomic<int> m_potential_Address{ 0 };
	ProcessMemorySnapshotManager* m_processSnapshotManager;
};