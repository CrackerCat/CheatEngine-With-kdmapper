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
		else if (!isFloatApprox && (request.firstType == ScanType::ExactValue || request.firstType == ScanType::GreaterThan || request.firstType == ScanType::LessThan)) {
			// 使用 SIMD 加速，从 snapshot 数据中批量匹配
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
		else if (request.firstType == ScanType::Between) {
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
			// 浮点数近似值 ExactValue 或其他回退：标量区间扫描
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

			SimdOp op = (request.nextType == NextScanType::Equal) ? SimdOp::Equal : SimdOp::NotEqual;
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
			if constexpr (std::is_floating_point_v<T>) {
				if (request.nextType == NextScanType::NotEqual) {
					// ★ NotEqual 近似模式：标量回退（范围取补不好 SIMD）
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
			} else {
				// 整数 Between
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
		else if (simdPath == SimdPath::CompareTwoBuffers) {
			// ★ SIMD 批量比较两个内存块（当前 vs 上次/首次快照）
			SimdOp op;
			switch (request.nextType) {
			case NextScanType::Changed:              op = SimdOp::NotEqual; break;
			case NextScanType::Unchanged:            op = SimdOp::Equal; break;
			case NextScanType::Increased:            op = SimdOp::Greater; break;
			case NextScanType::Decreased:            op = SimdOp::Less; break;
			case NextScanType::Compare_to_First_Scan: op = SimdOp::Equal; break;
			default:                                 op = SimdOp::Equal; break;
			}

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

