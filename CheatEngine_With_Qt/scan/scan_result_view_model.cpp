#include "scan_result_view_model.h"
#include "scan_result_repository.h"
#include "scan_result_formatter.h"
#include "scan_snapshot.h"
#include <QBrush>
#include <QColor>
#include <QString>



ScanResultViewModel::ScanResultViewModel(ScanResultRepository* repo, IValueProvider* valueProvider, QObject* parent)
	: QAbstractTableModel(parent)
	, m_repo(repo)
	, m_valueProvider(valueProvider)
{

}

void ScanResultViewModel::onRepositoryReplaced()
{
	if (!m_repo) return;
	beginResetModel();
	m_rowCurrentValues.clear();        // 清空缓存
	endResetModel();
}


void ScanResultViewModel::refreshCurrentValues() {
	if (!m_repo) return;
	size_t count = m_repo->getResultCount();
	if (count == 0) return;

	ensureCacheSize();

	int firstChanged = -1, lastChanged = -1;
	for (size_t i = 0; i < count; ++i) {
		if (updateRowCache(static_cast<int>(i))) {
			if (firstChanged == -1) firstChanged = static_cast<int>(i);
			lastChanged = static_cast<int>(i);
		}
	}
	if (firstChanged != -1) {
		// 只更新当前值列（列1），也可以更新列2/3（但列2/3是快照值，不会自动变，所以只需要列1）
		emit dataChanged(index(firstChanged, 1), index(lastChanged, 1), { Qt::DisplayRole });
	}
}

void ScanResultViewModel::ensureCacheSize() {
	size_t count = m_repo->getResultCount();
	if (m_rowCurrentValues.size() != count) {
		m_rowCurrentValues.resize(count);
		// 重新填充所有缓存值
		for (size_t i = 0; i < count; ++i) {
			uint64_t addr = m_repo->getAddressAtIndex(i);
			m_rowCurrentValues[i] = m_valueProvider->getCurrentValue(addr, m_displayType);
		}
	}
}



void ScanResultViewModel::notifyRowsModified(int firstRow, int lastRow) {
	if (firstRow > lastRow || firstRow < 0) return;
	// 更新缓存并发射 dataChanged 信号（列1~3，取决于需求）
	for (int r = firstRow; r <= lastRow; ++r) {
		updateRowCache(r);
	}
	emit dataChanged(index(firstRow, 1), index(lastRow, 3), { Qt::DisplayRole });
}

void ScanResultViewModel::setDisplayType(ScanDataType type) {
	if (m_displayType == type) return;
	m_displayType = type;
	// 显示类型变化，需要重新计算所有当前值的显示字符串，并刷新视图
	ensureCacheSize();  // 强制重建缓存（用新类型）
	if (rowCount() > 0)
		emit dataChanged(index(0, 1), index(rowCount() - 1, 1), { Qt::DisplayRole });
}


int ScanResultViewModel::rowCount(const QModelIndex& parent) const
{
	if (parent.isValid()) return 0;
	if (!m_repo) return 0;
	return std::min(static_cast<int>(m_repo->getResultCount()), MAX_DISPLAY);
}

int ScanResultViewModel::columnCount(const QModelIndex& parent) const
{
	if (parent.isValid()) return 0;
	return 4;
}


QVariant ScanResultViewModel::data(const QModelIndex& index, int role) const {
	if (!index.isValid()) return {};
	int row = index.row();
	int column = index.column();
	if (row >= static_cast<int>(m_repo->getResultCount())) return {};

	uint64_t addr = m_repo->getAddressAtIndex(row);

	// 处理地址列（column 0）的显示及前景色
	if (column == 0) {
		if (role == Qt::DisplayRole) {
			return QString::fromStdString(m_rowCurrentValues[row]);
		}
		else if (role == Qt::ForegroundRole) {
			if (m_valueProvider->isModuleBase(addr))
				return QBrush(Qt::green);
			return QBrush(); // 默认颜色
		}
		return {};
	}

	// 处理值列（column 1,2,3）的显示
	if (role == Qt::DisplayRole) {
		if (column == 1) {
			std::string val = m_valueProvider->getCurrentValue(addr, m_displayType);
			return QString::fromStdString(val);
		}
		else if (column == 2) {
			std::string val = m_valueProvider->getPreviousValue(addr, m_displayType);
			return QString::fromStdString(val);
		}
		else if (column == 3) {
			std::string val = m_valueProvider->getFirstValue(addr, m_displayType);
			return QString::fromStdString(val);
		}
		return {};
	}
	else if (role == Qt::ForegroundRole && column == 1) {
		// 当前值列：若当前值 ≠ 上一次值 且 上一次值不是 "---"，则显示红色
		std::string cur = m_valueProvider->getCurrentValue(addr, m_displayType);
		std::string prev = m_valueProvider->getPreviousValue(addr, m_displayType);
		if (cur != prev && prev != "---") {
			return QBrush(Qt::red);
		}
		return QBrush();
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
	return m_repo ? m_repo->getAddressAtIndex(row) : 0;
}


bool  ScanResultViewModel::updateRowCache(int row)
{
	uint64_t addr = m_repo->getAddressAtIndex(row);
	std::string newVal = m_valueProvider->getCurrentValue(addr, m_displayType);
	if (newVal != m_rowCurrentValues[row]) {
		m_rowCurrentValues[row] = std::move(newVal);
		return true;
	}
	return false;
}
