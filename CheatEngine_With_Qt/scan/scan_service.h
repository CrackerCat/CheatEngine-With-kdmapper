// scan_service.h
#pragma once
#include <QObject>
#include <QTimer>
#include <atomic>
#include <memory>
#include "scan_request.h"
#include "scan_engine.h"

class ScanResultRepository;
class ScanResultViewModel;

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
    ScanResultViewModel* resultModel() const;

    // 自动刷新（现在仅负责触发 UI 重绘）
    void startAutoRefresh(int intervalMs = 200);
    void stopAutoRefresh();

    void clear();
    void reset();

signals:
    void scanCompleted();
    void progressChanged(int completed, int total);

private:
    void onScanFinished(ScanEngine::ResultPack pack, ScanMode mode);

    std::unique_ptr<ScanEngine>            m_engine;
    std::unique_ptr<ScanResultRepository>  m_repository;
    ScanResultViewModel* m_viewModel;

    QTimer* m_refreshTimer;
    QTimer* m_progressTimer = nullptr;

    std::atomic<bool> m_scanning{ false };
    std::atomic<bool> m_cancelling{ false };

    // 记录首次扫描的快照路径和索引
    std::string m_firstPath;
    std::map<uint64_t, size_t> m_firstIndex;
};