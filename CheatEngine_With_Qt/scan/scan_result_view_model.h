#pragma once
#include "interface/iscan_value_provider.h"
#include "scan/scan_data_stream_define.h"
#include <QAbstractTableModel>
#include <mutex>
#include <utility> // pair
#include <vector>


class ScanResultRepository;

/// @brief 为 QTableView 提供数据的适配器。
///        只依赖 Repository，不处理数据存储或刷新。
class ScanResultViewModel : public QAbstractTableModel {
  Q_OBJECT
public:
  explicit ScanResultViewModel(ScanResultRepository *repo,
                               IScanValueProvider *valueProvider,
                               QObject *parent = nullptr);

  // --- 核心生命周期方法 ---

  // 当扫描完成或结果集完全替换时调用。此时会同步初始化所有列的缓存（Address,
  // Prev, First）。
  void onRepositoryReplaced();

  // 由 Service 的定时器调用。负责从内存读取最新值并更新 m_cacheCurrent。
  void refreshCurrentValues();

  // 当用户切换显示类型时，刷新所有缓存的字符串。
  void setDisplayType(ScanDataType type);
  ScanDataType getDisplayType() const { return m_displayType; }

  // ★ Hex 显示模式控制（转发到 provider 并重建缓存）
  void setHexDisplay(bool on);
  bool isHexDisplay() const {
    return m_valueProvider ? m_valueProvider->isHexDisplay() : false;
  }

  // ---- 模块过滤 ----
  /// @brief 设置模块地址范围过滤，仅显示该范围内的地址
  void setModuleFilter(uint64_t base, uint64_t size);
  /// @brief 清除模块过滤，显示所有结果
  void clearModuleFilter();
  /// @brief 获取当前过滤范围 (base, size)，base=0 表示无过滤
  std::pair<uint64_t, uint64_t> getModuleFilter() const {
    return {m_filterModuleBase, m_filterModuleSize};
  }

  // --- QAbstractTableModel 接口重写 ---
  int rowCount(const QModelIndex &parent = QModelIndex()) const override;
  int columnCount(const QModelIndex &parent = QModelIndex()) const override;

  // data() 现在是纯内存操作，严禁包含任何 ReadProcessMemory 或文件 IO。
  QVariant data(const QModelIndex &index, int role) const override;
  QVariant headerData(int section, Qt::Orientation orientation,
                      int role) const override;

  // 辅助工具
  uint64_t getAddress(int row) const;

  /// 获取指定行地址的实际匹配类型（All 模式时返回每地址独有类型）
  ScanDataType getMatchedType(int row) const;

signals:
  /// @brief 当过滤结果总数变化时发出（用于更新显示标签）
  void filteredCountChanged(int shown, int total);

private:
  // 更新指定行的"当前值"缓存。返回 true 表示值发生了变化。
  bool updateRowCache(int row);

  // 内部方法：根据当前 m_displayType 和模块过滤重新填充所有缓存列。
  void rebuildAllCaches();
  /// @brief 重建过滤索引（根据 m_filterModuleBase/Size 过滤所有结果）
  void rebuildFilteredIndices();

  // --- 外部依赖 ---
  ScanResultRepository *m_repo;        // 不拥有所有权
  IScanValueProvider *m_valueProvider; // 不拥有所有权
  ScanDataType m_displayType = ScanDataType::Int32;

  // --- 模块过滤 ---
  uint64_t m_filterModuleBase = 0;
  uint64_t m_filterModuleSize = 0;
  /// 过滤后的索引映射：m_filteredIndices[显示行] = 仓库中的真实索引
  std::vector<size_t> m_filteredIndices;

  // --- 核心缓存容器 (UI 性能的关键) ---
  struct RowUiCache {
    std::string address;
    std::string typeStr; // <--- 新增：保存具体匹配类型的字符串
    std::string current;
    std::string previous;
    std::string first;
    bool isBase = false;
  };
  std::vector<RowUiCache> m_rowCaches;
  // 缓存模块基址标记，避免在 data() 里频繁调用 resolveAddress
  std::vector<bool> m_cacheIsBase;

  mutable std::mutex m_mutex; // 保护缓存容器，防止异步刷新时读取冲突

  static constexpr int MAX_DISPLAY = 1000;
  static constexpr int STRING_DISPLAY_MAX = 64;    // 字符串最多显示字节数
  static constexpr int BYTEARRAY_DISPLAY_MAX = 32; // 字节数组最多显示字节数
};
