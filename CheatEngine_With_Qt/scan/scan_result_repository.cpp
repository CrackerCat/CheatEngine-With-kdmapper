#include "scan_result_repository.h"
#include "utils\encoding_formatter.h"
#include "process\process_manager.h"
#include <algorithm>

void ScanResultRepository::replaceAllResults(std::vector<ScanResult>&& newResults) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_result_data = std::move(newResults); 
    m_generation.fetch_add(1, std::memory_order_release);
}

// ---------------- 以下逻辑保持精简 ----------------

size_t ScanResultRepository::getResultCount() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_result_data.size();
}

const ScanResult* ScanResultRepository::getResultAt(size_t index) const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return (index < m_result_data.size()) ? &m_result_data[index] : nullptr;
}

uint64_t ScanResultRepository::getAddressAtIndex(size_t index) const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return (index < m_result_data.size()) ? m_result_data[index].address : 0;
}

int ScanResultRepository::getCurrentGeneration() const
{
    return m_generation.load();
}


std::vector<ScanResult> ScanResultRepository::getResults() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_result_data; // 返回拷贝以确保线程安全
}

void ScanResultRepository::clear() {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_result_data.clear();
    m_generation.fetch_add(1, std::memory_order_release);
}
void ScanResultRepository::setScanMetadata(const ScanMetadata& meta) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_metadata = meta;
}
const ScanMetadata& ScanResultRepository::getScanMetadata() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_metadata;
}
void ScanResultRepository::clearScanMetadata() {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_metadata = ScanMetadata{};
}


