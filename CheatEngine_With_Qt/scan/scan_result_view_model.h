#pragma once
#include <QAbstractTableModel>
#include "scan_data_stream_define.h"
#include "iscan_value_provider.h"
#include <mutex>

class ScanResultRepository;

/// @brief 为 QTableView 提供数据的适配器。
///        只依赖 Repository，不处理数据存储或刷新。
class ScanResultViewModel : public QAbstractTableModel {
	Q_OBJECT
public:
    explicit ScanResultViewModel(ScanResultRepository* repo, IScanValueProvider* valueProvider, QObject* parent = nullptr);

    // --- 核心生命周期方法 ---

        // 当扫描完成或结果集完全替换时调用。此时会同步初始化所有列的缓存（Address, Prev, First）。
    void onRepositoryReplaced();

    // 由 Service 的定时器调用。负责从内存读取最新值并更新 m_cacheCurrent。
    // 建议优化：此函数可以增加参数，仅刷新当前 UI 可见的行范围。
    void refreshCurrentValues();

    // 当用户切换显示类型（如从 Int 改为 Hex）时，刷新所有缓存的字符串。
    void setDisplayType(ScanDataType type);
    ScanDataType getDisplayType() const { return m_displayType; }

    // --- QAbstractTableModel 接口重写 ---
    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    int columnCount(const QModelIndex& parent = QModelIndex()) const override;

    // data() 现在是纯内存操作，严禁包含任何 ReadProcessMemory 或文件 IO。
    QVariant data(const QModelIndex& index, int role) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role) const override;

    // 辅助工具
    uint64_t getAddress(int row) const ;

private:
    // 更新指定行的“当前值”缓存。返回 true 表示值发生了变化。
    bool updateRowCache(int row);

    // 内部方法：根据当前 m_displayType 重新填充所有缓存列。
    void rebuildAllCaches();

    // --- 外部依赖 ---
    ScanResultRepository* m_repo;          // 不拥有所有权
    IScanValueProvider* m_valueProvider; // 不拥有所有权
    ScanDataType          m_displayType = ScanDataType::Int32;

    // --- 核心缓存容器 (UI 性能的关键) ---
    // 所有的 data() 请求都直接从这些 vector 中取值，达到 O(1) 速度。
    std::vector<std::string> m_cacheAddress;  // 列 0: 地址显示字符串 (如 "module.exe+0x123")
    std::vector<std::string> m_cacheCurrent;  // 列 1: 实时内存值字符串
    std::vector<std::string> m_cachePrevious; // 列 2: 上次快照值字符串
    std::vector<std::string> m_cacheFirst;    // 列 3: 首次快照值字符串

    // 缓存模块基址标记，避免在 data() 里频繁调用 resolveAddress
    std::vector<bool>        m_cacheIsBase;

    mutable std::mutex m_mutex; // 保护缓存容器，防止异步刷新时读取冲突


	static constexpr int MAX_DISPLAY = 1000;
	static constexpr int STRING_DISPLAY_MAX = 64;   // 字符串最多显示字节数
	static constexpr int BYTEARRAY_DISPLAY_MAX = 32; // 字节数组最多显示字节数
};