#pragma once
#include <QString>
#include <QStringList>
#include <QVariant>
#include <vector>
#include <memory>
#include <cstdint>
#include "pointer_chain.h"
#include "interface/imemory_accessor.h"
#include "scan/scan_data_stream_define.h"

class AddressItem {
    friend class AddressListModel;
public:
    // 将原本散落的 traits 直接内聚为黑盒的强类型属性
    enum class Type : uint8_t { Int8, Int16, Int32, Int64, Float, Double, String, ByteArray };
    enum class Encoding : uint8_t { ASCII, UTF8, UTF16 };

    struct Config {
        QString     description;
        uint64_t    address = 0;
        uint64_t    rawValue = 0;
        Type        type = Type::Int32;
        bool        frozen = false;
        bool        hexDisplay = false;
        bool        signedDisplay = true;
        Encoding    encoding = Encoding::UTF8;
        int         stringLength = 0;
        std::vector<uint8_t> buffer;
        PointerChain pointerChain;
    };

    static AddressItem create(const Config& cfg);
    AddressItem() = default;

    // 将当前状态导出为 Config（供 Add_Or_Change_Address_Dialog 等使用）
    Config toConfig() const;

    /// 以 Config 增量更新当前状态（代替 Model 逐个写私有成员）
    void applyConfig(const Config& cfg);

    // 对外极简的只读暴露
    const QString& description() const noexcept { return m_description; }
    uint64_t       address() const noexcept { return m_address; }
    Type           type() const noexcept { return m_type; }
    bool           isFrozen() const noexcept { return m_frozen; }
    bool           isChanged() const noexcept { return m_changed; }
    bool           isPointer() const noexcept { return m_pointerChain.isValid(); }
    const PointerChain& pointerChain() const noexcept { return m_pointerChain; }

    // ★ 绝杀优化：由对象自己决定在 UI 视图的某一列显示什么，彻底解放 Model
    QVariant displayData(int column, int role) const;

    // ★ 交互自治：外部把输入文本和执行句柄给它，怎么转、怎么写由它内部自己消化
    bool tryEditValue(const QString& input, const std::shared_ptr<IMemoryAccessor>& mem);
    
    // 自治操作：自己负责高频刷新和冻结写回
    bool refreshValue(const std::shared_ptr<IMemoryAccessor>& mem);
    bool freezeWrite(const std::shared_ptr<IMemoryAccessor>& mem) const;

    // ======================== 聚合自 value_traits.h 的静态方法 ========================
    
    /// 获取 Type 对应的字节数（字符串/字节数组返回默认读取大小）
    static size_t typeSize(Type t);
    
    /// 判断是否为整数类型
    static bool isIntegerType(Type t) {
        return t == Type::Int8 || t == Type::Int16 ||
               t == Type::Int32 || t == Type::Int64;
    }
    
    /// 判断是否为浮点类型
    static bool isFloatType(Type t) {
        return t == Type::Float || t == Type::Double;
    }
    
    /// 判断是否为数值类型（整数或浮点）
    static bool isNumericType(Type t) {
        return isIntegerType(t) || isFloatType(t);
    }
    
    /// 判断是否为字符串类型
    static bool isStringType(Type t) {
        return t == Type::String;
    }
    
    /// 判断是否为字节数组类型
    static bool isByteArrayType(Type t) {
        return t == Type::ByteArray;
    }

    /// 获取类型的显示名称
    static QString typeName(Type t);

    /// 将 ScanDataType 转换为 AddressItem::Type
    static Type fromScanDataType(ScanDataType sdt);

    /// 从 ScanDataType 推断字符串编码（仅 AsciiString/Utf8String/Utf16String 有意义）
    static Encoding encodingFromScanDataType(ScanDataType sdt);

    /// 获取所有类型选项列表（供 Delegate 通用菜单使用）
    static QStringList getTypeOptions();
    
    /// 获取当前类型的显示模式选项（供 DisplayMode Delegate 使用）
    QStringList getDisplayOptions() const;

private:
    QString     m_description;
    uint64_t    m_address = 0;
    uint64_t    m_rawValue = 0;
    Type        m_type = Type::Int32;
    bool        m_frozen = false;
    uint64_t    m_lastRawValue = 0;
    bool        m_changed = false;
    bool        m_hexDisplay = false;
    bool        m_signedDisplay = true;
    Encoding    m_encoding = Encoding::UTF8;
    std::vector<uint8_t> m_buffer;
    int         m_stringLength = 0;
    PointerChain m_pointerChain;

    // 内部私有转换方法，不对外暴露
    size_t  typeSize() const { return typeSize(m_type); }
    bool    isNumericType() const { return isNumericType(m_type); }
    bool    isStringType() const { return isStringType(m_type); }
    bool    isByteArrayType() const { return isByteArrayType(m_type); }
    QString formatNumeric() const;
    QString formatString() const;
    QString formatByteArray() const;
    QString formattedValue() const;
    void    updateResolvedAddressCache();
    uint64_t getEffectiveAddress(const std::shared_ptr<IMemoryAccessor>& mem) const;
    void    truncateBuffer(int readLen);

    QString m_cachedAddressText;
    bool    m_cachedIsBase = false;
};
