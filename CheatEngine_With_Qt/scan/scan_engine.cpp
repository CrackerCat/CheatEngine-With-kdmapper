#include "scan\scan_engine.h"
#include "process\process_manager.h"
#include "utils\thread_pool.h"


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
	case ScanDataType::Structure: break;
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
	if (!mem) { m_progress.fetch_add(1); return; }

	// ── 字符串 / 字节数组 首次扫描（仅 T=uint8_t 时编译，独立 Chunk 循环）──
	if constexpr (sizeof(T) == 1) {
		if (isStringType(request.dataType) || isByteArrayType(request.dataType)) {
			if (region.size == 0) { m_progress.fetch_add(1); return; }
			// 计算最大模式长度，用于 chunk 间的重叠
			size_t maxPatternLen = 0;
			if (isStringType(request.dataType)) {
				if (auto* sp = std::get_if<StringParams>(&request.params)) {
					maxPatternLen = sp->text.length();
					if (request.dataType == ScanDataType::Utf16String) maxPatternLen *= 2;
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
				if (!mem->read(region.base + off, memBuf.data(), toRead)) continue;
				std::vector<uint64_t> hits;
				// 非首 chunk 时，只提交 chunkSize 之后的新数据（避免重复匹配 overlap 区域）
				size_t searchLen = firstChunk ? toRead : chunkSize;
				if (isStringType(request.dataType)) {
					if (auto* sp = std::get_if<StringParams>(&request.params)) {
						std::vector<uint8_t> searchView(memBuf.begin(), memBuf.begin() + toRead);
						performStringSearch(searchView, region.base + off, *sp, request.dataType, hits);
						// 去重：滤掉 overlap 区域中的匹配（只保留 searchLen 之后的）
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

	// ── 数值类型扫描（原有逻辑，完全不动）────────────────────
	if (region.size < sizeof(T)) { m_progress.fetch_add(1); return; }

	const size_t step = request.alignment;
	const size_t chunkSize = 64 * 1024; // 64KB Chunk
	std::vector<uint8_t> memBuf(chunkSize + sizeof(T));
	std::vector<uint8_t> targetBuf(chunkSize + sizeof(T)); // 用于 SIMD 比较的目标填充块
	std::vector<ScanResult> batchResults;
	batchResults.reserve(2048);

	T v1 = 0, v2 = 0;
	if (auto* p = std::get_if<ValueParams>(&request.params)) {
		v1 = static_cast<T>(p->value1);
		v2 = static_cast<T>(p->value2);
	}

	if (request.firstType != ScanType::UnknownInitial && request.firstType != ScanType::Between) {
		for (size_t i = 0; i < targetBuf.size(); i += sizeof(T))
			std::memcpy(targetBuf.data() + i, &v1, sizeof(T));
	}

	for (size_t baseOffset = 0; baseOffset < region.size; baseOffset += chunkSize) {
		if (m_cancel.load()) break;
		size_t toRead = std::min(chunkSize, region.size - baseOffset);
		if (!mem->read(region.base + baseOffset, memBuf.data(), toRead)) continue;

		if (request.firstType == ScanType::UnknownInitial) {
			for (size_t off = 0; off + sizeof(T) <= toRead; off += step) {
				batchResults.push_back({ region.base + baseOffset + off });
				if (batchResults.size() >= 1024) { outCache->push_back_batch(batchResults); batchResults.clear(); }
			}
		}
		else if (request.firstType == ScanType::ExactValue || request.firstType == ScanType::GreaterThan || request.firstType == ScanType::LessThan) {
			SimdOp op = SimdOp::Equal;
			if (request.firstType == ScanType::GreaterThan) op = SimdOp::Greater;
			else if (request.firstType == ScanType::LessThan) op = SimdOp::Less;

			std::vector<uint64_t> matchedAddrs;
			SimdScanner::scanMemoryBlockForMatches<T>(memBuf.data(), targetBuf.data(), toRead,
				region.base + baseOffset, step, op, matchedAddrs);

			for (auto addr : matchedAddrs) {
				batchResults.push_back({ addr });
				if (batchResults.size() >= 1024) { outCache->push_back_batch(batchResults); batchResults.clear(); }
			}
		}
		else {
			for (size_t off = 0; off + sizeof(T) <= toRead; off += step) {
				T curVal;
				std::memcpy(&curVal, memBuf.data() + off, sizeof(T));
				if (curVal >= v1 && curVal <= v2) {
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
	T v1 = p ? static_cast<T>(p->value1) : 0;
	T v2 = p ? static_cast<T>(p->value2) : 0;

	for (const auto& res : oldBatch) {
		if (m_cancel.load()) break;

		// ── 字符串 / 字节数组（仅 T=uint8_t 时编译，直接比较后 continue）──
		if constexpr (sizeof(T) == 1) {
			if (isStringType(request.dataType)) {
				auto* sp = std::get_if<StringParams>(&request.params);
				if (sp && !sp->text.empty()) {
					size_t len = (request.dataType == ScanDataType::Utf16String) ? sp->text.length() * 2 : sp->text.length();
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
		// 将 UTF-8 搜索字符串正确转为 UTF-16（支持中文等多字节字符）
		int wideLen = MultiByteToWideChar(CP_UTF8, 0, p.text.data(), static_cast<int>(p.text.length()), nullptr, 0);
		if (wideLen <= 0) return;
		std::vector<uint16_t> target16(wideLen);
		MultiByteToWideChar(CP_UTF8, 0, p.text.data(), static_cast<int>(p.text.length()),
			reinterpret_cast<wchar_t*>(target16.data()), wideLen);

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
					if (!compareUtf16Insensitive(ptr[k], target16[k])) { match = false; break; }
				}
			}
			if (match) matched.push_back(base + i);
		}
	}
}

