#include "scan\scan_result_view_model.h"
#include "scan\scan_result_repository.h"
#include "utils\encoding_formatter.h"
#include <QBrush>
#include <QColor>
#include <QString>

ScanResultViewModel::ScanResultViewModel(ScanResultRepository *repo,
                                         IScanValueProvider *valueProvider,
                                         QObject *parent)
    : QAbstractTableModel(parent), m_repo(repo),
      m_valueProvider(valueProvider) {}

void ScanResultViewModel::rebuildFilteredIndices() {
  if (!m_repo)
    return;
  size_t total = m_repo->getResultCount();

  m_filteredIndices.clear();

  // ★ 使用批量读取代替单次逐行读取，池模式下大幅减少磁盘 I/O
  constexpr size_t BATCH_SIZE = 4096;

  if (m_filterModuleBase != 0 && m_filterModuleSize > 0) {
    uint64_t filterEnd = m_filterModuleBase + m_filterModuleSize;

    // 分批读取地址并过滤
    for (size_t batchStart = 0; batchStart < total; batchStart += BATCH_SIZE) {
      size_t batchCount = std::min(BATCH_SIZE, total - batchStart);
      auto addresses = m_repo->readAddressRange(batchStart, batchCount);
      for (size_t j = 0; j < addresses.size(); ++j) {
        if (addresses[j] >= m_filterModuleBase && addresses[j] < filterEnd) {
          m_filteredIndices.push_back(batchStart + j);
        }
      }
    }
  } else {
    // 无过滤：建立全量索引（池模式下只保留前 MAX_DISPLAY 个以避免内存撑爆）
    if (total > static_cast<size_t>(MAX_DISPLAY)) {
      // ★ 海量结果无过滤时，只保留前 MAX_DISPLAY 个
      m_filteredIndices.reserve(MAX_DISPLAY);
      for (int i = 0; i < MAX_DISPLAY; ++i) {
        m_filteredIndices.push_back(static_cast<size_t>(i));
      }
    } else {
      m_filteredIndices.reserve(total);
      for (size_t i = 0; i < total; ++i) {
        m_filteredIndices.push_back(i);
      }
    }
  }
}

void ScanResultViewModel::rebuildAllCaches() {
  if (!m_repo)
    return;

  // 1. 加锁保护缓存容器，防止定时刷新线程冲突
  std::lock_guard<std::mutex> lock(m_mutex);

  // 2. 重建过滤索引
  rebuildFilteredIndices();

  // 3. 确定显示行数，防止大数据量撑爆内存
  size_t count =
      std::min(m_filteredIndices.size(), static_cast<size_t>(MAX_DISPLAY));

  // 4. 预分配所有容器空间
  m_rowCaches.clear();
  m_rowCaches.reserve(count); // 一次分配

  // 5. 一次性填充静态值（这些值在本次扫描会话中不会改变）
  for (size_t i = 0; i < count; ++i) {
    size_t realIdx = m_filteredIndices[i];
    uint64_t addr = m_repo->getAddressAtIndex(realIdx);

    // ★ 修复：All 模式下解析每个地址的实际匹配类型
    ScanDataType effectiveType = m_displayType;
    if (effectiveType == ScanDataType::All) {
      effectiveType = m_repo->getMatchedTypeAtIndex(realIdx);
    }

    RowUiCache row;
    row.address = m_valueProvider->getAddressDisplay(addr);
    row.isBase = m_valueProvider->isModuleBase(addr);
    row.typeStr = scanDataTypeToString(effectiveType);
    row.previous = m_valueProvider->getPreviousValue(addr, effectiveType);
    row.first = m_valueProvider->getFirstValue(addr, effectiveType);
    row.current = m_valueProvider->getCurrentValue(addr, effectiveType);

    m_rowCaches.push_back(std::move(row));
  }

  // 6. 通知过滤计数变化（用于更新 UI 标签）
  int totalCount = static_cast<int>(m_filteredIndices.size());
  emit filteredCountChanged(static_cast<int>(count), totalCount);
}

void ScanResultViewModel::setHexDisplay(bool on) {
  if (!m_valueProvider)
    return;
  if (m_valueProvider->isHexDisplay() == on)
    return;
  m_valueProvider->setHexDisplay(on);
  rebuildAllCaches();
  beginResetModel();
  endResetModel();
}

void ScanResultViewModel::setDisplayType(ScanDataType type) {
  if (m_displayType == type)
    return; // 无变化无需更新
  m_displayType = type;
  rebuildAllCaches();
  beginResetModel();
  endResetModel();
}

void ScanResultViewModel::onRepositoryReplaced() {
  if (!m_repo)
    return;
  rebuildAllCaches();
  beginResetModel();
  endResetModel();
}

// ---- 模块过滤 ----

void ScanResultViewModel::setModuleFilter(uint64_t base, uint64_t size) {
  m_filterModuleBase = base;
  m_filterModuleSize = size;
  rebuildAllCaches();
  beginResetModel();
  endResetModel();
}

void ScanResultViewModel::clearModuleFilter() { setModuleFilter(0, 0); }

bool ScanResultViewModel::updateRowCache(int row) {
  // 边界检查
  if (row < 0 || row >= static_cast<int>(m_rowCaches.size()))
    return false;

  size_t realIdx = m_filteredIndices[row];
  uint64_t addr = m_repo->getAddressAtIndex(realIdx);

  // ★ 同样支持 All 扫描模式下的动态类型获取
  ScanDataType effectiveType = m_displayType;
  if (effectiveType == ScanDataType::All) {
    effectiveType = m_repo->getMatchedTypeAtIndex(realIdx);
  }

  // 从目标进程实时读取内存（涉及系统调用）
  std::string newVal = m_valueProvider->getCurrentValue(addr, effectiveType);

  // 只有当字符串发生变化时才更新，减少 UI 刷新压力
  if (newVal != m_rowCaches[row].current) {
    m_rowCaches[row].current = std::move(newVal);
    return true;
  }
  return false;
}

void ScanResultViewModel::refreshCurrentValues() {
  if (!m_repo || m_rowCaches.empty())
    return;

  int firstChanged = -1;
  int lastChanged = -1;

  // 遍历已缓存的行
  for (size_t i = 0; i < m_rowCaches.size(); ++i) {
    if (updateRowCache(static_cast<int>(i))) {
      if (firstChanged == -1)
        firstChanged = static_cast<int>(i);
      lastChanged = static_cast<int>(i);
    }
  }

  // 如果有数值变动，通知视图刷新"Value"列（列索引 1）
  if (firstChanged != -1) {
    emit dataChanged(index(firstChanged, 2), index(lastChanged, 2),
                     {Qt::DisplayRole, Qt::ForegroundRole}
                     // 也要更新颜色角色以触发红色高亮
    );
  }
}

int ScanResultViewModel::rowCount(const QModelIndex &parent) const {
  if (parent.isValid())
    return 0;
  if (!m_repo)
    return 0;
  return std::min(static_cast<int>(m_filteredIndices.size()), MAX_DISPLAY);
}

int ScanResultViewModel::columnCount(const QModelIndex &parent) const {
  if (parent.isValid())
    return 0;
  return 5;
}

QVariant ScanResultViewModel::data(const QModelIndex &index, int role) const {
  if (!index.isValid() || index.row() >= (int)m_rowCaches.size())
    return {};

  const auto &row = m_rowCaches[index.row()];
  int col = index.column();

  if (role == Qt::DisplayRole) {
    switch (col) {
    case 0:
      return QString::fromStdString(row.address);
    case 1:
      return QString::fromStdString(row.typeStr);
    case 2:
      return QString::fromStdString(row.current);
    case 3:
      return QString::fromStdString(row.previous);
    case 4:
      return QString::fromStdString(row.first);
    }
  } else if (role == Qt::ForegroundRole) {
    if (col == 0) {
      // 这里可以通过之前缓存的标记来判断，避免调用 resolveAddress
      uint64_t addr = m_repo->getAddressAtIndex(m_filteredIndices[index.row()]);
      if (m_valueProvider->isModuleBase(addr))
        return QBrush(Qt::green);
    } else if (col == 2) {
      // 直接比较内存缓存字符串，极快
      if (row.current != row.previous && row.previous != "---") {
        return QBrush(Qt::red);
      }
    }
  }
  return {};
}

QVariant ScanResultViewModel::headerData(int section,
                                         Qt::Orientation orientation,
                                         int role) const {
  if (role != Qt::DisplayRole || orientation != Qt::Horizontal)
    return {};
  switch (section) {
  case 0:
    return QStringLiteral("Address");
  case 1:
    return QStringLiteral("Type");
  case 2:
    return QStringLiteral("Value");
  case 3:
    return QStringLiteral("Previous");
  case 4:
    return QStringLiteral("First");
  default:
    return {};
  }
}

uint64_t ScanResultViewModel::getAddress(int row) const {
  if (!m_repo || row < 0 || row >= static_cast<int>(m_filteredIndices.size()))
    return 0;
  return m_repo->getAddressAtIndex(m_filteredIndices[row]);
}

ScanDataType ScanResultViewModel::getMatchedType(int row) const {
  if (!m_repo || row < 0 || row >= static_cast<int>(m_filteredIndices.size()))
    return ScanDataType::Int32;
  size_t realIdx = m_filteredIndices[row];

  // 如果是 All 模式，读取每个地址实际匹配的类型
  if (m_displayType == ScanDataType::All) {
    return m_repo->getMatchedTypeAtIndex(realIdx);
  }
  return m_displayType;
}
