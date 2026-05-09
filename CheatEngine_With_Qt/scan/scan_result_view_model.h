#pragma once
#include <QAbstractTableModel>
#include "scan_data_stream_define.h"
#include "value_provider_interface.h"

class ScanResultRepository;

/// @brief 为 QTableView 提供数据的适配器。
///        只依赖 Repository，不处理数据存储或刷新。
class ScanResultViewModel : public QAbstractTableModel {
	Q_OBJECT
public:
	explicit ScanResultViewModel(ScanResultRepository* repo, IValueProvider* valueProvider, QObject* parent = nullptr);

	// 通知 View 数据已整体变更,保留原有全量刷新（用于扫描完成等场景） 
	void onRepositoryReplaced();

	// 由外部（如定时器）调用，检测所有数据的变化并自动触发增量更新
	void refreshCurrentValues();

	void ensureCacheSize();

	// 通知 View 部分行更新
	void notifyRowsModified(int minRow, int maxRow);


	void setDisplayType(ScanDataType type);
	ScanDataType getDisplayType() const { return m_displayType; }

	// QAbstractTableModel 接口
	int rowCount(const QModelIndex& parent = QModelIndex()) const override;
	int columnCount(const QModelIndex& parent = QModelIndex()) const override;
	QVariant data(const QModelIndex& index, int role) const override;
	QVariant headerData(int section, Qt::Orientation, int role) const override;

	uint64_t getAddress(int row) const;


private:
	bool  updateRowCache(int row);               // 更新某行的缓存值

	//QString getDisplayValueForColumn(uint64_t addr, int column) const;

	ScanResultRepository* m_repo;   // 不拥有所有权
	IValueProvider* m_valueProvider;  // 不持有所有权
	ScanDataType m_displayType = ScanDataType::Int32;


	std::vector<uint64_t>         m_rowAddresses;          // 每行的内存地址
	std::vector<std::string>      m_rowCurrentValues; // 缓存当前值用于检测变化


	static constexpr int MAX_DISPLAY = 10000;
	static constexpr int STRING_DISPLAY_MAX = 64;   // 字符串最多显示字节数
	static constexpr int BYTEARRAY_DISPLAY_MAX = 32; // 字节数组最多显示字节数
};