// scan_service.cpp
#include "scan\scan_service.h"
#include "scan\scan_result_repository.h"
#include "scan\scan_result_view_model.h"
#include "process\process_manager.h"


ScanService::ScanService(QObject* parent)
	: QObject(parent)
	, m_processSnapshotManager(ProcessManager::instance().getProcessMemorySnapshotManager())
	, m_engine(std::make_unique<ScanEngine>(m_processSnapshotManager.get()))
	, m_repository(std::make_unique<ScanResultRepository>())
	, m_dataProvider(std::make_unique<ScanDataProvider>(m_processSnapshotManager.get(), ScanDataType::Int32))
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

	// ★ 关键：在保存"上一次扫描"快照之前，先提取再次扫描所需的当前结果
	std::vector<ScanResult> currentResults;
	if (request.mode == ScanMode::Next) {
		currentResults = m_repository->getResults();
	}

	// ★ 保存当前结果作为"上一次扫描"快照（必须在提取 currentResults 之后）
	if (m_repository && m_repository->getResultCount() > 0) {
		m_repository->saveAsPreviousResults();
	}

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

#ifdef _DEBUG
	auto pack = m_engine->execute(request, currentResults);
	QMetaObject::invokeMethod(this, [this, pack, mode = request.mode] {
		this->onScanFinished(pack, mode);
		}, Qt::QueuedConnection);
#else
	// 【修改】使用独立的线程运行 execute，不占用线程池名额
	std::thread([this, request, currentResults]() {
		auto pack = m_engine->execute(request, currentResults); // 内部会调用 dispatchScan

		// 任务结束，切回 UI 线程同步
		QMetaObject::invokeMethod(this, [this, pack, mode = request.mode] {
			this->onScanFinished(pack, mode);
			}, Qt::QueuedConnection);
		}).detach();// 这里的管理线程可以安全阻塞，因为它不属于 GlobalThreadPool,GlobalThreadPool里的线程会被计算密集的task占满，使用同一个GlobalThreadPool会发生线程池饥饿

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
	// UnknownInitial 模式（无论是否正在扫描）：认为有结果
	if (m_expectEmptyResults.load()) return true;

	// 扫描结束后，只要仓库里有地址，就认为成功
	return m_repository && m_repository->getResultCount() > 0;
}

int ScanService::totalResults() const
{
	return static_cast<int>(m_repository->getResultCount());
	//return m_engine->totalItems();
}

int ScanService::potentialAddressCount() const
{
	return m_engine ? m_engine->potentialAddressCount() : 0;
}

bool ScanService::isUnknownInitialMode() const
{
	return m_expectEmptyResults.load();
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
	// 1. 停止所有正在进行的扫描和刷新
	cancel();
	stopAutoRefresh();

	// 2. 清理核心仓库
	m_repository->clear();

	// 3. 同时清除上一次扫描快照
	m_repository->clearPreviousResults();

	// 3. 清理快照管理器（物理文件清理的核心）
	if (m_processSnapshotManager) {
		m_processSnapshotManager->clear();
	}

	// 4. 重置引擎状态（原子变量重置）
	m_engine->clear();

	// 5. 重置 UnknownInitial 标志
	m_expectEmptyResults = false;

	// 6. 通知 UI
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

	// 1. 结果分发：将池中的结果存入仓库（池 = 磁盘后备存储，不全部加载到内存）
	if (pack.results && pack.results->total_size() > 0) {
		m_repository->replaceAllResultsFromPool(pack.results);
	}

	// 如果此次扫描产生了实际地址结果，退出 UnknownInitial 模式
	if (m_repository->getResultCount() > 0) {
		m_expectEmptyResults = false;
	}

	//m_repository->setMetadata(report.metadata);
	m_dataProvider->setDisplayType(pack.dataType);
	m_viewModel->setDisplayType(pack.dataType);

	m_viewModel->onRepositoryReplaced();
	m_scanning = false;
	emit scanCompleted();
}

// ===== "上一次扫描" 快照 =====

bool ScanService::hasPreviousResults() const
{
	return m_repository && m_repository->hasPreviousResults();
}

bool ScanService::restorePreviousResults()
{
	if (!m_repository) return false;
	if (!m_repository->swapWithPrevious()) return false;

	// 清除快照管理器—恢复上一次扫描不需要匹配快照
	// 通知 ViewModel 刷新
	m_viewModel->onRepositoryReplaced();

	// 恢复后停用自动刷新并发出扫描完成信号，让 UI 更新状态
	stopAutoRefresh();
	emit scanCompleted();

	return true;
}
