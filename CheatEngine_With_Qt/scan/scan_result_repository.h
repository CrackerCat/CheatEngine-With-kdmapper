#pragma once
#include <vector>
#include <atomic>
#include <mutex>
#include <memory>
#include <map>
#include <string>
#include "scan_result.h"
#include "scan_types.h"

class ScanResultRepository {
public:
    // ----- 数据修改 -----

    /// 极速替换结果集：仅移动地址列表，无任何比对逻辑
    void replaceAllResults(std::vector<ScanResult>&& newResults);

    /// 设置快照信息：由 Service 在扫描完成后同步
    void setSnapshotInfo(const std::string& firstPath, const std::map<uint64_t, size_t>& firstIdx,
        const std::string& prevPath, const std::map<uint64_t, size_t>& prevIdx);

    // ----- 数据查询 -----

    [[nodiscard]] size_t resultCount() const;
    [[nodiscard]] const ScanResult* resultAt(size_t index) const;
    [[nodiscard]] uint64_t addressAtIndex(size_t index) const;

    /// 【新增核心接口】由 ViewModel 调用，实现懒加载数据显示
    /// 逻辑：Col 1 读内存，Col 2/3 读快照文件
    std::string getDisplayValue(uint64_t addr, int column, ScanDataType type) const;

    // ----- 快照与版本 -----

    [[nodiscard]] std::shared_ptr<const std::vector<ScanResult>> createSnapshot() const;
    [[nodiscard]] int currentGeneration() const;

private:
    mutable std::mutex m_mutex;
    std::vector<ScanResult> m_data; // 此时每个元素仅 8 字节
    std::atomic<int> m_generation{ 0 };

    mutable std::shared_ptr<const std::vector<ScanResult>> m_snapshotCache;
    mutable int m_snapshotGeneration{ 0 };

    // 磁盘快照路由信息
    std::string m_firstPath;
    std::string m_prevPath;
    std::map<uint64_t, size_t> m_firstIndex;
    std::map<uint64_t, size_t> m_prevIndex;
};