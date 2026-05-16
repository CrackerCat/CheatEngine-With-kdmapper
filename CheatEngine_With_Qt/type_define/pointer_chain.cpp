#include "pointer_chain.h"
#include "interface/imemory_accessor.h"

QString PointerChain::toString() const
{
    if (!isValid())
        return {};

    QString result = QStringLiteral("[") + baseAddressText;

    for (size_t i = 0; i < levels.size(); ++i) {
        result += QString("+0x%1->").arg(static_cast<long long>(levels[i].offset), 0, 16);
        result += QStringLiteral("?????");
    }
    result += QChar(']');
    return result;
}

bool resolvePointerAddress(const PointerChain& chain,
                           const std::shared_ptr<IMemoryAccessor>& mem,
                           uint64_t& finalAddress)
{
    if (!chain.isValid() || !mem) return false;

    uint64_t current = chain.baseAddress;
    for (const auto& level : chain.levels) {
        uint64_t ptrValue = 0;
        if (!mem->read(current, &ptrValue, sizeof(ptrValue)))
            return false;
        current = ptrValue + level.offset;
    }
    finalAddress = current;
    return true;
}
