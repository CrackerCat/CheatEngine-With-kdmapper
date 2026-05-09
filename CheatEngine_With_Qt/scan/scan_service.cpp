// scan_service.cpp
#include "scan_service.h"
#include "scan_result_repository.h"
#include "scan_result_view_model.h"

#ifndef _DEBUG
#include "thread_pool.h"
#endif // !_DEBUG

ScanService::ScanService(QObject* parent)
	: QObject(parent)
	, m_engine(std::make_unique<ScanEngine>())
	, m_repository(std::make_unique<ScanResultRepository>())
	, m_dataProvider(std::make_unique<ScanDataProvider>(nullptr, nullptr, ScanDataType::Int32))
	, m_viewModel(std::make_unique<ScanResultViewModel>(m_repository.get(), m_dataProvider.get(), this))
	, m_refreshTimer(new QTimer(this))
{
	// 自动刷新：现在只需要让视图重新拉取内存值即可，不需要后台计算
	connect(m_refreshTimer, &QTimer::timeout, this, [this]() {
		m_viewModel->refreshCurrentValues();  // 触发重绘
		});
}

ScanService::~ScanService() = default;

// ---------------- 公有接口 ----------------

void ScanService::startScan(const ScanRequest& request) {
	if (m_scanning.exchange(true)) return;

	stopAutoRefresh();
	m_cancelling = false;

	if (request.mode == ScanMode::First) {
		// 对于未知初始值扫描，设置预期行为
		if (request.firstType == ScanType::UnknownInitial) {
			m_expectEmptyResults = true;
		}
	}


	// 进度定时器 (逻辑保持原样)
	m_progressTimer = new QTimer(this);
	connect(m_progressTimer, &QTimer::timeout, this, [this]() {
		if (!m_scanning.load()) {
			m_progressTimer->stop();
			m_progressTimer->deleteLater();
			m_progressTimer = nullptr;
			return;
		}
		emit progressChanged(m_engine->progress(), m_engine->totalItems());
		});
	m_progressTimer->start(100);

	std::vector<ScanResult> currentResults;
	if (request.mode == ScanMode::Next) {
		currentResults = m_repository->getResults();
	}

#ifdef _DEBUG
	auto pack = m_engine->execute(request, currentResults);
	QMetaObject::invokeMethod(this, [this, pack, mode = request.mode] {
		this->onScanFinished(pack, mode);
		}, Qt::QueuedConnection);
#else
	GlobalThreadPool::instance().enqueue([this, request, currentResults] {
		auto pack = m_engine->execute(request, currentResults);

		// 任务结束，切回 UI 线程同步仓库与 ViewModel
		QMetaObject::invokeMethod(this, [this, pack, mode = request.mode] {
			this->onScanFinished(pack, mode);
			}, Qt::QueuedConnection);
		});
#endif // DEBUG

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
	return m_repository->getResultCount() > 0 || (m_expectEmptyResults && m_scanning.load());
}

int ScanService::totalResults() const
{
	return static_cast<int>(m_repository->getResultCount());
	//return m_engine->totalItems();
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
	m_repository->clear();
	m_engine->clear();
	m_viewModel->onRepositoryReplaced();
}

void ScanService::reset()
{
	clear();
	m_engine->cancel();
	m_scanning = false;
	// 如果正在扫描，等待线程结束？简单处理：直接重置标志
}


void ScanService::onScanFinished(ScanEngine::ScanReport pack, ScanMode mode) {
	if (m_progressTimer) { m_progressTimer->stop(); m_progressTimer->deleteLater(); }

	// 1. 结果分发：将自适应缓存中的数据存入仓库
	if (pack.results) {
		m_repository->replaceAllResults(pack.results->readChunk(0, pack.results->total_size()));
	}

	//m_repository->setMetadata(report.metadata);
	m_dataProvider->updateSnapshots(pack.firstSnapshot, pack.previousSnapshot);
	m_dataProvider->setDisplayType(pack.dataType);
	m_viewModel->setDisplayType(pack.dataType);

	m_viewModel->onRepositoryReplaced();
	m_scanning = false;
	emit scanCompleted();
}
