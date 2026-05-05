#include "scan_result_repository.h"
#include "scan_result_formatter.h"
#include <algorithm>

void ScanResultRepository::replaceAllResults(std::vector<ScanResult>&& newResults)
{
    std::lock_guard<std::mutex> lock(m_mutex);

    // 【优化】由于 ScanResult 只剩地址，不再需要复杂的归并继承逻辑
    // 整个替换过程现在是 O(1) 的移动语义，彻底解决了大结果集下的 UI 卡顿
    m_data = std::move(newResults);

    m_snapshotCache.reset();
    m_snapshotGeneration = 0;
    m_generation.fetch_add(1, std::memory_order_release);
}

void ScanResultRepository::setSnapshotInfo(const std::string& firstPath,
    const std::map<uint64_t, size_t>& firstIdx,
    const std::string& prevPath,
    const std::map<uint64_t, size_t>& prevIdx)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_firstPath = firstPath;
    m_firstIndex = firstIdx;
    m_prevPath = prevPath;
    m_prevIndex = prevIdx;
}

std::string ScanResultRepository::getDisplayValue(uint64_t addr, int column, ScanDataType type) const
{
    // 该方法不需要加锁 m_mutex，因为快照路径和索引在扫描间期是只读的
    switch (column) {
    case 1: // Value: 实时读取目标进程内存
        return ScanResultFormatter::formatValueAt(addr, type); 

    case 2: // Previous: 从上次扫描的磁盘快照读取
        return ScanResultFormatter::formatValueFromSnapshot(addr, m_prevPath, m_prevIndex, type);

    case 3: // First: 从首次扫描的磁盘快照读取
        return ScanResultFormatter::formatValueFromSnapshot(addr, m_firstPath, m_firstIndex, type);

    default:
        return "";
    }
}

// ---------------- 以下逻辑保持精简 ----------------

size_t ScanResultRepository::resultCount() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_data.size();
}

const ScanResult* ScanResultRepository::resultAt(size_t index) const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return (index < m_data.size()) ? &m_data[index] : nullptr;
}

uint64_t ScanResultRepository::addressAtIndex(size_t index) const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return (index < m_data.size()) ? m_data[index].address : 0;
}

std::shared_ptr<const std::vector<ScanResult>> ScanResultRepository::createSnapshot() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    int gen = m_generation.load(std::memory_order_acquire);

    if (m_snapshotCache && m_snapshotGeneration == gen)
        return m_snapshotCache;

    m_snapshotCache = std::make_shared<const std::vector<ScanResult>>(m_data);
    m_snapshotGeneration = gen;
    return m_snapshotCache;
}

int ScanResultRepository::currentGeneration() const
{
    return m_generation.load();
}