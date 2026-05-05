// scan_result_view_model.cpp
#include "scan_result_view_model.h"
#include "scan_result_repository.h"
#include "scan_result_formatter.h"
#include "process_manager.h"
#include <QBrush>
#include <QColor>
#include <QString>

ScanResultViewModel::ScanResultViewModel(ScanResultRepository* repo, QObject* parent)
    : QAbstractTableModel(parent)
    , m_repo(repo)
{
    // repository 的生命周期由 ScanManager 管理，这里仅保存裸指针
}

void ScanResultViewModel::onRepositoryReplaced()
{
    beginResetModel();
    endResetModel();
}

void ScanResultViewModel::onDeltaApplied(int minRow, int maxRow)
{
    // 只通知 Value / Previous / First 列变化（地址列不更新）
    if (minRow <= maxRow) {
        emit dataChanged(index(minRow, 1), index(maxRow, 3), { Qt::DisplayRole });
    }
}

void ScanResultViewModel::setDisplayType(ScanDataType type)
{
    m_displayType = type;
    // 注意：调用方应在需要时手动触发 onRepositoryReplaced() 以刷新显示
}

ScanDataType ScanResultViewModel::displayType() const
{
    return m_displayType;
}

int ScanResultViewModel::rowCount(const QModelIndex& parent) const
{
    if (parent.isValid()) return 0;
    if (!m_repo) return 0;
    return std::min(static_cast<int>(m_repo->resultCount()), MAX_DISPLAY);
}

int ScanResultViewModel::columnCount(const QModelIndex& parent) const
{
    if (parent.isValid()) return 0;
    return 4;
}


QVariant ScanResultViewModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || !m_repo) return {};

    int row = index.row();
    const ScanResult* item = m_repo->resultAt(row);
    if (!item) return {};

    uint64_t addr = item->address;

    // 1. 处理地址列及基址颜色 (Column 0)
    if (index.column() == 0) {
        std::string display;
        bool isBase = false;
        ProcessManager::instance().resolveAddress(addr, display, isBase);

        if (role == Qt::DisplayRole) return QString::fromStdString(display);
        if (role == Qt::ForegroundRole && isBase) return QBrush(QColor(0, 128, 0));
        return {};
    }

    // 2. 处理文本显示 (DisplayRole) - 懒加载核心
    if (role == Qt::DisplayRole) {
        // 调用 Repository 封装好的获取逻辑，ViewModel 不关心数据是来自内存还是磁盘
        return QString::fromStdString(m_repo->getDisplayValue(addr, index.column(), m_displayType));
    }

    // 3. 处理变红逻辑 (ForegroundRole)
    if (role == Qt::ForegroundRole && index.column() == 1) {
        // 如果当前值与上次扫描值不一致，则显示红色
        // 这里的对比同样是懒加载的：实时读内存 vs 读快照文件
        std::string curVal = m_repo->getDisplayValue(addr, 1, m_displayType);
        std::string prevVal = m_repo->getDisplayValue(addr, 2, m_displayType);

        if (curVal != prevVal && prevVal != "---") {
            return QBrush(Qt::red);
        }
    }

    return {};
}


QVariant ScanResultViewModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (role != Qt::DisplayRole || orientation != Qt::Horizontal)
        return {};
    switch (section) {
    case 0: return QStringLiteral("Address");
    case 1: return QStringLiteral("Value");
    case 2: return QStringLiteral("Previous");
    case 3: return QStringLiteral("First");
    default: return {};
    }
}

uint64_t ScanResultViewModel::getAddress(int row) const
{
    return m_repo ? m_repo->addressAtIndex(row) : 0;
}