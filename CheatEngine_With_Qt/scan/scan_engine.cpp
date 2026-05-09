#include "scan_engine.h"
#include "process_manager.h"

#ifndef _DEBUG
#include "thread_pool.h"
#endif // _DEBUG


#include <algorithm>
#include <cwctype>   // 必须包含，用于 towlower
#include <cctype>    // 用于 tolower (处理 ASCII)

ScanEngine::ScanEngine() : m_snapshotMgr(std::make_unique<SnapshotManager>()) {}

ScanEngine::ScanReport ScanEngine::execute(const ScanRequest& req, const std::vector<ScanResult>& prevResults) {
	m_cancel.store(false);
	m_progress.store(0);
	auto results = std::make_shared<AdaptiveCachePool<ScanResult>>(THEAD_LOCAL_SIZE);

	// 适配全部数据类型
	switch (req.dataType) {
	case ScanDataType::Int8:    dispatchScan<int8_t>(req, prevResults, results); break;
	case ScanDataType::Int16:   dispatchScan<int16_t>(req, prevResults, results); break;
	case ScanDataType::Int32:   dispatchScan<int32_t>(req, prevResults, results); break;
	case ScanDataType::Int64:   dispatchScan<int64_t>(req, prevResults, results); break;
	case ScanDataType::Float32: dispatchScan<float>(req, prevResults, results); break;
	case ScanDataType::Float64: dispatchScan<double>(req, prevResults, results); break;
	case ScanDataType::AsciiString:
	case ScanDataType::Utf16String:
	case ScanDataType::ByteArray: dispatchScan<uint8_t>(req, prevResults, results); break;
	}
	return { results, req.dataType,getFirstSnapshot(),getPreviousSnapshot() };
}

template <typename T>
void ScanEngine::dispatchScan(const ScanRequest& req, const std::vector<ScanResult>& prevResults,
	std::shared_ptr<AdaptiveCachePool<ScanResult>> outCache)
{
	auto regions = ProcessManager::instance().getMemoryRegions();
	auto currentSnap = std::shared_ptr<ScanSnapshot>(m_snapshotMgr->createSnapshot(regions));
	auto prevSnap = m_snapshotMgr->getPrevious();

	if (req.mode == ScanMode::First) {


		if (req.firstType == ScanType::UnknownInitial) {
			// 对于未知初始值扫描，扫描项数是内存区域的数量
			size_t totalItems = 0;
			totalItems = regions.size();
			for (const auto& reg : regions) {
				// 根据对齐方式计算该区域内的地址数量
				if (reg.size >= req.alignment) {
					totalItems += (reg.size - (reg.size % req.alignment)) / req.alignment;
				}
			}
			m_totalItems.store(static_cast<int>(totalItems));
			return;
		}



		m_totalItems.store(static_cast<int>(regions.size()));
		for (const auto& reg : regions) {
#ifdef _DEBUG
			taskFirstScan<T>(req, reg, currentSnap, outCache);
#else
			GlobalThreadPool::instance().enqueue([this, &req, reg, currentSnap, outCache] {
				taskFirstScan<T>(req, reg, currentSnap, outCache);
				});
#endif // DEBUG
		}
		m_snapshotMgr->setFirstSnapshot(currentSnap);
	}
	else {
		m_totalItems.store(static_cast<int>(prevResults.size()));
		const size_t batchSize = 4096;
		for (size_t i = 0; i < prevResults.size(); i += batchSize) {
			std::vector<ScanResult> batch;
			size_t end = (std::min)(i + batchSize, prevResults.size());
			batch.assign(prevResults.begin() + i, prevResults.begin() + end);
#ifdef _DEBUG
			taskNextScan<T>(req, batch, currentSnap, prevSnap, outCache);
#else
			GlobalThreadPool::instance().enqueue([this, &req, batch, currentSnap, prevSnap, outCache] {
				taskNextScan<T>(req, batch, currentSnap, prevSnap, outCache);
				});
#endif
		}
		m_snapshotMgr->setPreviousSnapshot(currentSnap);
	}

}

template <typename T>
void ScanEngine::taskFirstScan(const ScanRequest& req, MemoryRegion region,
	std::shared_ptr<ScanSnapshot> current,
	std::shared_ptr<AdaptiveCachePool<ScanResult>> outCache)
{
	if (m_cancel.load()) return;
	if (req.firstType == ScanType::UnknownInitial) { m_progress.fetch_add(1); return; }

	std::vector<uint8_t> buffer(region.size);
	if (!current->readData(region.base, buffer.data(), region.size)) { m_progress.fetch_add(1); return; }

	std::vector<uint64_t> matched;

	// 分流处理：数值类型(SIMD/Between) vs 特殊类型
	if constexpr (std::is_arithmetic_v<T> && !std::is_same_v<T, uint8_t>) {
		auto* p = std::get_if<ValueParams>(&req.params);
		if (!p) return;

		if (req.firstType == ScanType::Between) {
			T v1 = static_cast<T>(p->value1), v2 = static_cast<T>(p->value2);
			for (size_t i = 0; i + sizeof(T) <= region.size; i += req.alignment) {
				T cur; std::memcpy(&cur, buffer.data() + i, sizeof(T));
				if (cur >= v1 && cur <= v2) matched.push_back(region.base + i);
			}
		}
		else {
			// 使用 SIMD 加速 Exact/Greater/Less
			SimdOp op = (req.firstType == ScanType::GreaterThan) ? SimdOp::Greater :
				(req.firstType == ScanType::LessThan) ? SimdOp::Less : SimdOp::Equal;

			std::vector<uint8_t> tgtBuf(region.size);
			T val = static_cast<T>(p->value1);
			T* pTgt = reinterpret_cast<T*>(tgtBuf.data());
			std::fill(pTgt, pTgt + (region.size / sizeof(T)), val);

			SimdScanner::scanMemoryBlockForMatches<T>(buffer.data(), tgtBuf.data(), region.size, region.base, req.alignment, op, matched);
		}
	}
	else {
		// AOB 或 字符串扫描
		if (req.dataType == ScanDataType::ByteArray) {
			if (auto* aob = std::get_if<AobParams>(&req.params)) performAobSearch(buffer, region.base, *aob, matched);
		}
		else {
			if (auto* s = std::get_if<StringParams>(&req.params)) performStringSearch(buffer, region.base, *s, req.dataType, matched);
		}
	}

	if (!matched.empty()) {
		std::vector<ScanResult> batch;
		for (auto a : matched) batch.push_back({ a });
		outCache->push_back_batch(batch); // TLS 优化写入
	}
	m_progress.fetch_add(1);
}

template <typename T>
void ScanEngine::taskNextScan(const ScanRequest& req, const std::vector<ScanResult>& oldBatch,
	std::shared_ptr<ScanSnapshot> current,
	std::shared_ptr<ScanSnapshot> previous,
	std::shared_ptr<AdaptiveCachePool<ScanResult>> outCache)
{
	std::vector<ScanResult> survivors;
	auto* p = std::get_if<ValueParams>(&req.params);
	T v1 = p ? static_cast<T>(p->value1) : 0;
	T v2 = p ? static_cast<T>(p->value2) : 0;

	for (const auto& res : oldBatch) {
		if (m_cancel.load()) break;
		T curVal, oldVal;
		if (!current->readValue(res.address, curVal)) continue;

		bool match = false;
		switch (req.nextType) { // 补全所有 CE 再次扫描分支
		case NextScanType::Equal:     match = (curVal == v1); break;
		case NextScanType::NotEqual:  match = (curVal != v1); break;
		case NextScanType::Increased: if (previous && previous->readValue(res.address, oldVal)) match = (curVal > oldVal); break;
		case NextScanType::Decreased: if (previous && previous->readValue(res.address, oldVal)) match = (curVal < oldVal); break;
		case NextScanType::Changed:   if (previous && previous->readValue(res.address, oldVal)) match = (curVal != oldVal); break;
		case NextScanType::Unchanged: if (previous && previous->readValue(res.address, oldVal)) match = (curVal == oldVal); break;
		case NextScanType::Between:   match = (curVal >= v1 && curVal <= v2); break;
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


inline bool compareByteInsensitive(uint8_t a, uint8_t b) {
	return std::tolower(static_cast<int>(a)) == std::tolower(static_cast<int>(b));
}

// 辅助：UTF-16 不区分大小写比较（简化版，仅处理基础平面字符）
inline bool compareUtf16Insensitive(uint16_t a, uint16_t b) {
	return std::towlower(a) == std::towlower(b);
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
