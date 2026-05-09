#pragma once
#include "scan_data_stream_define.h"
#include "adaptive_cache.h"
#include "scan_snapshot_manager.h"
#include "scan_simd_accelerate.h"
#include "scan_result_repository.h"

#include <atomic>
#include <memory>
#include <vector>
#include <algorithm>
#include <cctype>


class ScanEngine {
public:


    struct ScanReport {
        std::shared_ptr<AdaptiveCachePool<ScanResult>> results; // 匹配结果
         //ScanMetadata metadata;                                   // 扫描元数据
		ScanDataType dataType;                                       // 数据类型（用于结果展示）
        std::shared_ptr<ScanSnapshot> firstSnapshot;            // 首次快照
        std::shared_ptr<ScanSnapshot> previousSnapshot;         // 上次快照
    };


    ScanEngine();
    ~ScanEngine() = default;

    // 唯一外部入口
    ScanReport execute(const ScanRequest& request, const std::vector<ScanResult>& prevResults);

    void cancel() { m_cancel.store(true, std::memory_order_release); }

    void clear() 
    {
        cancel();
        m_progress.store(0);
        m_totalItems.store(0);
        //m_snapshotMgr->clear();
	}

    bool isCancelled() const { return m_cancel.load(std::memory_order_acquire); }
    int progress() const { return m_progress.load(std::memory_order_relaxed); }
    int totalItems() const 
    {
        return m_totalItems.load();
    }


    std::shared_ptr<ScanSnapshot> getFirstSnapshot() const { return m_snapshotMgr->getFirst(); }
    std::shared_ptr<ScanSnapshot> getPreviousSnapshot() const { return m_snapshotMgr->getPrevious(); }

private:



    template <typename T>
    void dispatchScan(const ScanRequest& req, const std::vector<ScanResult>& prevResults,
        std::shared_ptr<AdaptiveCachePool<ScanResult>> outCache);

    template <typename T>
    void taskFirstScan(const ScanRequest& req, MemoryRegion region,
        std::shared_ptr<ScanSnapshot> current,
        std::shared_ptr<AdaptiveCachePool<ScanResult>> outCache);

    template <typename T>
    void taskNextScan(const ScanRequest& req, const std::vector<ScanResult>& oldBatch,
        std::shared_ptr<ScanSnapshot> current,
        std::shared_ptr<ScanSnapshot> previous,
        std::shared_ptr<AdaptiveCachePool<ScanResult>> outCache);

    // 特殊类型匹配算法

    void performStringSearch(const std::vector<uint8_t>& buf, uint64_t base, const StringParams& p, ScanDataType type, std::vector<uint64_t>& matched);
    void performAobSearch(const std::vector<uint8_t>& buf, uint64_t base, const AobParams& p, std::vector<uint64_t>& matched);

    mutable std::mutex m_statsMutex;
    std::atomic<bool> m_cancel{ false };
    std::atomic<int>  m_progress{ 0 };
    std::atomic<int>  m_totalItems{ 0 };

    std::atomic<int> m_potential_Address{ 0 };
    std::unique_ptr<SnapshotManager> m_snapshotMgr;
};