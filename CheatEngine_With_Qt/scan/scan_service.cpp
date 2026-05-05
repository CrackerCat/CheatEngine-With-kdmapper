// scan_service.cpp
#include "scan_service.h"
#include "scan_result_repository.h"
#include "scan_result_view_model.h"
#include "thread_pool.h"

ScanService::ScanService(QObject* parent)
	: QObject(parent)
	, m_engine(std::make_unique<ScanEngine>())
	, m_repository(std::make_unique<ScanResultRepository>())
	, m_refreshTimer(new QTimer(this))
{
	// ViewModel 现在通过仓库进行懒加载，不直接操作 Service
	m_viewModel = new ScanResultViewModel(m_repository.get(), this);

	// 自动刷新：现在只需要让视图重新拉取内存值即可，不需要后台计算
	connect(m_refreshTimer, &QTimer::timeout, this, [this]() {
		m_viewModel->onRepositoryReplaced(); // 触发重绘
		});
}

ScanService::~ScanService() = default;

// ---------------- 公有接口 ----------------

void ScanService::startScan(const ScanRequest& request) {
	if (m_scanning.exchange(true)) return;

	stopAutoRefresh();
	m_cancelling = false;

	// 进度定时器 (逻辑保持原样)
	m_progressTimer = new QTimer(this);
	connect(m_progressTimer, &QTimer::timeout, this, [this]() {
		if (!m_scanning.load()) {
			m_progressTimer->stop();
			m_progressTimer->deleteLater();
			m_progressTimer = nullptr;
			return;
		}
		emit progressChanged(m_engine->regionsCompleted(), m_engine->totalRegions());
		});
	m_progressTimer->start(100);

	ScanRequest finalRequest = request;
	if (finalRequest.mode == ScanMode::Next) {
		finalRequest.prevResults = m_repository->createSnapshot();
	}

	GlobalThreadPool::instance().enqueue([this, finalRequest]() {
		auto pack = m_engine->execute(finalRequest);
		QMetaObject::invokeMethod(this, [this, pack = std::move(pack), mode = finalRequest.mode]() mutable {
			onScanFinished(std::move(pack), mode);
			});
		});
}

void ScanService::cancel()
{
	m_cancelling = true;
	m_engine->cancel();
	if (m_scanning.load()) {
		// 确保进度定时器停止
		if (m_progressTimer) {
			m_progressTimer->stop();
			m_progressTimer->deleteLater();
			m_progressTimer = nullptr;
		}
		m_scanning = false;
	}
	stopAutoRefresh();
}

bool ScanService::isScanning() const
{
	return m_scanning.load();
}

bool ScanService::hasResults() const
{
	return m_repository->resultCount() > 0 ;
}

int ScanService::totalResults() const
{
	return static_cast<int>(m_repository->resultCount());
}

ScanResultViewModel* ScanService::resultModel() const
{
	return m_viewModel;
}

void ScanService::startAutoRefresh(int intervalMs)
{
	if (!m_scanning.load())
		m_refreshTimer->start(intervalMs);
}

void ScanService::stopAutoRefresh()
{
	m_refreshTimer->stop();
}

void ScanService::clear()
{
	stopAutoRefresh();
	m_repository->replaceAllResults({});
	m_viewModel->onRepositoryReplaced();
}

void ScanService::reset()
{
	clear();
	m_engine->cancel();
	m_scanning = false;
	// 如果正在扫描，等待线程结束？简单处理：直接重置标志
}

// ---------------- 内部实现 ----------------

void ScanService::onScanFinished(ScanEngine::ResultPack pack, ScanMode mode)
{
	// 进度定时器自毁
	if (m_progressTimer) {
		m_progressTimer->stop();
		m_progressTimer->deleteLater();
		m_progressTimer = nullptr;
	}

	if (m_cancelling.load() || m_engine->isCancelled()) {
		m_scanning = false;
		m_cancelling = false;
		return;
	}

	GlobalThreadPool::instance().enqueue([this, pack = std::move(pack), mode]() mutable {
		std::vector<ScanResult> allResults;
		if (pack.results) {
			// 1. 设置结果硬上限，防止 UI 渲染过载
			size_t total = pack.results->total_size();
			allResults.reserve(total);

			// 2. 只读取地址列表 (ScanResult 只有 address 字段)
			auto chunk = pack.results->readChunk(0, total);
			allResults = std::move(chunk);
			pack.results->clear();
		}

		// 3. 处理快照逻辑：首次扫描时备份路径，再次扫描时滚动路径
		if (mode == ScanMode::First) {
			m_firstPath = m_engine->getSnapshotPath();
			m_firstIndex = m_engine->getSnapshotIndex();
		}

		// 4. 回到 UI 线程同步仓库状态
		QMetaObject::invokeMethod(this, [this, results = std::move(allResults), pack_type = pack.dataType]() mutable {
			// 同步快照路径信息到仓库，供 ViewModel 懒加载读取
			m_repository->setSnapshotInfo(
				m_firstPath, m_firstIndex,               // 首次快照
				m_engine->getSnapshotPath(), m_engine->getSnapshotIndex() // 当前快照作为下一次的 Prev
			);

			m_repository->replaceAllResults(std::move(results)); // O(1) 移动
			m_viewModel->setDisplayType(pack_type);
			m_viewModel->onRepositoryReplaced();

			m_scanning = false;
			emit scanCompleted();
			});
		});
}