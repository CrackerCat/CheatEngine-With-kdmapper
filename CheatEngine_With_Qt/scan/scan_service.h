
#pragma once
#include "scan\scan_result_view_model.h"
#include "scan\scan_data_stream_define.h"
#include "scan\scan_data_provider.h"
#include "scan\scan_result_repository.h"
#include "process\process_memory_snapshot_manager.h"
#include "scan\scan_engine.h"

#include <QObject>
#include <QTimer>
#include <atomic>
#include <memory>


class ScanService : public QObject {
	Q_OBJECT
public:
	explicit ScanService(QObject* parent = nullptr);
	~ScanService() override;

	// 扫描控制
	void startScan(const ScanRequest& request);
	void cancel();

	// 状态与结果
	bool isScanning() const;
	bool hasResults() const;
	int  totalResults() const;

	/// 获取潜在地址总数（仅用于 UnknownInitial 首次扫描，节省内存）
	int  potentialAddressCount() const;
	/// 是否为 UnknownInitial 扫描模式
	bool isUnknownInitialMode() const;

	ScanResultViewModel* getResultViewModel() const { return m_viewModel.get(); }
	ScanDataProvider* getDataProvider() const { return m_dataProvider.get(); }

	// 自动刷新（现在仅负责触发 UI 重绘）
	void startAutoRefresh(int intervalMs = 200);
	void stopAutoRefresh();

	void clear();
	void reset();

	// ===== "上一次扫描" 快照 =====
	bool hasPreviousResults() const;
	bool restorePreviousResults();

signals:
	void scanCompleted();
	void progressChanged(int completed, int total);

private:
	void onScanFinished(ScanEngine::ScanReport pack, ScanMode mode);

	std::shared_ptr<ProcessMemorySnapshotManager>             m_processSnapshotManager;
	std::unique_ptr<ScanDataProvider>         m_dataProvider;
	std::unique_ptr<ScanEngine>               m_engine;
	std::unique_ptr<ScanResultRepository>     m_repository;
	std::unique_ptr<ScanResultViewModel>      m_viewModel;
	

	QTimer* m_refreshTimer;
	QTimer* m_progressTimer = nullptr;

	std::atomic<bool> m_scanning{ false };
	std::atomic<bool> m_cancelling{ false };

	std::atomic<bool> m_expectEmptyResults{ false }; //未知的初始值使用


	// 记录首次扫描的快照路径和索引
	std::string m_firstPath;
	std::map<uint64_t, size_t> m_firstIndex;
};