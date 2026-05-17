#include "scan\scan_result_repository.h"
#include <cstring>

void ScanResultRepository::replaceAllResults(std::vector<ScanResult>&& newResults) {
    std::lock_guard<std::mutex> lock(m_mutex);
    // 如果之前是池模式，释放池
    if (m_result_pool) {
        m_result_pool->clear();
        m_result_pool.reset();
    }
    m_result_data = std::move(newResults);
    m_generation.fetch_add(1, std::memory_order_release);
}

void ScanResultRepository::replaceAllResultsFromPool(std::shared_ptr<AdaptiveCachePool<ScanResult>> pool) {
    std::lock_guard<std::mutex> lock(m_mutex);
    // 释放旧的池或内存数据
    if (m_result_pool && m_result_pool != pool) {
        m_result_pool->clear();
    }
    m_result_pool.reset();
    m_result_data.clear();
    m_result_data.shrink_to_fit();

    size_t totalCount = pool ? pool->total_size() : 0;
    if (totalCount <= POOL_MEMORY_THRESHOLD) {
        // ★ 结果量较小：直接加载到内存 vector，后续访问更高效
        if (totalCount > 0) {
            m_result_data = pool->readChunk(0, totalCount);
        }
        pool->clear(); // 已全部取出，释放临时文件
    } else {
        // ★ 结果量巨大：保留池引用，数据在磁盘上
        m_result_pool = std::move(pool);
    }
    m_generation.fetch_add(1, std::memory_order_release);
}

// ---------------- 数据查询 ----------------

size_t ScanResultRepository::getResultCount() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_result_pool) {
        return m_result_pool->total_size();
    }
    return m_result_data.size();
}

size_t ScanResultRepository::getResultCountRelaxed() const
{
    // 无锁版本，仅用于 UI 线程的大致估算
    if (m_result_pool) {
        return m_result_pool->total_size();
    }
    return m_result_data.size();
}

const ScanResult* ScanResultRepository::getResultAt(size_t index) const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_result_pool) {
        // 池模式下不支持直接返回指针（数据在磁盘上），返回 nullptr
        // 调用方应使用 readPoolChunk 批量读取
        return nullptr;
    }
    return (index < m_result_data.size()) ? &m_result_data[index] : nullptr;
}

uint64_t ScanResultRepository::getAddressAtIndex(size_t index) const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_result_pool) {
        // 从池中读取单个元素
        auto chunk = m_result_pool->readChunk(index, 1);
        if (!chunk.empty()) {
            return chunk[0].address;
        }
        return 0;
    }
    return (index < m_result_data.size()) ? m_result_data[index].address : 0;
}

ScanDataType ScanResultRepository::getMatchedTypeAtIndex(size_t index) const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_result_pool) {
        auto chunk = m_result_pool->readChunk(index, 1);
        if (!chunk.empty()) {
            return chunk[0].matchedType;
        }
        return ScanDataType::Int32; // fallback
    }
    return (index < m_result_data.size()) ? m_result_data[index].matchedType : ScanDataType::Int32;
}

int ScanResultRepository::getCurrentGeneration() const
{
    return m_generation.load();
}

std::vector<ScanResult> ScanResultRepository::readPoolChunk(size_t start, size_t count) const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_result_pool) {
        return m_result_pool->readChunk(start, count);
    }
    // 内存模式：直接从 vector 读取切片
    std::vector<ScanResult> chunk;
    if (start >= m_result_data.size()) return chunk;
    size_t end = std::min(start + count, m_result_data.size());
    chunk.assign(m_result_data.begin() + start, m_result_data.begin() + end);
    return chunk;
}

std::vector<uint64_t> ScanResultRepository::readAddressRange(size_t start, size_t count) const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    std::vector<uint64_t> addresses;
    if (m_result_pool) {
        auto chunk = m_result_pool->readChunk(start, count);
        addresses.reserve(chunk.size());
        for (const auto& r : chunk) {
            addresses.push_back(r.address);
        }
    } else {
        if (start >= m_result_data.size()) return addresses;
        size_t end = std::min(start + count, m_result_data.size());
        addresses.reserve(end - start);
        for (size_t i = start; i < end; ++i) {
            addresses.push_back(m_result_data[i].address);
        }
    }
    return addresses;
}

std::vector<ScanResult> ScanResultRepository::getResults() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_result_pool) {
        // ★ 把整个池读到内存（危险！但调用方需要全量结果时只能这样）
        //    此方法应尽可能避免使用，保留以兼容现有代码
        size_t total = m_result_pool->total_size();
        if (total > 10'000'000) {
            // 超过 1 千万条：返回空，防止撑爆内存
            return {};
        }
        return m_result_pool->readChunk(0, total);
    }
    return m_result_data; // 返回拷贝以确保线程安全
}

void ScanResultRepository::clear() {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_result_data.clear();
    if (m_result_pool) {
        m_result_pool->clear();
        m_result_pool.reset();
    }
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

// ===== "上一次扫描" 结果快照 =====

void ScanResultRepository::saveAsPreviousResults()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    std::lock_guard<std::mutex> prevLock(m_prevMutex);

    // 保存当前结果到快照
    if (m_result_pool) {
        // 池模式：从池中读取全部数据到快照（接受一次性的拷贝开销）
        size_t total = m_result_pool->total_size();
        if (total > 10'000'000) {
            // 海量数据不做快照（释放池）
            m_result_pool->clear();
            m_result_pool.reset();
            m_previousScanResultSnapshot.results.clear();
        } else {
            m_previousScanResultSnapshot.results = m_result_pool->readChunk(0, total);
            m_result_pool->clear();
            m_result_pool.reset();
        }
    } else {
        m_previousScanResultSnapshot.results = std::move(m_result_data);
    }
    m_result_data.clear();
    m_generation.fetch_add(1, std::memory_order_release);
}

bool ScanResultRepository::hasPreviousResults() const
{
    std::lock_guard<std::mutex> prevLock(m_prevMutex);
    return !m_previousScanResultSnapshot.results.empty();
}

bool ScanResultRepository::swapWithPrevious()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    std::lock_guard<std::mutex> prevLock(m_prevMutex);

    if (m_previousScanResultSnapshot.results.empty())
        return false;

    // 清除池模式（如果有）
    if (m_result_pool) {
        m_result_pool->clear();
        m_result_pool.reset();
    }

    // 交换当前结果与快照
    m_result_data.swap(m_previousScanResultSnapshot.results);
    m_generation.fetch_add(1, std::memory_order_release);
    return true;
}

void ScanResultRepository::clearPreviousResults()
{
    std::lock_guard<std::mutex> prevLock(m_prevMutex);
    m_previousScanResultSnapshot.results.clear();
}
