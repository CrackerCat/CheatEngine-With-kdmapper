#include "scan\scan_engine.h"
#include "process\process_manager.h"
#include "utils\thread_pool.h"
#include <cstring>

// =============================================================================
// All 扫描辅助：整数类型比较
// =============================================================================
template<typename IntT>
static inline bool compareIntValue(IntT val, IntT v1, IntT v2, ScanType st, bool notMatch) {
    bool match = false;
    switch (st) {
    case ScanType::ExactValue:  match = (val == v1); break;
    case ScanType::GreaterThan: match = (val >  v1); break;
    case ScanType::LessThan:    match = (val <  v1); break;
    case ScanType::Between:     match = (val >= v1 && val <= v2); break;
    default: break;
    }
    return notMatch ? !match : match;
}

template<typename IntT>
static inline bool compareIntNextValue(IntT cur, IntT old, IntT v1, IntT v2,
    NextScanType nt, bool notMatch)
{
    bool match = false;
    switch (nt) {
    case NextScanType::Equal:     match = (cur == v1); break;
    case NextScanType::NotEqual:  match = (cur != v1); break;
    case NextScanType::Increased:   match = (cur >  old); break;
    case NextScanType::Decreased:   match = (cur <  old); break;
    case NextScanType::Changed:     match = (cur != old); break;
    case NextScanType::Unchanged:   match = (cur == old); break;
    case NextScanType::Between:     match = (cur >= v1 && cur <= v2); break;
    case NextScanType::IncreasedBy: match = (cur >  old + v1); break;
    case NextScanType::DecreasedBy: match = (cur <  old - v1); break;
    case NextScanType::Compare_to_First_Scan: match = (cur == old); break;
    default: break;
    }
    return notMatch ? !match : match;
}

// All 扫描辅助：Float32 比较（首次扫描）
static inline bool compareFloatFirst(float val, float v1, float v2, ScanType st,
    bool useApprox, bool notMatch)
{
    bool match = false;
    if (useApprox && st == ScanType::ExactValue) {
        match = (val >= v1 && val <= v2);
    } else {
        switch (st) {
        case ScanType::ExactValue:  match = (val == v1); break;
        case ScanType::GreaterThan: match = (val >  v1); break;
        case ScanType::LessThan:    match = (val <  v1); break;
        case ScanType::Between:     match = (val >= v1 && val <= v2); break;
        default: break;
        }
    }
    return notMatch ? !match : match;
}

// All 扫描辅助：Float64 比较（首次扫描）
static inline bool compareDoubleFirst(double val, double v1, double v2, ScanType st,
    bool useApprox, bool notMatch)
{
    bool match = false;
    if (useApprox && st == ScanType::ExactValue) {
        match = (val >= v1 && val <= v2);
    } else {
        switch (st) {
        case ScanType::ExactValue:  match = (val == v1); break;
        case ScanType::GreaterThan: match = (val >  v1); break;
        case ScanType::LessThan:    match = (val <  v1); break;
        case ScanType::Between:     match = (val >= v1 && val <= v2); break;
        default: break;
        }
    }
    return notMatch ? !match : match;
}

// All 扫描辅助：Float32 比较（再次扫描）
static inline bool compareFloatNext(float cur, float old, float v1, float v2,
    NextScanType nt, bool useApprox, bool notMatch)
{
    bool match = false;
    if (useApprox && (nt == NextScanType::Equal || nt == NextScanType::NotEqual)) {
        if (nt == NextScanType::Equal)  match = (cur >= v1 && cur <= v2);
        else                            match = (cur <  v1 || cur >  v2);
    } else {
        switch (nt) {
        case NextScanType::Equal:     match = (cur == v1); break;
        case NextScanType::NotEqual:  match = (cur != v1); break;
        case NextScanType::Increased:   match = (cur >  old); break;
        case NextScanType::Decreased:   match = (cur <  old); break;
        case NextScanType::Changed:     match = (cur != old); break;
        case NextScanType::Unchanged:   match = (cur == old); break;
        case NextScanType::Between:     match = (cur >= v1 && cur <= v2); break;
        case NextScanType::IncreasedBy: match = (cur >  old + v1); break;
        case NextScanType::DecreasedBy: match = (cur <  old - v1); break;
        case NextScanType::Compare_to_First_Scan: match = (cur == old); break;
        default: break;
        }
    }
    return notMatch ? !match : match;
}

// All 扫描辅助：Float64 比较（再次扫描）
static inline bool compareDoubleNext(double cur, double old, double v1, double v2,
    NextScanType nt, bool useApprox, bool notMatch)
{
    bool match = false;
    if (useApprox && (nt == NextScanType::Equal || nt == NextScanType::NotEqual)) {
        if (nt == NextScanType::Equal)  match = (cur >= v1 && cur <= v2);
        else                            match = (cur <  v1 || cur >  v2);
    } else {
        switch (nt) {
        case NextScanType::Equal:     match = (cur == v1); break;
        case NextScanType::NotEqual:  match = (cur != v1); break;
        case NextScanType::Increased:   match = (cur >  old); break;
        case NextScanType::Decreased:   match = (cur <  old); break;
        case NextScanType::Changed:     match = (cur != old); break;
        case NextScanType::Unchanged:   match = (cur == old); break;
        case NextScanType::Between:     match = (cur >= v1 && cur <= v2); break;
        case NextScanType::IncreasedBy: match = (cur >  old + v1); break;
        case NextScanType::DecreasedBy: match = (cur <  old - v1); break;
        case NextScanType::Compare_to_First_Scan: match = (cur == old); break;
        default: break;
        }
    }
    return notMatch ? !match : match;
}

// =============================================================================
// All 扫描类型定义表（CE 官方顺序：Byte→Int16→Int32→Int64→Float→Double）
// =============================================================================
struct AllTypeEntry {
    ScanDataType type;
    size_t size;
    size_t alignment;
};
static constexpr AllTypeEntry kAllTypes[] = {
    { ScanDataType::Int8,    1, 1 },
    { ScanDataType::Int16,   2, 2 },
    { ScanDataType::Int32,   4, 4 },
    { ScanDataType::Int64,   8, 8 },
    { ScanDataType::Float32, 4, 4 },
    { ScanDataType::Float64, 8, 8 },
};
static constexpr int kAllNumTypes = 6;

// =============================================================================
// 判断某个 NextScanType 是否需要用到历史快照中的旧值
// =============================================================================
static bool needOldValueForNextScan(NextScanType nt) {
    switch (nt) {
    case NextScanType::Increased:
    case NextScanType::Decreased:
    case NextScanType::Changed:
    case NextScanType::Unchanged:
    case NextScanType::IncreasedBy:
    case NextScanType::DecreasedBy:
    case NextScanType::Compare_to_First_Scan:
        return true;
    default:
        return false;
    }
}


ScanEngine::ScanEngine(ProcessMemorySnapshotManager* processSnapshotManager):
	m_processSnapshotManager(processSnapshotManager) 
{

}

ScanEngine::ScanReport ScanEngine::execute(const ScanRequest& request, const std::vector<ScanResult>& prevResults) {
	m_cancel.store(false);
	m_progress.store(0);
	auto results = std::make_shared<AdaptiveCachePool<ScanResult>>(THEAD_LOCAL_SIZE);

	// 适配全部数据类型
	switch (request.dataType) {
	case ScanDataType::Bit:     dispatchScan<uint8_t>(request, prevResults, results); break;
	case ScanDataType::Int8:    dispatchScan<int8_t>(request, prevResults, results); break;
	case ScanDataType::Int16:   dispatchScan<int16_t>(request, prevResults, results); break;
	case ScanDataType::Int32:   dispatchScan<int32_t>(request, prevResults, results); break;
	case ScanDataType::Int64:   dispatchScan<int64_t>(request, prevResults, results); break;
	case ScanDataType::Float32: dispatchScan<float>(request, prevResults, results); break;
	case ScanDataType::Float64: dispatchScan<double>(request, prevResults, results); break;
	case ScanDataType::AsciiString:
	case ScanDataType::Utf8String:
	case ScanDataType::Utf16String:
	case ScanDataType::ByteArray:
		dispatchScan<uint8_t>(request, prevResults, results); break;
	case ScanDataType::All:
		dispatchAllScan(request, prevResults, results);
		break;
	case ScanDataType::Structure: break;
	}
	return { results, request.dataType};
}

// =============================================================================
// dispatchAllScan — All 扫描调度入口
// =============================================================================
void ScanEngine::dispatchAllScan(const ScanRequest& request,
	const std::vector<ScanResult>& prevResults,
	std::shared_ptr<AdaptiveCachePool<ScanResult>> outCache)
{
	auto regions = ProcessManager::instance().getMemoryRegions(request);
	auto currentSnap = std::shared_ptr<IProcessMemorySnapshot>(m_processSnapshotManager->createSnapshot(regions));
	auto prevSnap = m_processSnapshotManager->getPreviousProcessMemeorySnapshot();

	std::vector<std::future<void>> futures;

	if (request.mode == ScanMode::First) {
		if (request.firstType == ScanType::UnknownInitial) {
			// All + UnknownInitial：按 1 字节对齐计数所有地址
			m_totalItems.store(static_cast<int>(regions.size()));
			m_potential_Address.store(0);
			for (const auto& region : regions) {
				size_t count = region.size / 1;
				m_potential_Address.fetch_add(static_cast<int>(count), std::memory_order_relaxed);
				m_progress.fetch_add(1);
			}
			m_processSnapshotManager->setFirstSnapshot(currentSnap);
			m_processSnapshotManager->setPreviousSnapshot(currentSnap);
		} else {
			m_totalItems.store(static_cast<int>(regions.size()));
			for (const auto& region : regions) {
				futures.push_back(GlobalThreadPool::instance().enqueue(
					[this, request, region, currentSnap, outCache] {
						taskFirstScanAll(request, region, currentSnap, outCache);
					}));
			}
			m_processSnapshotManager->setFirstSnapshot(currentSnap);
		}
	} else {
		// ── UnknownInitial 之后的再次扫描 ──
		if (prevResults.empty() && m_potential_Address.load() > 0) {
			m_totalItems.store(static_cast<int>(regions.size()));
			m_potential_Address.store(0);
			for (const auto& region : regions) {
				futures.push_back(GlobalThreadPool::instance().enqueue(
					[this, request, region, currentSnap, prevSnap, outCache] {
						taskFullScanWithNextConditionAll(request, region, currentSnap, prevSnap, outCache);
					}));
			}
		} else {
			m_totalItems.store(static_cast<int>(prevResults.size()));
			const size_t batchSize = 4096;
			for (size_t i = 0; i < prevResults.size(); i += batchSize) {
				std::vector<ScanResult> batch;
				size_t end = (std::min)(i + batchSize, prevResults.size());
				batch.assign(prevResults.begin() + i, prevResults.begin() + end);
				futures.push_back(GlobalThreadPool::instance().enqueue(
					[this, request, batch, currentSnap, prevSnap, outCache] {
						taskNextScanAll(request, batch, currentSnap, prevSnap, outCache);
					}));
			}
		}
	}

	for (auto& fut : futures) {
		if (fut.valid()) fut.get();
	}
	m_processSnapshotManager->setPreviousSnapshot(currentSnap);
}

// =============================================================================
// taskFirstScanAll — All 首次扫描：对每个对齐地址逐类型尝试
// =============================================================================
void ScanEngine::taskFirstScanAll(const ScanRequest& request, MemoryRegion region,
	std::shared_ptr<IProcessMemorySnapshot> currentSnap,
	std::shared_ptr<AdaptiveCachePool<ScanResult>> outCache)
{
	if (m_cancel.load()) return;
	if (region.size == 0) { m_progress.fetch_add(1); return; }

	// 解析参数
	auto* p = std::get_if<ValueParams>(&request.params);
	// 对于每个类型分别持有 v1/v2（按位存储，浮点数转成对应位模式）
	uint64_t typeV1[kAllNumTypes] = {0};
	uint64_t typeV2[kAllNumTypes] = {0};

	if (p) {
		for (int ti = 0; ti < kAllNumTypes; ++ti) {
			typeV1[ti] = p->value1;
			typeV2[ti] = p->value2;
		}
		// 浮点数特殊处理：Float32 和 Float64 的近似值
		if (request.containApproximateValue && request.firstType == ScanType::ExactValue) {
			// Float32 (index 4)
			{
				float target;
				std::memcpy(&target, &p->value1, sizeof(float));
				constexpr float relEps = 0.01f;
				float lo = target * (1.0f - relEps);
				float hi = target * (1.0f + relEps);
				float absMin = 0.0001f;
				if (target >= 0 && lo < -absMin) lo = 0.0f;
				if (hi - lo < absMin) { lo = target - absMin; hi = target + absMin; }
				std::memcpy(&typeV1[4], &lo, sizeof(float));
				std::memcpy(&typeV2[4], &hi, sizeof(float));
			}
			// Float64 (index 5)
			{
				double target;
				std::memcpy(&target, &p->value1, sizeof(double));
				constexpr double relEps = 0.01;
				double lo = target * (1.0 - relEps);
				double hi = target * (1.0 + relEps);
				double absMin = 0.0001;
				if (target >= 0 && lo < -absMin) lo = 0.0;
				if (hi - lo < absMin) { lo = target - absMin; hi = target + absMin; }
				std::memcpy(&typeV1[5], &lo, sizeof(double));
				std::memcpy(&typeV2[5], &hi, sizeof(double));
			}
		}
	}

	// 以 1 字节为基本对齐步长（All 扫描最细粒度）
	const size_t chunkSize = 64 * 1024;
	// 需要读最大 8 字节来覆盖所有类型
	const size_t maxReadSize = 8;
	std::vector<uint8_t> memBuf(chunkSize + maxReadSize);
	std::vector<ScanResult> batchResults;
	batchResults.reserve(2048);

	for (size_t baseOffset = 0; baseOffset < region.size && !m_cancel.load(); baseOffset += chunkSize) {
		size_t toRead = (std::min)(chunkSize, region.size - baseOffset);
		uint64_t chunkBase = region.base + baseOffset;
		if (!currentSnap->readData(chunkBase, memBuf.data(), toRead)) continue;

		// 对 chunk 中每个字节偏移，逐一尝试所有类型
		// 注意：Byte 读 1 字节（偏移 0），Int16 读 2 字节（偏移 0 或 1? — Int16 需 2 对齐）
		// 因此我们按 1 字节步进，对每个步进去测试各种可能的对齐读取
		for (size_t off = 0; off + 1 <= toRead; off += 1) {
			if (m_cancel.load()) break;
			uint64_t addr = chunkBase + off;

			// 按 CE 顺序尝试各类型：Byte(1,1)→Int16(2,2)→Int32(4,4)→Int64(8,8)→Float(4,4)→Double(8,8)
			bool matched = false;
			int matchedTypeIdx = -1;

			for (int ti = 0; ti < kAllNumTypes && !matched; ++ti) {
				const auto& entry = kAllTypes[ti];
				// 对齐检查：地址必须满足 entry.alignment
				if (addr % entry.alignment != 0) continue;
				// 边界检查：不能超过 chunk 末尾
				if (off + entry.size > toRead) continue;

				switch (entry.type) {
				case ScanDataType::Int8: {
					int8_t val;
					std::memcpy(&val, memBuf.data() + off, 1);
					int8_t v1 = static_cast<int8_t>(typeV1[ti]);
					int8_t v2 = static_cast<int8_t>(typeV2[ti]);
					if (compareIntValue(val, v1, v2, request.firstType, request.notMatch)) {
						matched = true; matchedTypeIdx = ti;
					}
					break;
				}
				case ScanDataType::Int16: {
					int16_t val;
					std::memcpy(&val, memBuf.data() + off, 2);
					int16_t v1 = static_cast<int16_t>(typeV1[ti]);
					int16_t v2 = static_cast<int16_t>(typeV2[ti]);
					if (compareIntValue(val, v1, v2, request.firstType, request.notMatch)) {
						matched = true; matchedTypeIdx = ti;
					}
					break;
				}
				case ScanDataType::Int32: {
					int32_t val;
					std::memcpy(&val, memBuf.data() + off, 4);
					int32_t v1 = static_cast<int32_t>(typeV1[ti]);
					int32_t v2 = static_cast<int32_t>(typeV2[ti]);
					if (compareIntValue(val, v1, v2, request.firstType, request.notMatch)) {
						matched = true; matchedTypeIdx = ti;
					}
					break;
				}
				case ScanDataType::Int64: {
					int64_t val;
					std::memcpy(&val, memBuf.data() + off, 8);
					int64_t v1 = static_cast<int64_t>(typeV1[ti]);
					int64_t v2 = static_cast<int64_t>(typeV2[ti]);
					if (compareIntValue(val, v1, v2, request.firstType, request.notMatch)) {
						matched = true; matchedTypeIdx = ti;
					}
					break;
				}
				case ScanDataType::Float32: {
					float val;
					std::memcpy(&val, memBuf.data() + off, 4);
					float v1, v2;
					std::memcpy(&v1, &typeV1[ti], sizeof(float));
					std::memcpy(&v2, &typeV2[ti], sizeof(float));
					if (compareFloatFirst(val, v1, v2, request.firstType,
						request.containApproximateValue, request.notMatch)) {
						matched = true; matchedTypeIdx = ti;
					}
					break;
				}
				case ScanDataType::Float64: {
					double val;
					std::memcpy(&val, memBuf.data() + off, 8);
					double v1, v2;
					std::memcpy(&v1, &typeV1[ti], sizeof(double));
					std::memcpy(&v2, &typeV2[ti], sizeof(double));
					if (compareDoubleFirst(val, v1, v2, request.firstType,
						request.containApproximateValue, request.notMatch)) {
						matched = true; matchedTypeIdx = ti;
					}
					break;
				}
				default: break;
				}
			}

			if (matched && matchedTypeIdx >= 0) {
				ScanResult sr;
				sr.address = addr;
				sr.matchedType = kAllTypes[matchedTypeIdx].type;
				batchResults.push_back(sr);
				if (batchResults.size() >= 1024) {
					outCache->push_back_batch(batchResults);
					batchResults.clear();
				}
			}
		}
	}

	if (!batchResults.empty()) outCache->push_back_batch(batchResults);
	m_progress.fetch_add(1);
}

// =============================================================================
// taskNextScanAll — All 再次扫描：对每个地址重新尝试所有类型
// =============================================================================
void ScanEngine::taskNextScanAll(const ScanRequest& request,
	const std::vector<ScanResult>& oldBatch,
	std::shared_ptr<IProcessMemorySnapshot> currentSnapshot,
	std::shared_ptr<IProcessMemorySnapshot> previousSnapshot,
	std::shared_ptr<AdaptiveCachePool<ScanResult>> outCache)
{
	if (m_cancel.load()) return;

	auto firstSnap = m_processSnapshotManager->getFirstProcessMemeorySnapshot();

	auto* p = std::get_if<ValueParams>(&request.params);
	uint64_t typeV1[kAllNumTypes] = {0};
	uint64_t typeV2[kAllNumTypes] = {0};
	if (p) {
		for (int ti = 0; ti < kAllNumTypes; ++ti) {
			typeV1[ti] = p->value1;
			typeV2[ti] = p->value2;
		}
		if (request.containApproximateValue &&
			(request.nextType == NextScanType::Equal || request.nextType == NextScanType::NotEqual)) {
			// Float32
			{
				float target;
				std::memcpy(&target, &p->value1, sizeof(float));
				constexpr float relEps = 0.05f;
				float lo = target * (1.0f - relEps);
				float hi = target * (1.0f + relEps);
				float absMin = 0.0001f;
				if (target >= 0 && lo < -absMin) lo = 0.0f;
				if (hi - lo < absMin) { lo = target - absMin; hi = target + absMin; }
				std::memcpy(&typeV1[4], &lo, sizeof(float));
				std::memcpy(&typeV2[4], &hi, sizeof(float));
			}
			// Float64
			{
				double target;
				std::memcpy(&target, &p->value1, sizeof(double));
				constexpr double relEps = 0.05;
				double lo = target * (1.0 - relEps);
				double hi = target * (1.0 + relEps);
				double absMin = 0.0001;
				if (target >= 0 && lo < -absMin) lo = 0.0;
				if (hi - lo < absMin) { lo = target - absMin; hi = target + absMin; }
				std::memcpy(&typeV1[5], &lo, sizeof(double));
				std::memcpy(&typeV2[5], &hi, sizeof(double));
			}
		}
	}

	std::vector<ScanResult> survivors;
	survivors.reserve(oldBatch.size());

	// 需要对比历史值 → 准备 8 字节缓冲区读取新旧快照
	const size_t maxRead = 8;
	uint8_t curBuf[maxRead];
	uint8_t oldBuf[maxRead];
	uint8_t firstBuf[maxRead];

	for (const auto& res : oldBatch) {
		if (m_cancel.load()) break;
		uint64_t addr = res.address;

		// 读取当前值（读取 8 字节即可覆盖所有类型）
		if (!currentSnapshot->readData(addr, curBuf, maxRead)) continue;

		bool matched = false;
		int matchedTypeIdx = -1;

		for (int ti = 0; ti < kAllNumTypes && !matched; ++ti) {
			const auto& entry = kAllTypes[ti];
			if (addr % entry.alignment != 0) continue;
			if (entry.size > maxRead) continue;

			switch (entry.type) {
			case ScanDataType::Int8: {
				int8_t cur; std::memcpy(&cur, curBuf, 1);
				int8_t old = 0;
				// 读取旧值
				if (needOldValueForNextScan(request.nextType)) {
					if (previousSnapshot && previousSnapshot->readData(addr, oldBuf, 1))
						std::memcpy(&old, oldBuf, 1);
					else continue;
				}
				int8_t v1 = static_cast<int8_t>(typeV1[ti]);
				int8_t v2 = static_cast<int8_t>(typeV2[ti]);
				if (compareIntNextValue(cur, old, v1, v2, request.nextType, request.notMatch)) {
					matched = true; matchedTypeIdx = ti;
				}
				break;
			}
			case ScanDataType::Int16: {
				int16_t cur; std::memcpy(&cur, curBuf, 2);
				int16_t old = 0;
				if (needOldValueForNextScan(request.nextType)) {
					if (previousSnapshot && previousSnapshot->readData(addr, oldBuf, 2))
						std::memcpy(&old, oldBuf, 2);
					else continue;
				}
				int16_t v1 = static_cast<int16_t>(typeV1[ti]);
				int16_t v2 = static_cast<int16_t>(typeV2[ti]);
				if (compareIntNextValue(cur, old, v1, v2, request.nextType, request.notMatch)) {
					matched = true; matchedTypeIdx = ti;
				}
				break;
			}
			case ScanDataType::Int32: {
				int32_t cur; std::memcpy(&cur, curBuf, 4);
				int32_t old = 0;
				if (needOldValueForNextScan(request.nextType)) {
					if (previousSnapshot && previousSnapshot->readData(addr, oldBuf, 4))
						std::memcpy(&old, oldBuf, 4);
					else continue;
				}
				int32_t v1 = static_cast<int32_t>(typeV1[ti]);
				int32_t v2 = static_cast<int32_t>(typeV2[ti]);
				if (compareIntNextValue(cur, old, v1, v2, request.nextType, request.notMatch)) {
					matched = true; matchedTypeIdx = ti;
				}
				break;
			}
			case ScanDataType::Int64: {
				int64_t cur; std::memcpy(&cur, curBuf, 8);
				int64_t old = 0;
				if (needOldValueForNextScan(request.nextType)) {
					if (previousSnapshot && previousSnapshot->readData(addr, oldBuf, 8))
						std::memcpy(&old, oldBuf, 8);
					else continue;
				}
				int64_t v1 = static_cast<int64_t>(typeV1[ti]);
				int64_t v2 = static_cast<int64_t>(typeV2[ti]);
				if (compareIntNextValue(cur, old, v1, v2, request.nextType, request.notMatch)) {
					matched = true; matchedTypeIdx = ti;
				}
				break;
			}
			case ScanDataType::Float32: {
				float cur; std::memcpy(&cur, curBuf, 4);
				float old = 0.0f;
				if (needOldValueForNextScan(request.nextType)) {
					if (previousSnapshot && previousSnapshot->readData(addr, oldBuf, 4))
						std::memcpy(&old, oldBuf, 4);
					else { if (request.nextType != NextScanType::Between) continue; }
				}
				float v1, v2;
				std::memcpy(&v1, &typeV1[ti], sizeof(float));
				std::memcpy(&v2, &typeV2[ti], sizeof(float));
				if (compareFloatNext(cur, old, v1, v2, request.nextType,
					request.containApproximateValue, request.notMatch)) {
					matched = true; matchedTypeIdx = ti;
				}
				break;
			}
			case ScanDataType::Float64: {
				double cur; std::memcpy(&cur, curBuf, 8);
				double old = 0.0;
				if (needOldValueForNextScan(request.nextType)) {
					if (previousSnapshot && previousSnapshot->readData(addr, oldBuf, 8))
						std::memcpy(&old, oldBuf, 8);
					else { if (request.nextType != NextScanType::Between) continue; }
				}
				double v1, v2;
				std::memcpy(&v1, &typeV1[ti], sizeof(double));
				std::memcpy(&v2, &typeV2[ti], sizeof(double));
				if (compareDoubleNext(cur, old, v1, v2, request.nextType,
					request.containApproximateValue, request.notMatch)) {
					matched = true; matchedTypeIdx = ti;
				}
				break;
			}
			default: break;
			}
		}

		if (matched && matchedTypeIdx >= 0) {
			ScanResult sr;
			sr.address = addr;
			sr.matchedType = kAllTypes[matchedTypeIdx].type;
			survivors.push_back(sr);
		}
	}

	if (!survivors.empty()) outCache->push_back_batch(survivors);
	m_progress.fetch_add(static_cast<int>(oldBatch.size()));
}

// =============================================================================
// taskFullScanWithNextConditionAll — All + UnknownInitial 再次扫描
// =============================================================================
void ScanEngine::taskFullScanWithNextConditionAll(const ScanRequest& request, MemoryRegion region,
	std::shared_ptr<IProcessMemorySnapshot> currentSnapshot,
	std::shared_ptr<IProcessMemorySnapshot> previousSnapshot,
	std::shared_ptr<AdaptiveCachePool<ScanResult>> outCache)
{
	if (m_cancel.load()) return;
	if (region.size == 0) { m_progress.fetch_add(1); return; }

	auto firstSnap = m_processSnapshotManager->getFirstProcessMemeorySnapshot();

	auto* p = std::get_if<ValueParams>(&request.params);
	uint64_t typeV1[kAllNumTypes] = {0};
	uint64_t typeV2[kAllNumTypes] = {0};
	if (p) {
		for (int ti = 0; ti < kAllNumTypes; ++ti) {
			typeV1[ti] = p->value1;
			typeV2[ti] = p->value2;
		}
		if (request.containApproximateValue &&
			(request.nextType == NextScanType::Equal || request.nextType == NextScanType::NotEqual)) {
			{
				float target;
				std::memcpy(&target, &p->value1, sizeof(float));
				constexpr float relEps = 0.05f;
				float lo = target * (1.0f - relEps);
				float hi = target * (1.0f + relEps);
				float absMin = 0.0001f;
				if (target >= 0 && lo < -absMin) lo = 0.0f;
				if (hi - lo < absMin) { lo = target - absMin; hi = target + absMin; }
				std::memcpy(&typeV1[4], &lo, sizeof(float));
				std::memcpy(&typeV2[4], &hi, sizeof(float));
			}
			{
				double target;
				std::memcpy(&target, &p->value1, sizeof(double));
				constexpr double relEps = 0.05;
				double lo = target * (1.0 - relEps);
				double hi = target * (1.0 + relEps);
				double absMin = 0.0001;
				if (target >= 0 && lo < -absMin) lo = 0.0;
				if (hi - lo < absMin) { lo = target - absMin; hi = target + absMin; }
				std::memcpy(&typeV1[5], &lo, sizeof(double));
				std::memcpy(&typeV2[5], &hi, sizeof(double));
			}
		}
	}

	const size_t maxRead = 8;
	const size_t chunkSize = 128 * 1024;
	std::vector<uint8_t> curBuf(chunkSize + maxRead);
	std::vector<uint8_t> prevBuf(chunkSize + maxRead);
	std::vector<ScanResult> batchResults;
	batchResults.reserve(4096);

	bool needsPrevBuf = needOldValueForNextScan(request.nextType);

	for (size_t baseOffset = 0; baseOffset < region.size && !m_cancel.load(); baseOffset += chunkSize) {
		size_t toRead = (std::min)(chunkSize, region.size - baseOffset);
		uint64_t chunkBase = region.base + baseOffset;
		if (!currentSnapshot->readData(chunkBase, curBuf.data(), toRead)) continue;

		if (needsPrevBuf) {
			auto* srcSnap = (request.nextType == NextScanType::Compare_to_First_Scan) ? firstSnap.get() : previousSnapshot.get();
			if (!srcSnap || !srcSnap->readData(chunkBase, prevBuf.data(), toRead)) continue;
		}

		for (size_t off = 0; off + 1 <= toRead && !m_cancel.load(); off += 1) {
			uint64_t addr = chunkBase + off;
			bool matched = false;
			int matchedTypeIdx = -1;

			for (int ti = 0; ti < kAllNumTypes && !matched; ++ti) {
				const auto& entry = kAllTypes[ti];
				if (addr % entry.alignment != 0) continue;
				if (off + entry.size > toRead) continue;

				switch (entry.type) {
				case ScanDataType::Int8: {
					int8_t cur; std::memcpy(&cur, curBuf.data() + off, 1);
					int8_t old = 0;
					if (needsPrevBuf) std::memcpy(&old, prevBuf.data() + off, 1);
					int8_t v1 = static_cast<int8_t>(typeV1[ti]);
					int8_t v2 = static_cast<int8_t>(typeV2[ti]);
					if (compareIntNextValue(cur, old, v1, v2, request.nextType, request.notMatch)) {
						matched = true; matchedTypeIdx = ti;
					}
					break;
				}
				case ScanDataType::Int16: {
					int16_t cur; std::memcpy(&cur, curBuf.data() + off, 2);
					int16_t old = 0;
					if (needsPrevBuf) std::memcpy(&old, prevBuf.data() + off, 2);
					int16_t v1 = static_cast<int16_t>(typeV1[ti]);
					int16_t v2 = static_cast<int16_t>(typeV2[ti]);
					if (compareIntNextValue(cur, old, v1, v2, request.nextType, request.notMatch)) {
						matched = true; matchedTypeIdx = ti;
					}
					break;
				}
				case ScanDataType::Int32: {
					int32_t cur; std::memcpy(&cur, curBuf.data() + off, 4);
					int32_t old = 0;
					if (needsPrevBuf) std::memcpy(&old, prevBuf.data() + off, 4);
					int32_t v1 = static_cast<int32_t>(typeV1[ti]);
					int32_t v2 = static_cast<int32_t>(typeV2[ti]);
					if (compareIntNextValue(cur, old, v1, v2, request.nextType, request.notMatch)) {
						matched = true; matchedTypeIdx = ti;
					}
					break;
				}
				case ScanDataType::Int64: {
					int64_t cur; std::memcpy(&cur, curBuf.data() + off, 8);
					int64_t old = 0;
					if (needsPrevBuf) std::memcpy(&old, prevBuf.data() + off, 8);
					int64_t v1 = static_cast<int64_t>(typeV1[ti]);
					int64_t v2 = static_cast<int64_t>(typeV2[ti]);
					if (compareIntNextValue(cur, old, v1, v2, request.nextType, request.notMatch)) {
						matched = true; matchedTypeIdx = ti;
					}
					break;
				}
				case ScanDataType::Float32: {
					float cur; std::memcpy(&cur, curBuf.data() + off, 4);
					float old = 0.0f;
					if (needsPrevBuf) std::memcpy(&old, prevBuf.data() + off, 4);
					float v1, v2;
					std::memcpy(&v1, &typeV1[ti], sizeof(float));
					std::memcpy(&v2, &typeV2[ti], sizeof(float));
					if (compareFloatNext(cur, old, v1, v2, request.nextType,
						request.containApproximateValue, request.notMatch)) {
						matched = true; matchedTypeIdx = ti;
					}
					break;
				}
				case ScanDataType::Float64: {
					double cur; std::memcpy(&cur, curBuf.data() + off, 8);
					double old = 0.0;
					if (needsPrevBuf) std::memcpy(&old, prevBuf.data() + off, 8);
					double v1, v2;
					std::memcpy(&v1, &typeV1[ti], sizeof(double));
					std::memcpy(&v2, &typeV2[ti], sizeof(double));
					if (compareDoubleNext(cur, old, v1, v2, request.nextType,
						request.containApproximateValue, request.notMatch)) {
						matched = true; matchedTypeIdx = ti;
					}
					break;
				}
				default: break;
				}
			}

			if (matched && matchedTypeIdx >= 0) {
				ScanResult sr;
				sr.address = addr;
				sr.matchedType = kAllTypes[matchedTypeIdx].type;
				batchResults.push_back(sr);
				if (batchResults.size() >= 4096) {
					outCache->push_back_batch(batchResults);
					batchResults.clear();
				}
			}
		}
	}

	if (!batchResults.empty()) outCache->push_back_batch(batchResults);
	m_progress.fetch_add(1);
}


template <typename T>
void ScanEngine::dispatchScan(const ScanRequest& request, const std::vector<ScanResult>& prevResults,
	std::shared_ptr<AdaptiveCachePool<ScanResult>> outCache)
{
	auto regions = ProcessManager::instance().getMemoryRegions(request);
	auto currentSnap = std::shared_ptr<IProcessMemorySnapshot>(m_processSnapshotManager->createSnapshot(regions));
	auto prevSnap = m_processSnapshotManager->getPreviousProcessMemeorySnapshot();


	// 用于等待所有线程完成的期值列表
	std::vector<std::future<void>> futures;

	if (request.mode == ScanMode::First) {
		// ── UnknownInitial 首次扫描：只计数，不存地址 ──
		if (request.firstType == ScanType::UnknownInitial) {
			m_totalItems.store(static_cast<int>(regions.size()));
			m_potential_Address.store(0);
			for (const auto& memory_region_section : regions) {
				// 直接在此计数，无需提交到 outCache，避免海量地址占用内存
				size_t count = memory_region_section.size / request.alignment;
				m_potential_Address.fetch_add(static_cast<int>(count), std::memory_order_relaxed);
				m_progress.fetch_add(1);
			}
			m_processSnapshotManager->setFirstSnapshot(currentSnap);
			m_processSnapshotManager->setPreviousSnapshot(currentSnap);
		} else {
			m_totalItems.store(static_cast<int>(regions.size()));
			for (const auto& memory_region_section : regions) {
				futures.push_back(GlobalThreadPool::instance().enqueue([this, request, memory_region_section, currentSnap, outCache] {
					taskFirstScan<T>(request, memory_region_section, currentSnap, outCache);
					}));
			}
			m_processSnapshotManager->setFirstSnapshot(currentSnap);
		}
	}
	else {
		// ── UnknownInitial 之后的再次扫描：全内存遍历 + next-scan 条件 ──
		if (prevResults.empty() && m_potential_Address.load() > 0) {
			m_totalItems.store(static_cast<int>(regions.size()));
			m_potential_Address.store(0); // 重置，后续再次扫描走常规逻辑
			for (const auto& memory_region_section : regions) {
				futures.push_back(GlobalThreadPool::instance().enqueue(
					[this, request, memory_region_section, currentSnap, prevSnap, outCache] {
						taskFullScanWithNextCondition<T>(request, memory_region_section, currentSnap, prevSnap, outCache);
					}));
			}
		} else {
			m_totalItems.store(static_cast<int>(prevResults.size()));
			const size_t batchSize = 4096;
			for (size_t i = 0; i < prevResults.size(); i += batchSize) {
				std::vector<ScanResult> batch;
				size_t end = (std::min)(i + batchSize, prevResults.size());
				batch.assign(prevResults.begin() + i, prevResults.begin() + end);
				futures.push_back(GlobalThreadPool::instance().enqueue(
					[this, request, batch, currentSnap, prevSnap, outCache] {
						taskNextScan<T>(request, batch, currentSnap, prevSnap, outCache);
					}));
			}
		}
	}



	for (auto& fut : futures) {
		if (fut.valid()) fut.get();
	}
	m_processSnapshotManager->setPreviousSnapshot(currentSnap);
}

template <typename T>
void ScanEngine::taskFirstScan(const ScanRequest& request, MemoryRegion region,
	std::shared_ptr<IProcessMemorySnapshot> currentSnap,
	std::shared_ptr<AdaptiveCachePool<ScanResult>> outCache)
{
	if (m_cancel.load()) return;

	// ── 字符串 / 字节数组 首次扫描（仅 T=uint8_t 时编译，独立 Chunk 循环）──
	if constexpr (sizeof(T) == 1) {
		if (isStringType(request.dataType) || isByteArrayType(request.dataType)) {
			if (region.size == 0) { m_progress.fetch_add(1); return; }
			// 计算最大模式长度，用于 chunk 间的重叠
			size_t maxPatternLen = 0;
			if (isStringType(request.dataType)) {
				if (auto* sp = std::get_if<StringParams>(&request.params)) {
					// sp->text.length() 对于 Utf16String 已经是 UTF-16 LE 原始字节长度
					// 对于 Ascii/Utf8 也是字节长度，所以不需要额外乘 2
					maxPatternLen = sp->text.length();
				}
			} else {
				if (auto* ap = std::get_if<AobParams>(&request.params))
					maxPatternLen = ap->pattern.size();
			}
			if (maxPatternLen == 0) { m_progress.fetch_add(1); return; }
			const size_t chunkSize = 64 * 1024;
			const size_t overlap = maxPatternLen; // 确保跨越边界的模式不会漏掉
			std::vector<uint8_t> memBuf(chunkSize + overlap);
			std::vector<ScanResult> batchResults; batchResults.reserve(2048);
			bool firstChunk = true;
			for (size_t off = 0; off < region.size && !m_cancel.load(); off += chunkSize) {
				size_t toRead = (std::min)(chunkSize + (firstChunk ? 0 : overlap), region.size - off);
				// 从内存快照 snapshot 中读取数据，而非直接读进程内存
				if (!currentSnap->readData(region.base + off, memBuf.data(), toRead)) continue;
				std::vector<uint64_t> hits;
				// 非首 chunk 时，只提交 chunkSize 之后的新数据（避免重复匹配 overlap 区域）
				if (isStringType(request.dataType)) {
					if (auto* sp = std::get_if<StringParams>(&request.params)) {
						std::vector<uint8_t> searchView(memBuf.begin(), memBuf.begin() + toRead);
						performStringSearch(searchView, region.base + off, *sp, request.dataType, hits);
						// 去重：滤掉 overlap 区域中的匹配（只保留 chunkSize 之后的）
						hits.erase(std::remove_if(hits.begin(), hits.end(), [&](uint64_t addr) {
							return !firstChunk && addr < region.base + off + overlap;
						}), hits.end());
					}
				} else {
					if (auto* ap = std::get_if<AobParams>(&request.params)) {
						performAobSearch(memBuf, region.base + off, *ap, hits);
						hits.erase(std::remove_if(hits.begin(), hits.end(), [&](uint64_t addr) {
							return !firstChunk && addr < region.base + off + overlap;
						}), hits.end());
					}
				}
				for (auto addr : hits) {
					batchResults.push_back({ addr });
					if (batchResults.size() >= 1024) { outCache->push_back_batch(batchResults); batchResults.clear(); }
				}
				firstChunk = false;
			}
			if (!batchResults.empty()) outCache->push_back_batch(batchResults);
			m_progress.fetch_add(1);
			return;
		}
	}

	// ── 数值类型首次扫描：使用 SIMD 从 snapshot 中读取并匹配 ──
	if (region.size < sizeof(T)) { m_progress.fetch_add(1); return; }

	const size_t step = request.alignment;
	const size_t chunkSize = 64 * 1024; // 64KB 分块
	std::vector<uint8_t> memBuf(chunkSize + sizeof(T));
	std::vector<uint8_t> targetBuf(chunkSize + sizeof(T)); // 用于 SIMD 比较的目标填充块
	std::vector<ScanResult> batchResults;
	batchResults.reserve(2048);

	T v1 = 0, v2 = 0;
		const bool isFloatApprox = std::is_floating_point_v<T>
		&& request.containApproximateValue
		&& request.firstType == ScanType::ExactValue;

	if (auto* p = std::get_if<ValueParams>(&request.params)) {
		if constexpr (std::is_floating_point_v<T>) {
			T target;
			std::memcpy(&target, &p->value1, sizeof(T));
			if (isFloatApprox) {
				// ★ 勾选了"包含近似值" → 使用 ±5% 相对容差
				//   对于目标值 1000，范围 [950, 1050]；对于目标值 0.5，范围 [0.475, 0.525]
				//   相比于固定 ±0.5，百分比容差对大值和小值都更合理
				constexpr T relativeEpsilon = static_cast<T>(0.01); // ±1%
				T lo = target * (static_cast<T>(1.0) - relativeEpsilon);
				T hi = target * (static_cast<T>(1.0) + relativeEpsilon);
				// 处理目标值为 0 或负数的边界情况，保证至少有一个最小绝对容差
				T absMin = static_cast<T>(0.0001);
				if (target >= static_cast<T>(0)) {
					if (lo < -absMin) lo = static_cast<T>(0);        // 下限不跌破 0（对于正数场景更自然）
				}
				if (hi - lo < absMin) { lo = target - absMin; hi = target + absMin; }
				std::memcpy(&v1, &lo, sizeof(T));
				std::memcpy(&v2, &hi, sizeof(T));
			} else {
				// 未勾选近似值 / GreaterThan/LessThan/Between → 取精确位值
				std::memcpy(&v1, &target, sizeof(T));
				if (request.firstType == ScanType::Between) {
					T tmp;
					std::memcpy(&tmp, &p->value2, sizeof(T));
					std::memcpy(&v2, &tmp, sizeof(T));
				}
			}
		} else {
			std::memcpy(&v1, &p->value1, sizeof(T));
			if (request.firstType == ScanType::Between)
				std::memcpy(&v2, &p->value2, sizeof(T));
		}
	}

	// 填充 targetBuf（浮点数含近似值 ExactValue 走标量区间，不需要 SIMD 目标块）
	const bool needTargetBuf = !isFloatApprox
		&& request.firstType != ScanType::UnknownInitial
		&& request.firstType != ScanType::Between;
	if (needTargetBuf) {
		for (size_t i = 0; i < targetBuf.size(); i += sizeof(T))
			std::memcpy(targetBuf.data() + i, &v1, sizeof(T));
	}

	for (size_t baseOffset = 0; baseOffset < region.size; baseOffset += chunkSize) {
		if (m_cancel.load()) break;
		size_t toRead = std::min(chunkSize, region.size - baseOffset);
		// 从内存快照 snapshot 中读取数据，而非直接读进程内存
		if (!currentSnap->readData(region.base + baseOffset, memBuf.data(), toRead)) continue;

		if (request.firstType == ScanType::UnknownInitial) {
			// 未知初始值：记录该区域内所有对齐地址
			for (size_t off = 0; off + sizeof(T) <= toRead; off += step) {
				batchResults.push_back({ region.base + baseOffset + off });
				if (batchResults.size() >= 1024) { outCache->push_back_batch(batchResults); batchResults.clear(); }
			}
		}
		else if (!isFloatApprox && (request.firstType == ScanType::ExactValue) && !request.notMatch) {
			// ── 精确值匹配 SIMD ──
			std::vector<uint64_t> matchedAddrs;
			SimdScanner::scanMemoryBlockForMatches<T>(memBuf.data(), targetBuf.data(), toRead,
				region.base + baseOffset, step, SimdOp::Equal, matchedAddrs);
			for (auto addr : matchedAddrs) {
				batchResults.push_back({ addr });
				if (batchResults.size() >= 1024) { outCache->push_back_batch(batchResults); batchResults.clear(); }
			}
		}
		else if (!isFloatApprox && (request.firstType == ScanType::ExactValue) && request.notMatch) {
			// ── 勾选了"非" + 精确值 → SIMD NotEqual ──
			std::vector<uint64_t> matchedAddrs;
			SimdScanner::scanMemoryBlockForMatches<T>(memBuf.data(), targetBuf.data(), toRead,
				region.base + baseOffset, step, SimdOp::NotEqual, matchedAddrs);
			for (auto addr : matchedAddrs) {
				batchResults.push_back({ addr });
				if (batchResults.size() >= 1024) { outCache->push_back_batch(batchResults); batchResults.clear(); }
			}
		}
		else if (!isFloatApprox && !request.notMatch && (request.firstType == ScanType::GreaterThan || request.firstType == ScanType::LessThan)) {
			// 使用 SIMD 加速
			SimdOp op = (request.firstType == ScanType::GreaterThan) ? SimdOp::Greater : SimdOp::Less;
			std::vector<uint64_t> matchedAddrs;
			SimdScanner::scanMemoryBlockForMatches<T>(memBuf.data(), targetBuf.data(), toRead,
				region.base + baseOffset, step, op, matchedAddrs);
			for (auto addr : matchedAddrs) {
				batchResults.push_back({ addr });
				if (batchResults.size() >= 1024) { outCache->push_back_batch(batchResults); batchResults.clear(); }
			}
		}
		else if (request.firstType == ScanType::Between && !request.notMatch) {
			// Between 类型：使用 SIMD 范围扫描加速
			std::vector<uint64_t> matchedAddrs;
			SimdScanner::scanMemoryBlockForRange<T>(memBuf.data(), toRead,
				region.base + baseOffset, step, v1, v2, matchedAddrs);
			for (auto addr : matchedAddrs) {
				batchResults.push_back({ addr });
				if (batchResults.size() >= 1024) { outCache->push_back_batch(batchResults); batchResults.clear(); }
			}
		}
		else {
			// ── 标量回退：浮点数近似值 ExactValue / notMatch+GreaterThan/LessThan/Between ──
			for (size_t off = 0; off + sizeof(T) <= toRead; off += step) {
				T curVal;
				std::memcpy(&curVal, memBuf.data() + off, sizeof(T));
				bool match = false;
				if (request.notMatch) {
					// 非模式：反转条件
					switch (request.firstType) {
					case ScanType::ExactValue: // 浮点近似值取反
						match = (curVal < v1 || curVal > v2);
						break;
					case ScanType::GreaterThan:
						match = (curVal <= v1);
						break;
					case ScanType::LessThan:
						match = (curVal >= v1);
						break;
					case ScanType::Between:
						match = (curVal < v1 || curVal > v2);
						break;
					default:
						match = false;
						break;
					}
				} else {
					// 正常模式
					switch (request.firstType) {
					case ScanType::ExactValue: // 浮点近似值在 [v1,v2] 内
					case ScanType::GreaterThan:
					case ScanType::LessThan:
						match = (curVal >= v1 && curVal <= v2);
						break;
					case ScanType::Between:
						match = (curVal >= v1 && curVal <= v2);
						break;
					default:
						match = false;
						break;
					}
				}
				if (match) {
					batchResults.push_back({ region.base + baseOffset + off });
					if (batchResults.size() >= 1024) { outCache->push_back_batch(batchResults); batchResults.clear(); }
				}
			}
		}
	}

	if (!batchResults.empty()) outCache->push_back_batch(batchResults);
	m_progress.fetch_add(1);
}

template <typename T>
void ScanEngine::taskNextScan(const ScanRequest& request,
	const std::vector<ScanResult>& oldBatch,
	std::shared_ptr<IProcessMemorySnapshot> currentSnapshot,
	std::shared_ptr<IProcessMemorySnapshot> previousSnapshot,
	std::shared_ptr<AdaptiveCachePool<ScanResult>> outCache)
{
	std::vector<ScanResult> survivors;
	survivors.reserve(oldBatch.size());
	auto firstSnap = m_processSnapshotManager->getFirstProcessMemeorySnapshot();

	auto* p = std::get_if<ValueParams>(&request.params);
	T v1 = 0, v2 = 0;
	if (p) {
		if constexpr (std::is_floating_point_v<T>) {
			T target;
			std::memcpy(&target, &p->value1, sizeof(T));
			const bool useApprox = request.containApproximateValue
				&& (request.nextType == NextScanType::Equal || request.nextType == NextScanType::NotEqual);
			if (useApprox) {
				// ★ 勾选了"包含近似值" → 使用 ±5% 相对容差
				constexpr T relativeEpsilon = static_cast<T>(0.05); // ±5%
				T lo = target * (static_cast<T>(1.0) - relativeEpsilon);
				T hi = target * (static_cast<T>(1.0) + relativeEpsilon);
				T absMin = static_cast<T>(0.0001);
				if (target >= static_cast<T>(0)) {
					if (lo < -absMin) lo = static_cast<T>(0);
				}
				if (hi - lo < absMin) { lo = target - absMin; hi = target + absMin; }
				std::memcpy(&v1, &lo, sizeof(T));
				std::memcpy(&v2, &hi, sizeof(T));
			} else {
				std::memcpy(&v1, &target, sizeof(T));
				std::memcpy(&v2, &p->value2, sizeof(T));
			}
		} else {
			std::memcpy(&v1, &p->value1, sizeof(T));
			std::memcpy(&v2, &p->value2, sizeof(T));
		}
	}

	for (const auto& res : oldBatch) {
		if (m_cancel.load()) break;

		// ── 字符串 / 字节数组（仅 T=uint8_t 时编译，直接比较后 continue）──
		if constexpr (sizeof(T) == 1) {
			if (isStringType(request.dataType)) {
				auto* sp = std::get_if<StringParams>(&request.params);
				if (sp && !sp->text.empty()) {
					// sp->text.length() 对于 Utf16String 已经是 UTF-16 LE 的字节数
					size_t len = sp->text.length();
					std::vector<uint8_t> buf(len);
					if (currentSnapshot->readData(res.address, buf.data(), len)) {
						std::vector<uint64_t> matched;
						performStringSearch(buf, res.address, *sp, request.dataType, matched);
						if (!matched.empty()) survivors.push_back(res);
					}
				}
				continue;
			}
			if (isByteArrayType(request.dataType)) {
				auto* ap = std::get_if<AobParams>(&request.params);
				if (ap && !ap->pattern.empty()) {
					std::vector<uint8_t> buf(ap->pattern.size());
					if (currentSnapshot->readData(res.address, buf.data(), ap->pattern.size())) {
						std::vector<uint64_t> matched;
						performAobSearch(buf, res.address, *ap, matched);
						if (!matched.empty()) survivors.push_back(res);
					}
				}
				continue;
			}
		}

		T curVal, oldVal;
		if (!currentSnapshot->readValue(res.address, curVal)) continue;

		bool match = false;
		switch (request.nextType) {
		case NextScanType::Equal:
			if constexpr (std::is_floating_point_v<T>) {
				// ★ 浮点数 Equal 使用 Epsilon 范围匹配
				match = (curVal >= v1 && curVal <= v2);
			} else {
				match = (curVal == v1);
			}
			break;
		case NextScanType::NotEqual:
			if constexpr (std::is_floating_point_v<T>) {
				// ★ 浮点数 NotEqual 在范围外
				match = (curVal < v1 || curVal > v2);
			} else {
				match = (curVal != v1);
			}
			break;
		case NextScanType::Increased: if (previousSnapshot && previousSnapshot->readValue(res.address, oldVal)) match = (curVal > oldVal); break;
		case NextScanType::Decreased: if (previousSnapshot && previousSnapshot->readValue(res.address, oldVal)) match = (curVal < oldVal); break;
		case NextScanType::Changed:   if (previousSnapshot && previousSnapshot->readValue(res.address, oldVal)) match = (curVal != oldVal); break;
		case NextScanType::Unchanged: if (previousSnapshot && previousSnapshot->readValue(res.address, oldVal)) match = (curVal == oldVal); break;
		case NextScanType::Between:   match = (curVal >= v1 && curVal <= v2); break;
        case NextScanType::IncreasedBy: if (previousSnapshot && previousSnapshot->readValue(res.address, oldVal)) match = (curVal > oldVal + v1); break;
        case NextScanType::DecreasedBy: if (previousSnapshot && previousSnapshot->readValue(res.address, oldVal)) match = (curVal < oldVal - v1); break;
		case NextScanType::Compare_to_First_Scan: if (firstSnap && firstSnap->readValue(res.address, oldVal)) match = (curVal == oldVal); break;
		default: break;
		}
		// ★ 勾选了"非" → 反转匹配条件（精确数值反转 → 非精确数值，以此类推）
		if (request.notMatch) match = !match;
		if (match) survivors.push_back(res);
	}
	if (!survivors.empty()) outCache->push_back_batch(survivors);
	m_progress.fetch_add(static_cast<int>(oldBatch.size()));
}


template <typename T>
void ScanEngine::taskFullScanWithNextCondition(const ScanRequest& request, MemoryRegion region,
	std::shared_ptr<IProcessMemorySnapshot> currentSnapshot,
	std::shared_ptr<IProcessMemorySnapshot> previousSnapshot,
	std::shared_ptr<AdaptiveCachePool<ScanResult>> outCache)
{
	if (m_cancel.load()) return;

	const size_t step = request.alignment;
	const size_t scalarSize = sizeof(T);
	const size_t chunkSize = 128 * 1024; // 128KB 分块（更大块更有利于 SIMD）
	std::vector<uint8_t> memBuf(chunkSize + scalarSize);
	std::vector<uint8_t> prevBuf(chunkSize + scalarSize); // 存放上一次/首次快照的批量数据
	std::vector<ScanResult> batchResults;
	batchResults.reserve(4096);

	auto firstSnap = m_processSnapshotManager->getFirstProcessMemeorySnapshot();

	// ===== 解析参数 v1/v2 =====
	auto* p = std::get_if<ValueParams>(&request.params);
	T v1 = 0, v2 = 0;
	bool useRangeForEqualNotEqual = false; // 浮点数近似匹配时用范围比较
	if (p) {
		if constexpr (std::is_floating_point_v<T>) {
			T target;
			std::memcpy(&target, &p->value1, sizeof(T));
			const bool useApprox = request.containApproximateValue
				&& (request.nextType == NextScanType::Equal || request.nextType == NextScanType::NotEqual);
			if (useApprox) {
				useRangeForEqualNotEqual = true;
				constexpr T relativeEpsilon = static_cast<T>(0.05);
				T lo = target * (static_cast<T>(1.0) - relativeEpsilon);
				T hi = target * (static_cast<T>(1.0) + relativeEpsilon);
				T absMin = static_cast<T>(0.0001);
				if (target >= static_cast<T>(0)) {
					if (lo < -absMin) lo = static_cast<T>(0);
				}
				if (hi - lo < absMin) { lo = target - absMin; hi = target + absMin; }
				std::memcpy(&v1, &lo, sizeof(T));
				std::memcpy(&v2, &hi, sizeof(T));
			} else {
				std::memcpy(&v1, &target, sizeof(T));
				std::memcpy(&v2, &p->value2, sizeof(T));
			}
		} else {
			std::memcpy(&v1, &p->value1, sizeof(T));
			std::memcpy(&v2, &p->value2, sizeof(T));
		}
	}

	// ===== 判断走哪条快速路 =====
	enum class SimdPath {
		None,                  // 标量回退（IncreasedBy / DecreasedBy）
		CompareWithTarget,     // Equal / NotEqual → 用 SimdScanner::scanMemoryBlockForMatches
		RangeFilter,           // Between / 浮点近似 Equal/NotEqual → 用 scanMemoryBlockForRange
		CompareTwoBuffers      // Changed / Unchanged / Increased / Decreased / Compare_to_First_Scan
		                       // → 用 compareTwoMemoryBlocks
	};
	SimdPath simdPath = SimdPath::None;
	bool needsPrevBuf = false;

	switch (request.nextType) {
	case NextScanType::Equal:
		if constexpr (std::is_floating_point_v<T>) {
			simdPath = useRangeForEqualNotEqual ? SimdPath::RangeFilter : SimdPath::CompareWithTarget;
		} else {
			simdPath = SimdPath::CompareWithTarget;
		}
		break;
	case NextScanType::NotEqual:
		if constexpr (std::is_floating_point_v<T>) {
			simdPath = useRangeForEqualNotEqual ? SimdPath::RangeFilter : SimdPath::CompareWithTarget;
		} else {
			simdPath = SimdPath::CompareWithTarget;
		}
		break;
	case NextScanType::Between:
		simdPath = SimdPath::RangeFilter;
		break;
	case NextScanType::Changed:   simdPath = SimdPath::CompareTwoBuffers; needsPrevBuf = true; break;
	case NextScanType::Unchanged: simdPath = SimdPath::CompareTwoBuffers; needsPrevBuf = true; break;
	case NextScanType::Increased: simdPath = SimdPath::CompareTwoBuffers; needsPrevBuf = true; break;
	case NextScanType::Decreased: simdPath = SimdPath::CompareTwoBuffers; needsPrevBuf = true; break;
	case NextScanType::Compare_to_First_Scan: simdPath = SimdPath::CompareTwoBuffers; needsPrevBuf = true; break;
		// IncreasedBy / DecreasedBy → SimdPath::None（标量回退，但会整块读取 prevBuf 消除虚拟调用）
	case NextScanType::IncreasedBy: simdPath = SimdPath::None; needsPrevBuf = true; break;
	case NextScanType::DecreasedBy: simdPath = SimdPath::None; needsPrevBuf = true; break;
	default: break;
	}

	// Pre-allocate target buffer for CompareWithTarget path
	std::vector<uint8_t> targetBuf;
	if (simdPath == SimdPath::CompareWithTarget) {
		targetBuf.resize(chunkSize + scalarSize);
	}

	for (size_t baseOffset = 0; baseOffset < region.size && !m_cancel.load(); baseOffset += chunkSize) {
		size_t toRead = (std::min)(chunkSize, region.size - baseOffset);
		uint64_t chunkBase = region.base + baseOffset;

		// ★ 批量读取当前快照内存（vs 逐地址虚拟调用）
		if (!currentSnapshot->readData(chunkBase, memBuf.data(), toRead)) continue;

		// ★ 批量读取上一次/首次快照（vs 逐地址虚拟调用 * 数十亿次）
		if (needsPrevBuf) {
			auto* srcSnap = (request.nextType == NextScanType::Compare_to_First_Scan) ? firstSnap.get() : previousSnapshot.get();
			if (!srcSnap || !srcSnap->readData(chunkBase, prevBuf.data(), toRead)) continue;
		}

		// ===== SIMD 快速路径 =====
		if (simdPath == SimdPath::CompareWithTarget) {
			// 填充目标值缓冲区
			T targetVal = v1;
			for (size_t i = 0; i < toRead; i += scalarSize)
				std::memcpy(targetBuf.data() + i, &targetVal, scalarSize);

			SimdOp op = request.notMatch ? invertSimdOp((request.nextType == NextScanType::Equal) ? SimdOp::Equal : SimdOp::NotEqual)
			                             : ((request.nextType == NextScanType::Equal) ? SimdOp::Equal : SimdOp::NotEqual);
			std::vector<uint64_t> matched;
			SimdScanner::scanMemoryBlockForMatches<T>(
				memBuf.data(), targetBuf.data(), toRead, chunkBase, step, op, matched);

			for (auto addr : matched) {
				batchResults.push_back({ addr });
				if (batchResults.size() >= 4096) {
					outCache->push_back_batch(batchResults);
					batchResults.clear();
				}
			}
		}
		else if (simdPath == SimdPath::RangeFilter) {
			// ★ 注意：浮点数 NotEqual 近似模式需要的是"不在 [v1,v2] 内"
			//   但 SIMD 范围扫描只能找"在范围内"的，NotEqual 需要取补
			//   所以我们先用 range 找匹配的，然后用 std::vector<uint64_t> 收集再反转...
			//   但那样性能反而更差。对于 NotEqual 近似模式，仍然走标量回退。
			// ★ 如果勾选了"非"（notMatch=true），需要取补：匹配不在 [v1,v2] 的值
			const bool invertedRange = request.notMatch;
			if constexpr (std::is_floating_point_v<T>) {
				if (invertedRange || request.nextType == NextScanType::NotEqual) {
					// ★ 取补模式：标量回退（范围取补不好 SIMD）
					for (size_t off = 0; off + scalarSize <= toRead; off += step) {
						if (m_cancel.load()) break;
						T curVal;
						std::memcpy(&curVal, memBuf.data() + off, sizeof(T));
						bool inRange = (curVal >= v1 && curVal <= v2);
						if (invertedRange ? !inRange : inRange) {
							batchResults.push_back({ chunkBase + off });
							if (batchResults.size() >= 4096) {
								outCache->push_back_batch(batchResults);
								batchResults.clear();
							}
						}
					}
				} else {
					std::vector<uint64_t> matched;
					SimdScanner::scanMemoryBlockForRange<T>(
						memBuf.data(), toRead, chunkBase, step, v1, v2, matched);
					for (auto addr : matched) {
						batchResults.push_back({ addr });
						if (batchResults.size() >= 4096) {
							outCache->push_back_batch(batchResults);
							batchResults.clear();
						}
					}
				}
			} else {
				if (invertedRange) {
					// ★ 整数取补模式：标量回退
					for (size_t off = 0; off + scalarSize <= toRead; off += step) {
						if (m_cancel.load()) break;
						T curVal;
						std::memcpy(&curVal, memBuf.data() + off, sizeof(T));
						if (curVal < v1 || curVal > v2) {
							batchResults.push_back({ chunkBase + off });
							if (batchResults.size() >= 4096) {
								outCache->push_back_batch(batchResults);
								batchResults.clear();
							}
						}
					}
				} else {
					std::vector<uint64_t> matched;
					SimdScanner::scanMemoryBlockForRange<T>(
						memBuf.data(), toRead, chunkBase, step, v1, v2, matched);
					for (auto addr : matched) {
						batchResults.push_back({ addr });
						if (batchResults.size() >= 4096) {
							outCache->push_back_batch(batchResults);
							batchResults.clear();
						}
					}
				}
			}
		}
		else if (simdPath == SimdPath::CompareTwoBuffers) {
			// ★ SIMD 批量比较两个内存块（当前 vs 上次/首次快照）
				// ★ 根据 notMatch 反转比较操作
				SimdOp baseOp;
				switch (request.nextType) {
				case NextScanType::Changed:              baseOp = SimdOp::NotEqual; break;
				case NextScanType::Unchanged:            baseOp = SimdOp::Equal; break;
				case NextScanType::Increased:            baseOp = SimdOp::Greater; break;
				case NextScanType::Decreased:            baseOp = SimdOp::Less; break;
				case NextScanType::Compare_to_First_Scan: baseOp = SimdOp::Equal; break;
				default:                                 baseOp = SimdOp::Equal; break;
				}
				SimdOp op = request.notMatch ? invertSimdOp(baseOp) : baseOp;

			std::vector<uint64_t> matched;
			SimdScanner::compareTwoMemoryBlocks<T>(
				memBuf.data(), prevBuf.data(), toRead, chunkBase, step, op, matched);

			for (auto addr : matched) {
				batchResults.push_back({ addr });
				if (batchResults.size() >= 4096) {
					outCache->push_back_batch(batchResults);
					batchResults.clear();
				}
			}
		}
		else {
			// ===== 标量回退（IncreasedBy / DecreasedBy + 浮点 NotEqual 近似）=====
			// 虽然有虚拟调用开销，但 prevBuf 整块预读已消除逐地址调用
			T placeholderPrev;
			for (size_t off = 0; off + scalarSize <= toRead; off += step) {
				if (m_cancel.load()) break;
				uint64_t addr = chunkBase + off;
				T curVal;
				std::memcpy(&curVal, memBuf.data() + off, sizeof(T));

				bool match = false;
				switch (request.nextType) {
				case NextScanType::Equal:
					if constexpr (std::is_floating_point_v<T>) {
						match = (curVal >= v1 && curVal <= v2);
					} else {
						match = (curVal == v1);
					}
					break;
				case NextScanType::NotEqual:
					if constexpr (std::is_floating_point_v<T>) {
						match = (curVal < v1 || curVal > v2);
					} else {
						match = (curVal != v1);
					}
					break;
				case NextScanType::IncreasedBy:
					std::memcpy(&placeholderPrev, prevBuf.data() + off, sizeof(T));
					match = (curVal > placeholderPrev + v1);
					break;
				case NextScanType::DecreasedBy:
					std::memcpy(&placeholderPrev, prevBuf.data() + off, sizeof(T));
					match = (curVal < placeholderPrev - v1);
					break;
				default:
					// 安全回退
					match = false;
					break;
				}

				if (match) {
					batchResults.push_back({ addr });
					if (batchResults.size() >= 4096) {
						outCache->push_back_batch(batchResults);
						batchResults.clear();
					}
				}
			}
		}
	}

	if (!batchResults.empty()) outCache->push_back_batch(batchResults);
	m_progress.fetch_add(1);
}

void ScanEngine::performAobSearch(const std::vector<uint8_t>& buf, uint64_t base, const AobParams& p, std::vector<uint64_t>& matched) {

	if (p.pattern.empty() || buf.size() < p.pattern.size()) return;

    const size_t patLen = p.pattern.size();

    // ── 全字节匹配无通配符：用 SIMD 找首字节候选 + memcmp 验证 ──
    const bool allFullByte = std::all_of(p.mask.begin(), p.mask.end(),
        [](uint8_t m) { return m == 0xFF; });
    if (allFullByte) {
        std::vector<size_t> candidates;
        SimdScanner::findFirstChar(buf.data(), buf.size(), p.pattern[0], candidates);
        for (size_t offset : candidates) {
            if (offset + patLen <= buf.size()) {
                if (std::memcmp(buf.data() + offset, p.pattern.data(), patLen) == 0) {
                    matched.push_back(base + offset);
                }
            }
        }
        return;
    }

    // ── 有 nibble 级通配符：逐字节 nibble 比较 ──
    for (size_t i = 0; i <= buf.size() - patLen; ++i) {
        bool match = true;
        for (size_t k = 0; k < patLen; ++k) {
            const uint8_t m = p.mask[k];
            if (m == 0x00) continue;                         // "??" 完全通配，跳过
            const uint8_t cur = buf[i + k];
            const uint8_t pat = p.pattern[k];
            if (m == 0xFF) {                                 // "3E" 全字节匹配
                if (cur != pat) { match = false; break; }
            } else if (m == 0xF0) {                          // "3?" 仅高半字节
                if ((cur & 0xF0) != pat) { match = false; break; }
            } else if (m == 0x0F) {                          // "?E" 仅低半字节
                if ((cur & 0x0F) != pat) { match = false; break; }
            } else {
                // 未知掩码 — 安全回退：全字节比较
                if (cur != pat) { match = false; break; }
            }
        }
        if (match) matched.push_back(base + i);
    }
}


void ScanEngine::performStringSearch(const std::vector<uint8_t>& buf, uint64_t base,
	const StringParams& p, ScanDataType type,
	std::vector<uint64_t>& matched)
{
	if (p.text.empty() || buf.size() < p.text.length()) return;

	if (type == ScanDataType::AsciiString || type == ScanDataType::Utf8String) {
		const std::string& target = p.text;
		size_t tLen = target.length();
		if (buf.size() < tLen) return;

		// 性能优化：区分大小写时先用 SIMD 找首字节
		if (p.caseSensitive) {
			std::vector<size_t> candidates;
			SimdScanner::findFirstChar(buf.data(), buf.size(), static_cast<uint8_t>(target[0]), candidates);

			for (size_t offset : candidates) {
				if (offset + tLen <= buf.size()) {
					if (std::memcmp(buf.data() + offset, target.data(), tLen) == 0) {
						matched.push_back(base + offset);
					}
				}
			}
		}
		else {
			// 不区分大小写：逐字节比较（仅对 ASCII 范围内的字母正确，
			// 对多字节 UTF-8 非 ASCII 字符会逐字节 tolower，能满足大部分场景）
			for (size_t i = 0; i <= buf.size() - tLen; ++i) {
				bool match = true;
				for (size_t k = 0; k < tLen; ++k) {
					if (compareByteInsensitive(buf[i + k], static_cast<uint8_t>(target[k]))) {
						// 继续
					} else {
						match = false;
						break;
					}
				}
				if (match) matched.push_back(base + i);
			}
		}
	}
	else if (type == ScanDataType::Utf16String) {
		// p.text 来自 parseStringParams()，存的是 UTF-16 LE 原始字节
		// 直接将其作为 uint16_t 数组使用，无需 MultiByteToWideChar 二次转换
		size_t tBytes = p.text.length();
		if (buf.size() < tBytes || tBytes < 2 || (tBytes % 2) != 0) return;

		const uint16_t* target16 = reinterpret_cast<const uint16_t*>(p.text.data());
		size_t targetLen = tBytes / 2;

		// UTF-16 扫描按 2 字节对齐（小端序 LE）
		for (size_t i = 0; i <= buf.size() - tBytes; i += 2) {
			const uint16_t* ptr = reinterpret_cast<const uint16_t*>(buf.data() + i);
			bool match = true;
			for (size_t k = 0; k < targetLen; ++k) {
				if (p.caseSensitive) {
					if (ptr[k] != target16[k]) { match = false; break; }
				}
				else {
					if (!compareUtf16Insensitive(ptr[k], target16[k])) { match = false; break; }
				}
			}
			if (match) matched.push_back(base + i);
		}
	}
}

