#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include <memory>
#include <variant>

enum class ScanMode { First, Next };

enum class ScanType
{
    ExactValue,
    GreaterThan,
    LessThan,
    Between,
	UnknownInitial, //只有首次扫描才会有这个选项，表示不限制初始值，后续再根据用户输入的数值进行过滤
    StringScan  
};

enum class NextScanType
{
    Equal,
    NotEqual,
    Increased,
    Decreased,
    Changed,
    Unchanged,
    Between,
    IncreasedBy,
    DecreasedBy,
	EndsWith,
    Compare_to_First_Scan
};

enum class ScanDataType : uint8_t {
    Bit,
    Int8,
    Int16,
    Int32,
    Int64,
    Float32,
    Float64,
    AsciiString, 
    Utf8String,
    Utf16String,
    ByteArray,
    All,
    Structure
};

inline size_t scanDataTypeSize(ScanDataType t) {
    switch (t) {
    case ScanDataType::Bit:     return 1; // Bit 通常按字节读取后再处理位
    case ScanDataType::Int8:    return 1;
    case ScanDataType::Int16:   return 2;
    case ScanDataType::Int32:   return 4;
    case ScanDataType::Int64:   return 8;
    case ScanDataType::Float32: return 4;
    case ScanDataType::Float64: return 8;
    case ScanDataType::AsciiString: return 0; //特殊类型处理
    case ScanDataType::Utf8String:  return 0; 
    case ScanDataType::Utf16String: return 0;
    case ScanDataType::ByteArray:   return 0;
    case ScanDataType::All:         return 8; // 按最大长度读取以兼容所有子类型
    case ScanDataType::Structure:   return 0;
    default:                        return 0;
    }
}

// 内存属性过滤结构体（用于限定扫描范围）
struct MemoryFilter {
    // ---- 内存状态 (stateFilter) ----
    static constexpr uint32_t Commit  = 0x1000;   // MEM_COMMIT
    static constexpr uint32_t Reserve = 0x2000;   // MEM_RESERVE
    static constexpr uint32_t Free    = 0x10000;  // MEM_FREE

    // ---- 内存类型 (typeFilter) ----
    static constexpr uint32_t TypePrivate = 0x20000;   // MEM_PRIVATE
    static constexpr uint32_t TypeImage   = 0x1000000; // MEM_IMAGE
    static constexpr uint32_t TypeMapped  = 0x40000;   // MEM_MAPPED

    // ---- 抽象访问权限 (accessFilter) ----
    static constexpr uint32_t AccessRead    = 1;
    static constexpr uint32_t AccessWrite   = 2;
    static constexpr uint32_t AccessExecute = 4;

    // ---- 具体页面保护属性 (protectFilter) ----
    static constexpr uint32_t ProtectNoAccess        = 0x0001;
    static constexpr uint32_t ProtectReadOnly        = 0x0002;
    static constexpr uint32_t ProtectReadWrite       = 0x0004;
    static constexpr uint32_t ProtectWriteCopy       = 0x0008;
    static constexpr uint32_t ProtectExecute         = 0x0010;
    static constexpr uint32_t ProtectExecuteRead     = 0x0020;
    static constexpr uint32_t ProtectExecuteReadWrite = 0x0040;
    static constexpr uint32_t ProtectExecuteWriteCopy = 0x0080;
    static constexpr uint32_t ProtectGuard           = 0x0100;

    // ===== 过滤条件（位掩码，0 表示不限制该类别） =====
    uint32_t stateFilter   = Commit;                // 默认：已提交
    uint32_t typeFilter    = TypePrivate | TypeImage | TypeMapped; // 默认：全部三种
    uint32_t accessFilter  = AccessRead;            // 默认：至少可读
    uint32_t protectFilter = 0;                     // 0 = 不按具体保护属性过滤

    // ===== 便利快捷开关（Hi-Level 过滤，与 accessFilter 配合使用） =====
    bool Writable          = false;
    bool Executable        = false;  
    bool CopyOnWrite       = false;
};

// 仅在 Between 时使用
struct ValueParams {
    uint64_t value1 = 0;
    uint64_t value2 = 0;   
};

// 字符串参数
struct StringParams {
    std::string text;
    bool caseSensitive = true;
};

// 字节数组 (AOB) 参数
// mask[i] 是 nibble 级掩码：
//   0xFF — 全字节匹配
//   0xF0 — 仅高半字节匹配 (如 "3?")
//   0x0F — 仅低半字节匹配 (如 "?E")
//   0x00 — 完全通配 (如 "??")
struct AobParams {
    std::vector<uint8_t> pattern;
    std::vector<uint8_t> mask;   // 原为 vector<bool>，改为 nibble 级别 uint8_t 掩码
};

// 结构体中的一个成员
struct StructureMember {
    ScanDataType type;      // 成员的数据类型 (如 Int32, Float)
    ValueParams criteria;   // 匹配该成员的数值条件
    size_t offsetFromPrev;  // 距离上一个成员末尾的偏移字节数
};

// 结构体扫描参数
struct StructureParams {
    std::vector<StructureMember> members;

    // 辅助方法：计算这个结构体在内存中占用的总跨度
    size_t totalSpan() const {
        size_t span = 0;
        for (const auto& m : members) {
            span += m.offsetFromPrev + scanDataTypeSize(m.type);
        }
        return span;
    }
};


// 统一特殊类型参数体
using ScanParams = std::variant<ValueParams, StringParams, AobParams, StructureParams>;


namespace AllTypeMask {
    enum : uint16_t {
        Int8    = 1 << 0,
        Int16   = 1 << 1,
        Int32   = 1 << 2,
        Int64   = 1 << 3,
        Float   = 1 << 4,
        Double  = 1 << 5
    };
}

#pragma pack(push, 1)
struct ScanResult
{
    uint64_t address;
    uint16_t typeMask = 0; 
    
    // 辅助方法：判断是否包含某种类型
    bool hasType(uint16_t mask) const { return (typeMask & mask) != 0; }
};
#pragma pack(pop)


// 扫描请求（唯一入口）
struct ScanRequest {
    ScanMode    mode = ScanMode::First;
    ScanDataType dataType = ScanDataType::Int32;

    size_t      alignment = 1;
    ScanType    firstType = ScanType::ExactValue;
    NextScanType nextType = NextScanType::Equal;

    // 模块过滤（0 表示不作限制）
    uint64_t moduleBase = 0;
    uint64_t moduleSize = 0;

    // 具体参数（由 mode 和 dataType 决定取哪种）
    ScanParams  params;

    // 内存属性过滤
    MemoryFilter memFilter;

    bool percentMode = false;
    bool containApproximateValue = false;
    bool notMatch = false;          // 勾选"非"时反转匹配条件

    std::shared_ptr<const std::vector<ScanResult>> prevResults = nullptr;
};

inline bool isFloatingPoint(ScanDataType t) {
    return t == ScanDataType::Float32 || t == ScanDataType::Float64;
}

inline bool isStringType(ScanDataType t) {
    return t == ScanDataType::AsciiString || t == ScanDataType::Utf16String || t == ScanDataType::Utf8String;
}

inline bool isByteArrayType(ScanDataType t) {
    return t == ScanDataType::ByteArray;
}

inline std::string scanDataTypeToString(ScanDataType t) {
    switch (t) {
    case ScanDataType::Bit:         return "Bit";
    case ScanDataType::Int8:        return "Byte";
    case ScanDataType::Int16:       return "2 Bytes";
    case ScanDataType::Int32:       return "4 Bytes";
    case ScanDataType::Int64:       return "8 Bytes";
    case ScanDataType::Float32:     return "Float";
    case ScanDataType::Float64:     return "Double";
    case ScanDataType::AsciiString: return "String (Ascii)";
    case ScanDataType::Utf8String:  return "String (UTF-8)";
    case ScanDataType::Utf16String: return "String (UTF-16)";
    case ScanDataType::ByteArray:   return "Array of Byte";
    case ScanDataType::All:         return "All";
    case ScanDataType::Structure:   return "Structure";
    default:                        return "Unknown";
    }
}