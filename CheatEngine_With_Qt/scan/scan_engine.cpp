#include "scan\scan_engine.h"
#include "process\process_manager.h"
#include "utils\thread_pool.h"
#include <algorithm>


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
	case ScanDataType::Int8:    dispatchScan<int8_t>(request, prevResults, results); break;
	case ScanDataType::Int16:   dispatchScan<int16_t>(request, prevResults, results); break;
	case ScanDataType::Int32:   dispatchScan<int32_t>(request, prevResults, results); break;
	case ScanDataType::Int64:   dispatchScan<int64_t>(request, prevResults, results); break;
	case ScanDataType::Float32: dispatchScan<float>(request, prevResults, results); break;
	case ScanDataType::Float64: dispatchScan<double>(request, prevResults, results); break;
	case ScanDataType::AsciiString:
	case ScanDataType::Utf16String:
	case ScanDataType::ByteArray: dispatchScan<uint8_t>(request, prevResults, results); break;
	}
	return { results, request.dataType};
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
		m_totalItems.store(static_cast<int>(regions.size()));
		for (const auto& memory_region_section : regions) {

// #ifdef _DEBUG
// 			taskFirstScan<T>(request, memory_region_section, currentSnap, outCache);
//  // _DEBUG
// #else
			futures.push_back(GlobalThreadPool::instance().enqueue([this, request, memory_region_section, outCache] {
				taskFirstScan<T>(request, memory_region_section,  outCache);
				}));
// #endif
		}
		m_processSnapshotManager->setFirstSnapshot(currentSnap);
	}
	else {
		m_totalItems.store(static_cast<int>(prevResults.size()));
		const size_t batchSize = 4096;

		for (size_t i = 0; i < prevResults.size(); i += batchSize) {
			std::vector<ScanResult> batch;
			size_t end = (std::min)(i + batchSize, prevResults.size());
			batch.assign(prevResults.begin() + i, prevResults.begin() + end);
// #ifdef _DEBUG
// 			taskNextScan<T>(request, batch, currentSnap, prevSnap, outCache);
// #else 

			futures.push_back(GlobalThreadPool::instance().enqueue(
				[this, request, batch, currentSnap, prevSnap, outCache] {
					taskNextScan<T>(request, batch, currentSnap, prevSnap, outCache);
				}));
// #endif
		}
	}

	for (auto& fut : futures) {
		if (fut.valid()) fut.get();
	}
	m_processSnapshotManager->setPreviousSnapshot(currentSnap);
}

template <typename T>
void ScanEngine::taskFirstScan(const ScanRequest& request, MemoryRegion region,
	std::shared_ptr<AdaptiveCachePool<ScanResult>> outCache)
{
	if (m_cancel.load()) return;


	auto mem = ProcessManager::instance().memory();
	if (!mem || region.size < sizeof(T)) {
		m_progress.fetch_add(1);
		return;
	}

	const size_t step = request.alignment;
	const size_t chunkSize = 64 * 1024; // 64KB Chunk
	std::vector<uint8_t> memBuf(chunkSize + sizeof(T));
	std::vector<uint8_t> targetBuf(chunkSize + sizeof(T)); // 用于 SIMD 比较的目标填充块
	std::vector<ScanResult> batchResults;
	batchResults.reserve(2048);

	// 预解析参数
	T v1 = 0, v2 = 0;
	if (auto* p = std::get_if<ValueParams>(&request.params)) {
		v1 = static_cast<T>(p->value1);
		v2 = static_cast<T>(p->value2);
	}

	// 初始化 SIMD 目标块（将搜索值广播到整个缓冲区）
	if (request.firstType != ScanType::UnknownInitial && request.firstType != ScanType::Between) {
		for (size_t i = 0; i < targetBuf.size(); i += sizeof(T)) {
			std::memcpy(targetBuf.data() + i, &v1, sizeof(T));
		}
	}

	for (size_t baseOffset = 0; baseOffset < region.size; baseOffset += chunkSize) {
		if (m_cancel.load()) break;

		size_t toRead = std::min(chunkSize, region.size - baseOffset);
		if (!mem->read(region.base + baseOffset, memBuf.data(), toRead)) continue;

		// --- 情况 A: 未知初始值扫描 ---
		if (request.firstType == ScanType::UnknownInitial) {
			for (size_t off = 0; off + sizeof(T) <= toRead; off += step) {
				batchResults.push_back({ region.base + baseOffset + off });
				if (batchResults.size() >= 1024) {
					outCache->push_back_batch(batchResults);
					batchResults.clear();
				}
			}
		}
		// --- 情况 B: SIMD 加速路径 (Exact/Greater/Less) ---
		else if (request.firstType == ScanType::ExactValue || request.firstType == ScanType::GreaterThan || request.firstType == ScanType::LessThan) {
			SimdOp op = SimdOp::Equal;
			if (request.firstType == ScanType::GreaterThan) op = SimdOp::Greater;
			else if (request.firstType == ScanType::LessThan) op = SimdOp::Less;

			std::vector<uint64_t> matchedAddrs;
			SimdScanner::scanMemoryBlockForMatches<T>(
				memBuf.data(), targetBuf.data(), toRead,
				region.base + baseOffset, step, op, matchedAddrs);

			for (auto addr : matchedAddrs) {
				batchResults.push_back({ addr });
				if (batchResults.size() >= 1024) {
					outCache->push_back_batch(batchResults);
					batchResults.clear();
				}
			}
		}
		// --- 情况 C: 标量回退路径 (Between 等) ---
		else {
			for (size_t off = 0; off + sizeof(T) <= toRead; off += step) {
				T curVal;
				std::memcpy(&curVal, memBuf.data() + off, sizeof(T));
				if (curVal >= v1 && curVal <= v2) {
					batchResults.push_back({ region.base + baseOffset + off });
				}
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
	T v1 = p ? static_cast<T>(p->value1) : 0;
	T v2 = p ? static_cast<T>(p->value2) : 0;


	
		
	for (const auto& res : oldBatch) {
		if (m_cancel.load()) break;
		T curVal, oldVal;
		if (!currentSnapshot->readValue(res.address, curVal)) continue;

		bool match = false;
		switch (request.nextType) { // 补全所有 CE 再次扫描分支
		case NextScanType::Equal:     match = (curVal == v1); break;
		case NextScanType::NotEqual:  match = (curVal != v1); break;
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
		if (match) survivors.push_back(res);
	}
	if (!survivors.empty()) outCache->push_back_batch(survivors);
	m_progress.fetch_add(static_cast<int>(oldBatch.size()));
}


void ScanEngine::performAobSearch(const std::vector<uint8_t>& buf, uint64_t base, const AobParams& p, std::vector<uint64_t>& matched) {
	if (p.pattern.empty() || buf.size() < p.pattern.size()) return;

	for (size_t i = 0; i <= buf.size() - p.pattern.size(); ++i) {
		bool match = true;
		for (size_t k = 0; k < p.pattern.size(); ++k) {
			// 如果 mask[k] 为 true，表示该字节需要匹配；为 false 则是通配符 '?'
			if (p.mask[k] && buf[i + k] != p.pattern[k]) {
				match = false;
				break;
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

	if (type == ScanDataType::AsciiString) {
		const std::string& target = p.text;
		size_t tLen = target.length();

		// 性能优化：如果是区分大小写的搜索，先用 SIMD 找首字母
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
			// 不区分大小写：常规线性扫描
			for (size_t i = 0; i <= buf.size() - tLen; ++i) {
				bool match = true;
				for (size_t k = 0; k < tLen; ++k) {
					if (std::tolower(buf[i + k]) != std::tolower(static_cast<unsigned char>(target[k]))) {
						match = false;
						break;
					}
				}
				if (match) matched.push_back(base + i);
			}
		}
	}
	else if (type == ScanDataType::Utf16String) {
		// UTF-16 每一个字符占 2 字节。假设 p.text 是 UTF-8 编码，需先转为 UTF-16 数组对比
		std::vector<uint16_t> target16;
		for (char c : p.text) target16.push_back(static_cast<uint16_t>(static_cast<unsigned char>(c)));

		size_t tBytes = target16.size() * 2;
		if (buf.size() < tBytes) return;

		// UTF-16 扫描通常按 2 字节对齐
		for (size_t i = 0; i <= buf.size() - tBytes; i += 2) {
			const uint16_t* ptr = reinterpret_cast<const uint16_t*>(buf.data() + i);
			bool match = true;
			for (size_t k = 0; k < target16.size(); ++k) {
				if (p.caseSensitive) {
					if (ptr[k] != target16[k]) { match = false; break; }
				}
				else {
					// 使用 ::towlower 处理宽字符大小写
					if (::towlower(ptr[k]) != ::towlower(target16[k])) { match = false; break; }
				}
			}
			if (match) matched.push_back(base + i);
		}
	}
}

